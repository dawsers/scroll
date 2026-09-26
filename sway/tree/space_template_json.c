// v1 JSON schema (see docs/research/space-templates-final/plan.md sec. 4)
// and $XDG_CONFIG_HOME/scroll/templates/<name>.json persistence for
// sway_space_template. Pure serialization/parsing logic here delegates all
// tree-shape/slot/geometry structural rules to space_template_validate()
// (G1) rather than re-implementing them -- this file is only responsible
// for JSON <-> struct field mapping, type/enum/regex/version checks, and
// filesystem I/O.
#include "sway/tree/space_template_json.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <errno.h>
#include <json.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "list.h"
#include "log.h"
#include "stringop.h"

// --- error/result plumbing -------------------------------------------------

const char *space_template_json_error_str(enum sway_space_template_json_error error) {
	switch (error) {
	case SPACE_TEMPLATE_JSON_OK:
		return "ok";
	case SPACE_TEMPLATE_JSON_ERROR_PARSE:
		return "malformed JSON";
	case SPACE_TEMPLATE_JSON_ERROR_ROOT_TYPE:
		return "document root must be an object";
	case SPACE_TEMPLATE_JSON_ERROR_MISSING_FIELD:
		return "missing required field";
	case SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE:
		return "field has the wrong type";
	case SPACE_TEMPLATE_JSON_ERROR_UNSUPPORTED_VERSION:
		return "unsupported version";
	case SPACE_TEMPLATE_JSON_ERROR_INVALID_ENUM:
		return "invalid enum value";
	case SPACE_TEMPLATE_JSON_ERROR_INVALID_NUMBER:
		return "invalid number";
	case SPACE_TEMPLATE_JSON_ERROR_INVALID_REGEX:
		return "invalid regular expression";
	case SPACE_TEMPLATE_JSON_ERROR_TOO_LARGE:
		return "document or field exceeds a size limit";
	case SPACE_TEMPLATE_JSON_ERROR_STRUCTURAL:
		return "structurally invalid template";
	case SPACE_TEMPLATE_JSON_ERROR_UNSAFE_NAME:
		return "unsafe template name";
	case SPACE_TEMPLATE_JSON_ERROR_NOT_FOUND:
		return "template not found";
	case SPACE_TEMPLATE_JSON_ERROR_IO:
		return "I/O error";
	}
	return "unknown error";
}

void space_template_json_result_destroy(struct sway_space_template_json_result *result) {
	if (!result) {
		return;
	}
	free(result->path);
	result->path = NULL;
	free(result->detail);
	result->detail = NULL;
	// result->template is intentionally left untouched -- caller-owned.
}

static struct sway_space_template_json_result json_error(
		enum sway_space_template_json_error error, char *path, char *detail) {
	struct sway_space_template_json_result result = {
		.error = error, .path = path, .detail = detail, .template = NULL,
	};
	return result;
}

static struct sway_space_template_json_result json_ok(struct sway_space_template *template) {
	struct sway_space_template_json_result result = {
		.error = SPACE_TEMPLATE_JSON_OK, .path = NULL, .detail = NULL, .template = template,
	};
	return result;
}

// --- enum <-> string mapping -------------------------------------------

struct enum_str_map {
	const char *str;
	int value;
};

static const struct enum_str_map scroller_mode_map[] = {
	{ "none", SPACE_TEMPLATE_SCROLLER_NONE },
	{ "horizontal", SPACE_TEMPLATE_SCROLLER_HORIZONTAL },
	{ "vertical", SPACE_TEMPLATE_SCROLLER_VERTICAL },
};

static const struct enum_str_map insert_map[] = {
	{ "before", SPACE_TEMPLATE_INSERT_BEFORE },
	{ "after", SPACE_TEMPLATE_INSERT_AFTER },
	{ "beginning", SPACE_TEMPLATE_INSERT_BEGINNING },
	{ "end", SPACE_TEMPLATE_INSERT_END },
};

static const struct enum_str_map fit_map[] = {
	{ "nofit", SPACE_TEMPLATE_FIT_NOFIT },
	{ "fitsplit", SPACE_TEMPLATE_FIT_FITSPLIT },
	{ "fitfraction", SPACE_TEMPLATE_FIT_FITFRACTION },
};

static const struct enum_str_map layout_map[] = {
	{ "none", SPACE_TEMPLATE_LAYOUT_NONE },
	{ "horizontal", SPACE_TEMPLATE_LAYOUT_HORIZONTAL },
	{ "vertical", SPACE_TEMPLATE_LAYOUT_VERTICAL },
};

#define ENUM_MAP_LEN(map) (sizeof(map) / sizeof((map)[0]))

static const char *enum_to_str(const struct enum_str_map *map, size_t n, int value) {
	for (size_t i = 0; i < n; ++i) {
		if (map[i].value == value) {
			return map[i].str;
		}
	}
	return "none";
}

static bool str_to_enum(const struct enum_str_map *map, size_t n, const char *str, int *out) {
	for (size_t i = 0; i < n; ++i) {
		if (strcmp(map[i].str, str) == 0) {
			*out = map[i].value;
			return true;
		}
	}
	return false;
}

// --- regex validation (compile-only; nothing is retained) -------------

static bool regex_is_valid(const char *pattern, char **out_error) {
	int errorcode;
	PCRE2_SIZE erroroffset;
	pcre2_code *code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
		PCRE2_UTF | PCRE2_UCP, &errorcode, &erroroffset, NULL);
	if (!code) {
		if (out_error) {
			PCRE2_UCHAR buffer[256];
			pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
			*out_error = format_str("%s (at offset %zu)", (char *)buffer, (size_t)erroroffset);
		}
		return false;
	}
	pcre2_code_free(code);
	return true;
}

// --- field helpers -------------------------------------------------------

static bool get_number_field(struct json_object *obj, const char *key, const char *path,
		double *out, struct sway_space_template_json_result *err) {
	struct json_object *value = NULL;
	if (!json_object_object_get_ex(obj, key, &value)) {
		*err = json_error(SPACE_TEMPLATE_JSON_ERROR_MISSING_FIELD, format_str("%s.%s", path, key), NULL);
		return false;
	}
	enum json_type type = json_object_get_type(value);
	if (type != json_type_double && type != json_type_int) {
		*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE,
			format_str("%s.%s", path, key), strdup("expected a number"));
		return false;
	}
	double d = json_object_get_double(value);
	if (!isfinite(d)) {
		*err = json_error(SPACE_TEMPLATE_JSON_ERROR_INVALID_NUMBER,
			format_str("%s.%s", path, key), strdup("must be finite"));
		return false;
	}
	*out = d;
	return true;
}

// --- node parsing --------------------------------------------------------

static struct sway_space_template_node *parse_node(struct json_object *json_node,
		const char *path, bool is_floating, struct sway_space_template_json_result *err) {
	if (!json_node || json_object_get_type(json_node) != json_type_object) {
		*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, strdup(path), strdup("expected an object"));
		return NULL;
	}

	struct json_object *children_json = NULL;
	bool has_children = json_object_object_get_ex(json_node, "children", &children_json);
	struct json_object *slot_json = NULL;
	bool has_slot = json_object_object_get_ex(json_node, "slot", &slot_json);

	struct sway_space_template_node *node = NULL;

	if (has_children) {
		if (json_object_get_type(children_json) != json_type_array) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE,
				format_str("%s.children", path), strdup("expected an array"));
			return NULL;
		}

		enum sway_space_template_layout layout = SPACE_TEMPLATE_LAYOUT_NONE;
		struct json_object *layout_json = NULL;
		if (json_object_object_get_ex(json_node, "layout", &layout_json)) {
			if (json_object_get_type(layout_json) != json_type_string) {
				*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE,
					format_str("%s.layout", path), strdup("expected a string"));
				return NULL;
			}
			int value;
			if (!str_to_enum(layout_map, ENUM_MAP_LEN(layout_map),
					json_object_get_string(layout_json), &value)) {
				*err = json_error(SPACE_TEMPLATE_JSON_ERROR_INVALID_ENUM,
					format_str("%s.layout", path), strdup(json_object_get_string(layout_json)));
				return NULL;
			}
			layout = value;
		}

		node = space_template_node_create_inner(layout);

		int n = json_object_array_length(children_json);
		for (int i = 0; i < n; ++i) {
			char *child_path = format_str("%s.children[%d]", path, i);
			struct sway_space_template_node *child =
				parse_node(json_object_array_get_idx(children_json, i), child_path, is_floating, err);
			free(child_path);
			if (!child) {
				space_template_node_destroy(node);
				return NULL;
			}
			space_template_node_add_child(node, child);
		}
	} else if (has_slot) {
		if (json_object_get_type(slot_json) != json_type_string) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE,
				format_str("%s.slot", path), strdup("expected a string"));
			return NULL;
		}
		node = space_template_node_create_leaf(json_object_get_string(slot_json));

		struct json_object *hint_json = NULL;
		if (json_object_object_get_ex(json_node, "view_hint", &hint_json)) {
			if (json_object_get_type(hint_json) != json_type_object) {
				*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE,
					format_str("%s.view_hint", path), strdup("expected an object"));
				space_template_node_destroy(node);
				return NULL;
			}
			static const char *hint_keys[3] = { "app_id", "class", "title" };
			char *hints[3] = { NULL, NULL, NULL };
			bool failed = false;
			for (int i = 0; i < 3 && !failed; ++i) {
				struct json_object *hint_value = NULL;
				if (!json_object_object_get_ex(hint_json, hint_keys[i], &hint_value)) {
					continue;
				}
				if (json_object_get_type(hint_value) != json_type_string) {
					*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE,
						format_str("%s.view_hint.%s", path, hint_keys[i]), strdup("expected a string"));
					failed = true;
					break;
				}
				const char *value = json_object_get_string(hint_value);
				if (strlen(value) > SPACE_TEMPLATE_JSON_MAX_REGEX_LEN) {
					*err = json_error(SPACE_TEMPLATE_JSON_ERROR_TOO_LARGE,
						format_str("%s.view_hint.%s", path, hint_keys[i]),
						format_str("exceeds the %d character limit", SPACE_TEMPLATE_JSON_MAX_REGEX_LEN));
					failed = true;
					break;
				}
				char *regex_error = NULL;
				if (!regex_is_valid(value, &regex_error)) {
					*err = json_error(SPACE_TEMPLATE_JSON_ERROR_INVALID_REGEX,
						format_str("%s.view_hint.%s", path, hint_keys[i]), regex_error);
					failed = true;
					break;
				}
				hints[i] = strdup(value);
			}
			if (!failed) {
				space_template_node_set_hints(node, hints[0], hints[1], hints[2]);
			}
			for (int i = 0; i < 3; ++i) {
				free(hints[i]);
			}
			if (failed) {
				space_template_node_destroy(node);
				return NULL;
			}
		}
	} else {
		*err = json_error(SPACE_TEMPLATE_JSON_ERROR_MISSING_FIELD, strdup(path),
			strdup("node must have either \"children\" or \"slot\""));
		return NULL;
	}

	if (is_floating) {
		double x, y, width, height;
		if (!get_number_field(json_node, "x", path, &x, err) ||
				!get_number_field(json_node, "y", path, &y, err) ||
				!get_number_field(json_node, "width", path, &width, err) ||
				!get_number_field(json_node, "height", path, &height, err)) {
			space_template_node_destroy(node);
			return NULL;
		}
		space_template_node_set_floating_geometry(node, x, y, width, height);
	} else {
		struct json_object *size_json = NULL;
		if (!json_object_object_get_ex(json_node, "size", &size_json) ||
				json_object_get_type(size_json) != json_type_object) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_MISSING_FIELD,
				format_str("%s.size", path), strdup("expected a \"size\" object"));
			space_template_node_destroy(node);
			return NULL;
		}
		char *size_path = format_str("%s.size", path);
		double width_fraction = 0, height_fraction = 0;
		bool ok = get_number_field(size_json, "width_fraction", size_path, &width_fraction, err) &&
			get_number_field(size_json, "height_fraction", size_path, &height_fraction, err);
		free(size_path);
		if (!ok) {
			space_template_node_destroy(node);
			return NULL;
		}
		space_template_node_set_tiling_geometry(node, width_fraction, height_fraction);
	}

	return node;
}

static bool parse_scroller(struct json_object *obj, const char *path,
		struct sway_space_template_scroller *scroller, struct sway_space_template_json_result *err) {
	struct json_object *field = NULL;

	if (json_object_object_get_ex(obj, "mode", &field)) {
		if (json_object_get_type(field) != json_type_string) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, format_str("%s.mode", path), strdup("expected a string"));
			return false;
		}
		int value;
		if (!str_to_enum(scroller_mode_map, ENUM_MAP_LEN(scroller_mode_map), json_object_get_string(field), &value)) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_INVALID_ENUM, format_str("%s.mode", path), strdup(json_object_get_string(field)));
			return false;
		}
		scroller->mode = value;
	}
	if (json_object_object_get_ex(obj, "insert", &field)) {
		if (json_object_get_type(field) != json_type_string) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, format_str("%s.insert", path), strdup("expected a string"));
			return false;
		}
		int value;
		if (!str_to_enum(insert_map, ENUM_MAP_LEN(insert_map), json_object_get_string(field), &value)) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_INVALID_ENUM, format_str("%s.insert", path), strdup(json_object_get_string(field)));
			return false;
		}
		scroller->insert = value;
	}
	if (json_object_object_get_ex(obj, "fit", &field)) {
		if (json_object_get_type(field) != json_type_string) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, format_str("%s.fit", path), strdup("expected a string"));
			return false;
		}
		int value;
		if (!str_to_enum(fit_map, ENUM_MAP_LEN(fit_map), json_object_get_string(field), &value)) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_INVALID_ENUM, format_str("%s.fit", path), strdup(json_object_get_string(field)));
			return false;
		}
		scroller->fit = value;
	}
	if (json_object_object_get_ex(obj, "focus", &field)) {
		if (json_object_get_type(field) != json_type_boolean) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, format_str("%s.focus", path), strdup("expected a boolean"));
			return false;
		}
		scroller->focus = json_object_get_boolean(field);
	}
	if (json_object_object_get_ex(obj, "center_horizontal", &field)) {
		if (json_object_get_type(field) != json_type_boolean) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, format_str("%s.center_horizontal", path), strdup("expected a boolean"));
			return false;
		}
		scroller->center_horizontal = json_object_get_boolean(field);
	}
	if (json_object_object_get_ex(obj, "center_vertical", &field)) {
		if (json_object_get_type(field) != json_type_boolean) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, format_str("%s.center_vertical", path), strdup("expected a boolean"));
			return false;
		}
		scroller->center_vertical = json_object_get_boolean(field);
	}
	if (json_object_object_get_ex(obj, "reorder", &field)) {
		if (json_object_get_type(field) != json_type_boolean) {
			*err = json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, format_str("%s.reorder", path), strdup("expected a boolean"));
			return false;
		}
		scroller->reorder = json_object_get_boolean(field);
	}
	return true;
}

struct sway_space_template_json_result space_template_from_json(struct json_object *root) {
	if (!root || json_object_get_type(root) != json_type_object) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_ROOT_TYPE, strdup("$"), strdup("document root must be an object"));
	}

	struct sway_space_template_json_result err = json_ok(NULL);

	struct json_object *version_json = NULL;
	if (!json_object_object_get_ex(root, "version", &version_json) ||
			json_object_get_type(version_json) != json_type_int) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_MISSING_FIELD, strdup("$.version"), strdup("expected an integer"));
	}
	int version = json_object_get_int(version_json);
	if (version != SPACE_TEMPLATE_VERSION) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_UNSUPPORTED_VERSION, strdup("$.version"),
			format_str("unsupported version %d (expected %d)", version, SPACE_TEMPLATE_VERSION));
	}

	struct json_object *name_json = NULL;
	if (!json_object_object_get_ex(root, "name", &name_json) ||
			json_object_get_type(name_json) != json_type_string) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_MISSING_FIELD, strdup("$.name"), strdup("expected a string"));
	}
	const char *name = json_object_get_string(name_json);
	if (!space_template_name_is_safe(name)) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_UNSAFE_NAME, strdup("$.name"), strdup(name));
	}

	struct sway_space_template *template = space_template_create(name);

	struct json_object *scroller_json = NULL;
	if (json_object_object_get_ex(root, "scroller", &scroller_json)) {
		if (json_object_get_type(scroller_json) != json_type_object) {
			space_template_destroy(template);
			return json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, strdup("$.scroller"), strdup("expected an object"));
		}
		if (!parse_scroller(scroller_json, "$.scroller", &template->scroller, &err)) {
			space_template_destroy(template);
			return err;
		}
	}

	struct json_object *tiling_json = NULL;
	if (json_object_object_get_ex(root, "tiling", &tiling_json)) {
		if (json_object_get_type(tiling_json) != json_type_array) {
			space_template_destroy(template);
			return json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, strdup("$.tiling"), strdup("expected an array"));
		}
		int n = json_object_array_length(tiling_json);
		for (int i = 0; i < n; ++i) {
			char *path = format_str("$.tiling[%d]", i);
			struct sway_space_template_node *node =
				parse_node(json_object_array_get_idx(tiling_json, i), path, false, &err);
			free(path);
			if (!node) {
				space_template_destroy(template);
				return err;
			}
			space_template_add_tiling(template, node);
		}
	}

	struct json_object *floating_json = NULL;
	if (json_object_object_get_ex(root, "floating", &floating_json)) {
		if (json_object_get_type(floating_json) != json_type_array) {
			space_template_destroy(template);
			return json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, strdup("$.floating"), strdup("expected an array"));
		}
		int n = json_object_array_length(floating_json);
		for (int i = 0; i < n; ++i) {
			char *path = format_str("$.floating[%d]", i);
			struct sway_space_template_node *node =
				parse_node(json_object_array_get_idx(floating_json, i), path, true, &err);
			free(path);
			if (!node) {
				space_template_destroy(template);
				return err;
			}
			space_template_add_floating(template, node);
		}
	}

	struct json_object *focused_json = NULL;
	if (json_object_object_get_ex(root, "focused_slot", &focused_json)) {
		if (json_object_get_type(focused_json) != json_type_string) {
			space_template_destroy(template);
			return json_error(SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE, strdup("$.focused_slot"), strdup("expected a string"));
		}
		space_template_set_focused_slot(template, json_object_get_string(focused_json));
	}

	struct sway_space_template_validation_result validation = space_template_validate(template);
	if (validation.error != SPACE_TEMPLATE_VALID) {
		char *json_path = validation.path ? format_str("$.%s", validation.path) : strdup("$");
		free(validation.path);
		space_template_destroy(template);
		return json_error(SPACE_TEMPLATE_JSON_ERROR_STRUCTURAL, json_path,
			strdup(space_template_validation_error_str(validation.error)));
	}

	return json_ok(template);
}

struct sway_space_template_json_result space_template_from_json_string(const char *text) {
	if (!text) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_PARSE, strdup("$"), strdup("empty document"));
	}
	if (strlen(text) > SPACE_TEMPLATE_JSON_MAX_BYTES) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_TOO_LARGE, strdup("$"),
			format_str("document exceeds the %d byte limit", SPACE_TEMPLATE_JSON_MAX_BYTES));
	}
	enum json_tokener_error jerr = json_tokener_success;
	struct json_object *root = json_tokener_parse_verbose(text, &jerr);
	if (!root) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_PARSE, strdup("$"), strdup(json_tokener_error_desc(jerr)));
	}
	struct sway_space_template_json_result result = space_template_from_json(root);
	json_object_put(root);
	return result;
}

// --- serialization ---------------------------------------------------

static struct json_object *node_to_json(const struct sway_space_template_node *node, bool is_floating) {
	struct json_object *obj = json_object_new_object();
	if (node->children) {
		json_object_object_add(obj, "layout",
			json_object_new_string(enum_to_str(layout_map, ENUM_MAP_LEN(layout_map), node->layout)));
		struct json_object *children = json_object_new_array();
		for (int i = 0; i < node->children->length; ++i) {
			json_object_array_add(children, node_to_json(node->children->items[i], is_floating));
		}
		json_object_object_add(obj, "children", children);
	} else {
		json_object_object_add(obj, "slot", json_object_new_string(node->slot));
		if (node->hint_app_id || node->hint_class || node->hint_title) {
			struct json_object *hint = json_object_new_object();
			if (node->hint_app_id) {
				json_object_object_add(hint, "app_id", json_object_new_string(node->hint_app_id));
			}
			if (node->hint_class) {
				json_object_object_add(hint, "class", json_object_new_string(node->hint_class));
			}
			if (node->hint_title) {
				json_object_object_add(hint, "title", json_object_new_string(node->hint_title));
			}
			json_object_object_add(obj, "view_hint", hint);
		}
	}

	if (is_floating) {
		json_object_object_add(obj, "x", json_object_new_double(node->x));
		json_object_object_add(obj, "y", json_object_new_double(node->y));
		json_object_object_add(obj, "width", json_object_new_double(node->width));
		json_object_object_add(obj, "height", json_object_new_double(node->height));
	} else {
		struct json_object *size = json_object_new_object();
		json_object_object_add(size, "width_fraction", json_object_new_double(node->width_fraction));
		json_object_object_add(size, "height_fraction", json_object_new_double(node->height_fraction));
		json_object_object_add(obj, "size", size);
	}

	return obj;
}

struct json_object *space_template_to_json(const struct sway_space_template *template) {
	struct json_object *root = json_object_new_object();
	json_object_object_add(root, "version", json_object_new_int(template->version));
	json_object_object_add(root, "name", json_object_new_string(template->name));

	struct json_object *scroller = json_object_new_object();
	json_object_object_add(scroller, "mode", json_object_new_string(
		enum_to_str(scroller_mode_map, ENUM_MAP_LEN(scroller_mode_map), template->scroller.mode)));
	json_object_object_add(scroller, "insert", json_object_new_string(
		enum_to_str(insert_map, ENUM_MAP_LEN(insert_map), template->scroller.insert)));
	json_object_object_add(scroller, "fit", json_object_new_string(
		enum_to_str(fit_map, ENUM_MAP_LEN(fit_map), template->scroller.fit)));
	json_object_object_add(scroller, "focus", json_object_new_boolean(template->scroller.focus));
	json_object_object_add(scroller, "center_horizontal", json_object_new_boolean(template->scroller.center_horizontal));
	json_object_object_add(scroller, "center_vertical", json_object_new_boolean(template->scroller.center_vertical));
	json_object_object_add(scroller, "reorder", json_object_new_boolean(template->scroller.reorder));
	json_object_object_add(root, "scroller", scroller);

	struct json_object *tiling = json_object_new_array();
	if (template->tiling) {
		for (int i = 0; i < template->tiling->length; ++i) {
			json_object_array_add(tiling, node_to_json(template->tiling->items[i], false));
		}
	}
	json_object_object_add(root, "tiling", tiling);

	struct json_object *floating = json_object_new_array();
	if (template->floating) {
		for (int i = 0; i < template->floating->length; ++i) {
			json_object_array_add(floating, node_to_json(template->floating->items[i], true));
		}
	}
	json_object_object_add(root, "floating", floating);

	if (template->focused_slot && template->focused_slot[0]) {
		json_object_object_add(root, "focused_slot", json_object_new_string(template->focused_slot));
	}

	return root;
}

char *space_template_to_json_string(const struct sway_space_template *template) {
	struct json_object *root = space_template_to_json(template);
	char *result = strdup(json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
	json_object_put(root);
	return result;
}

// --- filesystem path resolution -----------------------------------------

bool space_template_name_is_safe(const char *name) {
	if (!name || !name[0]) {
		return false;
	}
	size_t len = strlen(name);
	if (len > SPACE_TEMPLATE_JSON_MAX_NAME_LEN) {
		return false;
	}
	for (size_t i = 0; i < len; ++i) {
		char c = name[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-';
		if (!ok) {
			return false;
		}
	}
	return true;
}

char *space_template_dir(void) {
	const char *config_home = getenv("XDG_CONFIG_HOME");
	if (config_home && config_home[0]) {
		return format_str("%s/scroll/templates", config_home);
	}
	const char *home = getenv("HOME");
	if (home && home[0]) {
		return format_str("%s/.config/scroll/templates", home);
	}
	return NULL;
}

char *space_template_path_for_name(const char *name) {
	if (!space_template_name_is_safe(name)) {
		return NULL;
	}
	char *dir = space_template_dir();
	if (!dir) {
		return NULL;
	}
	char *path = format_str("%s/%s.json", dir, name);
	free(dir);
	return path;
}

// --- filesystem I/O helpers ------------------------------------------

static bool mkdir_p(const char *path, char **out_error) {
	char *copy = strdup(path);
	size_t len = strlen(copy);
	bool ok = true;
	for (size_t i = 1; i < len && ok; ++i) {
		if (copy[i] != '/') {
			continue;
		}
		copy[i] = '\0';
		if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
			if (out_error) {
				*out_error = format_str("mkdir %s: %s", copy, strerror(errno));
			}
			ok = false;
		}
		copy[i] = '/';
	}
	if (ok && mkdir(copy, 0755) != 0 && errno != EEXIST) {
		if (out_error) {
			*out_error = format_str("mkdir %s: %s", copy, strerror(errno));
		}
		ok = false;
	}
	free(copy);
	return ok;
}

// Writes `contents` to `path` atomically: a same-directory temp file is
// written, fsync'd, and rename(2)'d over the target. On any failure the
// target is left untouched and the temp file is removed.
static bool atomic_write_file(const char *path, const char *contents, char **out_error) {
	char *tmp_path = format_str("%s.XXXXXX", path);
	int fd = mkstemp(tmp_path);
	if (fd < 0) {
		if (out_error) {
			*out_error = format_str("mkstemp %s: %s", tmp_path, strerror(errno));
		}
		free(tmp_path);
		return false;
	}
	fchmod(fd, 0644);

	size_t len = strlen(contents);
	size_t written = 0;
	bool ok = true;
	while (written < len) {
		ssize_t n = write(fd, contents + written, len - written);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (out_error) {
				*out_error = format_str("write %s: %s", tmp_path, strerror(errno));
			}
			ok = false;
			break;
		}
		written += (size_t)n;
	}
	if (ok && fsync(fd) != 0) {
		if (out_error) {
			*out_error = format_str("fsync %s: %s", tmp_path, strerror(errno));
		}
		ok = false;
	}
	close(fd);
	if (ok && rename(tmp_path, path) != 0) {
		if (out_error) {
			*out_error = format_str("rename %s -> %s: %s", tmp_path, path, strerror(errno));
		}
		ok = false;
	}
	if (!ok) {
		unlink(tmp_path);
	}
	free(tmp_path);
	return ok;
}

// --- in-memory load cache (name + mtime keyed; never scans at startup) --

struct template_cache_entry {
	char *name;
	struct sway_space_template *template; // cache-owned
	time_t mtime;
};

static list_t *g_template_cache = NULL;

static list_t *cache(void) {
	if (!g_template_cache) {
		g_template_cache = create_list();
	}
	return g_template_cache;
}

static struct template_cache_entry *cache_find(const char *name) {
	list_t *c = cache();
	for (int i = 0; i < c->length; ++i) {
		struct template_cache_entry *entry = c->items[i];
		if (strcmp(entry->name, name) == 0) {
			return entry;
		}
	}
	return NULL;
}

static void cache_entry_destroy(struct template_cache_entry *entry) {
	if (!entry) {
		return;
	}
	free(entry->name);
	space_template_destroy(entry->template);
	free(entry);
}

void space_template_cache_invalidate(const char *name) {
	if (!name) {
		return;
	}
	list_t *c = cache();
	for (int i = 0; i < c->length; ++i) {
		struct template_cache_entry *entry = c->items[i];
		if (strcmp(entry->name, name) == 0) {
			list_del(c, i);
			cache_entry_destroy(entry);
			return;
		}
	}
}

void space_template_cache_clear(void) {
	list_t *c = cache();
	for (int i = 0; i < c->length; ++i) {
		cache_entry_destroy(c->items[i]);
	}
	list_reset(c);
}

// --- persistence -------------------------------------------------------

struct sway_space_template_json_result space_template_save(const struct sway_space_template *template) {
	if (!template) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_IO, NULL, strdup("no template"));
	}
	struct sway_space_template_validation_result validation = space_template_validate(template);
	if (validation.error != SPACE_TEMPLATE_VALID) {
		char *json_path = validation.path ? format_str("$.%s", validation.path) : strdup("$");
		free(validation.path);
		return json_error(SPACE_TEMPLATE_JSON_ERROR_STRUCTURAL, json_path,
			strdup(space_template_validation_error_str(validation.error)));
	}
	if (!space_template_name_is_safe(template->name)) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_UNSAFE_NAME, strdup("$.name"), strdup(template->name));
	}

	char *dir = space_template_dir();
	if (!dir) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_IO, NULL,
			strdup("cannot resolve template directory (no XDG_CONFIG_HOME/HOME)"));
	}
	char *mkdir_error = NULL;
	if (!mkdir_p(dir, &mkdir_error)) {
		free(dir);
		return json_error(SPACE_TEMPLATE_JSON_ERROR_IO, NULL, mkdir_error);
	}
	char *path = format_str("%s/%s.json", dir, template->name);
	free(dir);

	char *json_str = space_template_to_json_string(template);
	char *write_error = NULL;
	bool ok = atomic_write_file(path, json_str, &write_error);
	free(json_str);
	free(path);
	if (!ok) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_IO, NULL, write_error);
	}

	// The on-disk file changed; force the next load() to re-read it rather
	// than serve a stale cached copy under a reused mtime.
	space_template_cache_invalidate(template->name);
	return json_ok(NULL);
}

struct sway_space_template_json_result space_template_load(const char *name) {
	if (!space_template_name_is_safe(name)) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_UNSAFE_NAME, strdup("$.name"), name ? strdup(name) : NULL);
	}
	char *path = space_template_path_for_name(name);
	if (!path) {
		return json_error(SPACE_TEMPLATE_JSON_ERROR_IO, NULL, strdup("cannot resolve template directory"));
	}

	struct stat st;
	if (stat(path, &st) != 0) {
		int saved_errno = errno;
		free(path);
		if (saved_errno == ENOENT) {
			return json_error(SPACE_TEMPLATE_JSON_ERROR_NOT_FOUND, NULL, strdup(name));
		}
		return json_error(SPACE_TEMPLATE_JSON_ERROR_IO, NULL, strdup(strerror(saved_errno)));
	}

	struct template_cache_entry *cached = cache_find(name);
	if (cached && cached->mtime == st.st_mtime) {
		free(path);
		return json_ok(space_template_copy(cached->template));
	}

	if (st.st_size < 0 || (size_t)st.st_size > SPACE_TEMPLATE_JSON_MAX_BYTES) {
		free(path);
		return json_error(SPACE_TEMPLATE_JSON_ERROR_TOO_LARGE, NULL,
			format_str("%s exceeds the %d byte limit", name, SPACE_TEMPLATE_JSON_MAX_BYTES));
	}

	FILE *f = fopen(path, "rb");
	if (!f) {
		int saved_errno = errno;
		free(path);
		return json_error(SPACE_TEMPLATE_JSON_ERROR_IO, NULL, strdup(strerror(saved_errno)));
	}
	free(path);

	char *contents = malloc((size_t)st.st_size + 1);
	size_t read_bytes = fread(contents, 1, (size_t)st.st_size, f);
	fclose(f);
	contents[read_bytes] = '\0';

	struct sway_space_template_json_result result = space_template_from_json_string(contents);
	free(contents);

	if (result.error != SPACE_TEMPLATE_JSON_OK) {
		// Leave any previously cached valid entry untouched (D-04): only
		// this call's result reports the failure.
		return result;
	}

	struct sway_space_template *cache_copy = space_template_copy(result.template);
	if (cached) {
		space_template_destroy(cached->template);
		cached->template = cache_copy;
		cached->mtime = st.st_mtime;
	} else {
		struct template_cache_entry *entry = calloc(1, sizeof(struct template_cache_entry));
		entry->name = strdup(name);
		entry->template = cache_copy;
		entry->mtime = st.st_mtime;
		list_add(cache(), entry);
	}

	return result; // .template is an independent copy owned by the caller.
}
