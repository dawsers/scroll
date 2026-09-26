// Shared glue between the space-template engine (G1-G3) and both public
// surfaces that need it: the `space_template` command
// (sway/commands/space_template.c) and the get/apply_space_template IPC
// messages (sway/ipc-server.c). Kept as one file so the mapping-resolution
// logic -- con_id/criteria -> live view -- and its error reporting exist
// exactly once.
#include "sway/space_template_ipc.h"

#include <string.h>

#include "list.h"
#include "sway/criteria.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/tree/node.h"
#include "sway/tree/space_template_apply.h"
#include "sway/tree/space_template_json.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"

// --- get_space_template ---------------------------------------------

struct json_object *space_template_ipc_get(const char *name) {
	struct sway_space_template_json_result result = space_template_load(name);
	struct json_object *json;
	if (result.error == SPACE_TEMPLATE_JSON_OK) {
		json = space_template_to_json(result.template);
		space_template_destroy(result.template);
	} else {
		json = json_object_new_object();
		json_object_object_add(json, "success", json_object_new_boolean(false));
		json_object_object_add(json, "error",
			json_object_new_string(space_template_json_error_str(result.error)));
		if (result.path) {
			json_object_object_add(json, "path", json_object_new_string(result.path));
		}
		if (result.detail) {
			json_object_object_add(json, "detail", json_object_new_string(result.detail));
		}
	}
	space_template_json_result_destroy(&result);
	return json;
}

// --- apply_space_template ---------------------------------------------

static struct json_object *error_reply(const char *error, const char *slot) {
	struct json_object *json = json_object_new_object();
	json_object_object_add(json, "success", json_object_new_boolean(false));
	json_object_object_add(json, "error", json_object_new_string(error));
	if (slot) {
		struct json_object *slots = json_object_new_array();
		json_object_array_add(slots, json_object_new_string(slot));
		json_object_object_add(json, "slots", slots);
	}
	return json;
}

static struct json_object *error_reply_slots(const char *error, list_t *slots) {
	struct json_object *json = json_object_new_object();
	json_object_object_add(json, "success", json_object_new_boolean(false));
	json_object_object_add(json, "error", json_object_new_string(error));
	if (slots && slots->length > 0) {
		struct json_object *slots_json = json_object_new_array();
		for (int i = 0; i < slots->length; ++i) {
			json_object_array_add(slots_json, json_object_new_string(slots->items[i]));
		}
		json_object_object_add(json, "slots", slots_json);
	}
	return json;
}

// Resolves one {"slot": "...", "con_id": N} or {"slot": "...", "criteria":
// "..."} mapping entry to a live view. Returns NULL and sets *out_error
// (heap-owned, caller must build the reply from it) / *out_slot
// (heap-owned copy of the offending slot, or NULL if the entry has none) on
// any failure. Never mutates the tree -- criteria matching only reads.
static bool resolve_mapping(struct json_object *mapping,
		struct sway_space_template_binding *out_binding,
		struct json_object **out_error_reply) {
	struct json_object *slot_json = NULL;
	if (!json_object_object_get_ex(mapping, "slot", &slot_json) ||
			json_object_get_type(slot_json) != json_type_string ||
			json_object_get_string(slot_json)[0] == '\0') {
		*out_error_reply = error_reply("mapping is missing a non-empty \"slot\" string", NULL);
		return false;
	}
	const char *slot = json_object_get_string(slot_json);

	struct json_object *con_id_json = NULL;
	struct json_object *criteria_json = NULL;
	bool has_con_id = json_object_object_get_ex(mapping, "con_id", &con_id_json);
	bool has_criteria = json_object_object_get_ex(mapping, "criteria", &criteria_json);

	if (has_con_id == has_criteria) { // neither, or both -- ambiguous request shape
		*out_error_reply = error_reply(
			"mapping must have exactly one of \"con_id\" or \"criteria\"", slot);
		return false;
	}

	struct sway_view *view = NULL;

	if (has_con_id) {
		if (json_object_get_type(con_id_json) != json_type_int) {
			*out_error_reply = error_reply("\"con_id\" must be an integer", slot);
			return false;
		}
		size_t con_id = (size_t)json_object_get_int64(con_id_json);
		struct sway_node *node = node_by_id(con_id);
		if (!node || node->type != N_CONTAINER || !node->sway_container->view) {
			*out_error_reply = error_reply("con_id does not refer to a live view", slot);
			return false;
		}
		view = node->sway_container->view;
	} else {
		if (json_object_get_type(criteria_json) != json_type_string) {
			*out_error_reply = error_reply("\"criteria\" must be a string", slot);
			return false;
		}
		char *raw = strdup(json_object_get_string(criteria_json));
		char *parse_error = NULL;
		struct criteria *criteria = criteria_parse(raw, &parse_error);
		free(raw);
		if (!criteria) {
			*out_error_reply = error_reply(parse_error ? parse_error : "invalid criteria", slot);
			free(parse_error);
			return false;
		}

		list_t *containers = criteria_get_containers(criteria);
		criteria_destroy(criteria);

		list_t *matches = create_list();
		for (int i = 0; i < containers->length; ++i) {
			struct sway_container *container = containers->items[i];
			if (container->view) {
				list_add(matches, container->view);
			}
		}
		list_free(containers);

		if (matches->length == 0) {
			list_free(matches);
			*out_error_reply = error_reply("criteria matched no live view", slot);
			return false;
		}
		if (matches->length > 1) {
			list_free(matches);
			*out_error_reply = error_reply("criteria matched more than one view (ambiguous)", slot);
			return false;
		}
		view = matches->items[0];
		list_free(matches);
	}

	out_binding->slot = strdup(slot); // owned by the caller of resolve_mapping(); see free loop below
	out_binding->view = view;
	return true;
}

static void free_binding_slots(list_t *bindings) {
	for (int i = 0; i < bindings->length; ++i) {
		struct sway_space_template_binding *binding = bindings->items[i];
		free((char *)binding->slot);
		free(binding);
	}
	list_free(bindings);
}

struct json_object *space_template_ipc_apply(struct json_object *request,
		struct sway_workspace *workspace) {
	if (!request || json_object_get_type(request) != json_type_object) {
		return error_reply("request must be a JSON object", NULL);
	}

	struct json_object *name_json = NULL;
	if (!json_object_object_get_ex(request, "name", &name_json) ||
			json_object_get_type(name_json) != json_type_string) {
		return error_reply("request is missing a \"name\" string", NULL);
	}
	const char *name = json_object_get_string(name_json);

	struct json_object *workspace_json = NULL;
	if (json_object_object_get_ex(request, "workspace", &workspace_json)) {
		if (json_object_get_type(workspace_json) != json_type_string ||
				strcmp(json_object_get_string(workspace_json), "current") != 0) {
			return error_reply("\"workspace\" must be \"current\" (v1 supports only the current workspace)", NULL);
		}
	}
	if (!workspace) {
		return error_reply("no current workspace", NULL);
	}

	struct json_object *mappings_json = NULL;
	if (!json_object_object_get_ex(request, "mappings", &mappings_json) ||
			json_object_get_type(mappings_json) != json_type_array) {
		return error_reply("request is missing a \"mappings\" array", NULL);
	}

	list_t *bindings = create_list(); // struct sway_space_template_binding *, slot owned, view borrowed
	int n = json_object_array_length(mappings_json);
	for (int i = 0; i < n; ++i) {
		struct json_object *mapping = json_object_array_get_idx(mappings_json, i);
		struct sway_space_template_binding *binding = calloc(1, sizeof(struct sway_space_template_binding));
		struct json_object *error_reply_json = NULL;
		if (!resolve_mapping(mapping, binding, &error_reply_json)) {
			free(binding);
			free_binding_slots(bindings);
			return error_reply_json;
		}
		list_add(bindings, binding);
	}

	struct sway_space_template_json_result load_result = space_template_load(name);
	if (load_result.error != SPACE_TEMPLATE_JSON_OK) {
		free_binding_slots(bindings);
		struct json_object *json = error_reply(space_template_json_error_str(load_result.error), NULL);
		space_template_json_result_destroy(&load_result);
		return json;
	}

	struct sway_space_template_apply_result apply_result_value =
		space_template_apply(load_result.template, workspace, bindings);

	space_template_destroy(load_result.template);
	free_binding_slots(bindings);

	struct json_object *json;
	if (apply_result_value.error == SPACE_TEMPLATE_APPLY_OK) {
		json = json_object_new_object();
		json_object_object_add(json, "success", json_object_new_boolean(true));
	} else {
		json = error_reply_slots(
			space_template_apply_error_str(apply_result_value.error), apply_result_value.slots);
	}
	space_template_apply_result_destroy(&apply_result_value);

	return json;
}
