// `space_template save|apply` command. Both subcommands are thin wrappers:
// `save` calls space_template_capture() + space_template_save(); `apply`
// builds the same request object the IPC apply_space_template message
// takes and hands it to the shared space_template_ipc_apply(), so the two
// public surfaces (command line, IPC) share one implementation and one set
// of error messages.
#include <json.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/space_template_ipc.h"
#include "sway/tree/space_template_capture.h"
#include "sway/tree/space_template_json.h"

static const char expected_syntax[] =
	"Expected 'space_template save <name> [--with-hints]' or "
	"'space_template apply <name> [mappings_json]'";

static struct cmd_results *cmd_space_template_save(int argc, char **argv) {
	if (argc < 1 || argc > 2) {
		return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
	}
	bool with_hints = false;
	if (argc == 2) {
		if (strcasecmp(argv[1], "--with-hints") != 0) {
			return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
		}
		with_hints = true;
	}

	struct sway_workspace *workspace = config->handler_context.workspace;
	if (!workspace) {
		return cmd_results_new(CMD_INVALID,
			"Can't run this command when there is no active workspace.");
	}

	struct sway_space_template *template = space_template_capture(workspace, argv[0], with_hints);
	struct sway_space_template_json_result result = space_template_save(template);
	space_template_destroy(template);

	if (result.error != SPACE_TEMPLATE_JSON_OK) {
		struct cmd_results *error = cmd_results_new(CMD_INVALID, "space_template save failed: %s%s%s",
			space_template_json_error_str(result.error),
			result.detail ? ": " : "", result.detail ? result.detail : "");
		space_template_json_result_destroy(&result);
		return error;
	}
	space_template_json_result_destroy(&result);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *cmd_space_template_apply(int argc, char **argv) {
	if (argc < 1 || argc > 2) {
		return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
	}

	struct sway_workspace *workspace = config->handler_context.workspace;
	if (!workspace) {
		return cmd_results_new(CMD_INVALID,
			"Can't run this command when there is no active workspace.");
	}

	struct json_object *mappings = NULL;
	if (argc == 2) {
		enum json_tokener_error jerr = json_tokener_success;
		mappings = json_tokener_parse_verbose(argv[1], &jerr);
		if (!mappings || json_object_get_type(mappings) != json_type_array) {
			if (mappings) {
				json_object_put(mappings);
			}
			return cmd_results_new(CMD_INVALID,
				"mappings must be a JSON array, e.g. '[{\"slot\":\"editor\",\"con_id\":3}]'");
		}
	} else {
		mappings = json_object_new_array();
	}

	struct json_object *request = json_object_new_object();
	json_object_object_add(request, "name", json_object_new_string(argv[0]));
	json_object_object_add(request, "mappings", mappings); // request now owns mappings

	struct json_object *response = space_template_ipc_apply(request, workspace);
	json_object_put(request);

	struct json_object *success_json = NULL;
	bool success = json_object_object_get_ex(response, "success", &success_json) &&
		json_object_get_boolean(success_json);

	struct cmd_results *result;
	if (success) {
		result = cmd_results_new(CMD_SUCCESS, NULL);
	} else {
		struct json_object *error_json = NULL;
		const char *error = json_object_object_get_ex(response, "error", &error_json) ?
			json_object_get_string(error_json) : "apply failed";
		result = cmd_results_new(CMD_INVALID, "%s", error);
	}
	json_object_put(response);
	return result;
}

struct cmd_results *cmd_space_template(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "space_template", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	if (strcasecmp(argv[0], "save") == 0) {
		return cmd_space_template_save(argc - 1, argv + 1);
	} else if (strcasecmp(argv[0], "apply") == 0) {
		return cmd_space_template_apply(argc - 1, argv + 1);
	}
	return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
}
