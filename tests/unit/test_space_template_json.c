// Focused parser/persistence tests for the space-template JSON schema and
// XDG persistence (issue #2, G2). No compositor/Wayland/wlroots dependency:
// links only common list/string helpers plus json-c and pcre2.

#include <json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sway/tree/space_template.h"
#include "sway/tree/space_template_json.h"

static int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

static struct sway_space_template *build_valid_template(void) {
	struct sway_space_template *template = space_template_create("dev");
	template->scroller.mode = SPACE_TEMPLATE_SCROLLER_HORIZONTAL;
	template->scroller.insert = SPACE_TEMPLATE_INSERT_AFTER;
	template->scroller.fit = SPACE_TEMPLATE_FIT_FITSPLIT;
	template->scroller.focus = true;
	template->scroller.reorder = true;

	struct sway_space_template_node *root =
		space_template_node_create_inner(SPACE_TEMPLATE_LAYOUT_HORIZONTAL);
	space_template_node_set_tiling_geometry(root, 1.0, 1.0);

	struct sway_space_template_node *editor = space_template_node_create_leaf("editor");
	space_template_node_set_tiling_geometry(editor, 0.6, 1.0);
	space_template_node_set_hints(editor, "code", NULL, "^untitled");
	space_template_node_add_child(root, editor);

	struct sway_space_template_node *terminal = space_template_node_create_leaf("terminal");
	space_template_node_set_tiling_geometry(terminal, 0.4, 1.0);
	space_template_node_add_child(root, terminal);

	space_template_add_tiling(template, root);

	struct sway_space_template_node *popup = space_template_node_create_leaf("popup");
	space_template_node_set_floating_geometry(popup, 0.3, 0.3, 0.4, 0.4);
	space_template_add_floating(template, popup);

	space_template_set_focused_slot(template, "editor");
	return template;
}

static void test_json_round_trip_in_memory(void) {
	struct sway_space_template *template = build_valid_template();
	CHECK(space_template_validate(template).error == SPACE_TEMPLATE_VALID);

	char *json_str = space_template_to_json_string(template);
	CHECK(json_str != NULL);

	struct sway_space_template_json_result result = space_template_from_json_string(json_str);
	if (result.error != SPACE_TEMPLATE_JSON_OK) {
		fprintf(stderr, "FAIL round-trip parse: %s at %s (%s)\n",
			space_template_json_error_str(result.error),
			result.path ? result.path : "(none)", result.detail ? result.detail : "");
		failures++;
	} else {
		struct sway_space_template *parsed = result.template;
		CHECK(parsed->version == template->version);
		CHECK(strcmp(parsed->name, "dev") == 0);
		CHECK(parsed->scroller.mode == SPACE_TEMPLATE_SCROLLER_HORIZONTAL);
		CHECK(parsed->scroller.insert == SPACE_TEMPLATE_INSERT_AFTER);
		CHECK(parsed->scroller.fit == SPACE_TEMPLATE_FIT_FITSPLIT);
		CHECK(parsed->scroller.focus == true);
		CHECK(parsed->scroller.reorder == true);
		CHECK(parsed->tiling->length == 1);
		struct sway_space_template_node *root = parsed->tiling->items[0];
		CHECK(root->children->length == 2);
		struct sway_space_template_node *editor = root->children->items[0];
		CHECK(strcmp(editor->slot, "editor") == 0);
		CHECK(editor->hint_app_id && strcmp(editor->hint_app_id, "code") == 0);
		CHECK(editor->hint_class == NULL);
		CHECK(editor->hint_title && strcmp(editor->hint_title, "^untitled") == 0);
		CHECK(parsed->floating->length == 1);
		struct sway_space_template_node *popup = parsed->floating->items[0];
		CHECK(popup->width > 0.39 && popup->width < 0.41);
		CHECK(strcmp(parsed->focused_slot, "editor") == 0);
		space_template_destroy(parsed);
	}
	space_template_json_result_destroy(&result);
	free(json_str);
	space_template_destroy(template);
}

static void test_unknown_keys_ignored(void) {
	const char *json_str =
		"{\"version\":1,\"name\":\"x\",\"totally_unknown\":42,"
		"\"scroller\":{\"mode\":\"horizontal\",\"nonsense\":true},"
		"\"tiling\":[{\"slot\":\"a\",\"size\":{\"width_fraction\":1.0,\"height_fraction\":1.0},\"extra\":1}],"
		"\"floating\":[]}";
	struct sway_space_template_json_result result = space_template_from_json_string(json_str);
	if (result.error != SPACE_TEMPLATE_JSON_OK) {
		fprintf(stderr, "FAIL unknown-keys: %s at %s\n",
			space_template_json_error_str(result.error), result.path ? result.path : "(none)");
		failures++;
	} else {
		space_template_destroy(result.template);
	}
	space_template_json_result_destroy(&result);
}

static void test_unsupported_version_rejected(void) {
	const char *json_str = "{\"version\":2,\"name\":\"x\",\"tiling\":[],\"floating\":[]}";
	struct sway_space_template_json_result result = space_template_from_json_string(json_str);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_UNSUPPORTED_VERSION);
	CHECK(result.path && strcmp(result.path, "$.version") == 0);
	space_template_json_result_destroy(&result);
}

static void test_unsafe_name_rejected(void) {
	CHECK(!space_template_name_is_safe(NULL));
	CHECK(!space_template_name_is_safe(""));
	CHECK(!space_template_name_is_safe("."));
	CHECK(!space_template_name_is_safe(".."));
	CHECK(!space_template_name_is_safe("../../etc/passwd"));
	CHECK(!space_template_name_is_safe("a/b"));
	CHECK(!space_template_name_is_safe(".hidden"));
	CHECK(space_template_name_is_safe("dev"));
	CHECK(space_template_name_is_safe("my-template_v2"));
	CHECK(space_template_path_for_name("../../etc/passwd") == NULL);

	const char *json_str = "{\"version\":1,\"name\":\"../evil\",\"tiling\":[],\"floating\":[]}";
	struct sway_space_template_json_result result = space_template_from_json_string(json_str);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_UNSAFE_NAME);
	space_template_json_result_destroy(&result);
}

static void test_invalid_enum_rejected(void) {
	const char *json_str =
		"{\"version\":1,\"name\":\"x\",\"scroller\":{\"mode\":\"sideways\"},"
		"\"tiling\":[],\"floating\":[]}";
	struct sway_space_template_json_result result = space_template_from_json_string(json_str);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_INVALID_ENUM);
	CHECK(result.path && strcmp(result.path, "$.scroller.mode") == 0);
	space_template_json_result_destroy(&result);
}

static void test_invalid_regex_rejected(void) {
	const char *json_str =
		"{\"version\":1,\"name\":\"x\",\"tiling\":["
		"{\"slot\":\"a\",\"size\":{\"width_fraction\":1.0,\"height_fraction\":1.0},"
		"\"view_hint\":{\"app_id\":\"(unclosed\"}}],\"floating\":[]}";
	struct sway_space_template_json_result result = space_template_from_json_string(json_str);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_INVALID_REGEX);
	CHECK(result.path && strcmp(result.path, "$.tiling[0].view_hint.app_id") == 0);
	CHECK(result.detail != NULL); // PCRE2's compile error message
	space_template_json_result_destroy(&result);
}

static void test_structural_errors_delegate_to_g1(void) {
	// Duplicate slots -> G1's DUPLICATE_SLOT, surfaced as a JSON-path error.
	const char *dup =
		"{\"version\":1,\"name\":\"x\",\"tiling\":["
		"{\"slot\":\"a\",\"size\":{\"width_fraction\":0.5,\"height_fraction\":1.0}},"
		"{\"slot\":\"a\",\"size\":{\"width_fraction\":0.5,\"height_fraction\":1.0}}],"
		"\"floating\":[]}";
	struct sway_space_template_json_result result = space_template_from_json_string(dup);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_STRUCTURAL);
	CHECK(result.path && strstr(result.path, "tiling[1]") != NULL);
	space_template_json_result_destroy(&result);

	// Empty slot -> G1's EMPTY_SLOT.
	const char *empty_slot =
		"{\"version\":1,\"name\":\"x\",\"tiling\":["
		"{\"slot\":\"\",\"size\":{\"width_fraction\":1.0,\"height_fraction\":1.0}}],\"floating\":[]}";
	result = space_template_from_json_string(empty_slot);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_STRUCTURAL);
	space_template_json_result_destroy(&result);

	// focused_slot naming a nonexistent slot -> G1's INVALID_FOCUSED_SLOT.
	const char *bad_focus =
		"{\"version\":1,\"name\":\"x\",\"tiling\":["
		"{\"slot\":\"a\",\"size\":{\"width_fraction\":1.0,\"height_fraction\":1.0}}],"
		"\"floating\":[],\"focused_slot\":\"nope\"}";
	result = space_template_from_json_string(bad_focus);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_STRUCTURAL);
	space_template_json_result_destroy(&result);

	// Floating node with non-positive width -> G1's INVALID_GEOMETRY.
	const char *bad_floating =
		"{\"version\":1,\"name\":\"x\",\"tiling\":[],\"floating\":["
		"{\"slot\":\"a\",\"x\":0,\"y\":0,\"width\":0,\"height\":0.4}]}";
	result = space_template_from_json_string(bad_floating);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_STRUCTURAL);
	space_template_json_result_destroy(&result);
}

static void test_missing_required_fields(void) {
	// Tiling leaf without "size".
	const char *no_size = "{\"version\":1,\"name\":\"x\",\"tiling\":[{\"slot\":\"a\"}],\"floating\":[]}";
	struct sway_space_template_json_result result = space_template_from_json_string(no_size);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_MISSING_FIELD);
	CHECK(result.path && strcmp(result.path, "$.tiling[0].size") == 0);
	space_template_json_result_destroy(&result);

	// Floating leaf missing "height".
	const char *no_height =
		"{\"version\":1,\"name\":\"x\",\"tiling\":[],\"floating\":["
		"{\"slot\":\"a\",\"x\":0.1,\"y\":0.1,\"width\":0.3}]}";
	result = space_template_from_json_string(no_height);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_MISSING_FIELD);
	CHECK(result.path && strcmp(result.path, "$.floating[0].height") == 0);
	space_template_json_result_destroy(&result);

	// Node with neither "children" nor "slot".
	const char *neither = "{\"version\":1,\"name\":\"x\",\"tiling\":[{}],\"floating\":[]}";
	result = space_template_from_json_string(neither);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_MISSING_FIELD);
	space_template_json_result_destroy(&result);
}

static void test_malformed_document_rejected(void) {
	struct sway_space_template_json_result result = space_template_from_json_string("{not json");
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_PARSE);
	space_template_json_result_destroy(&result);

	result = space_template_from_json_string("[1,2,3]");
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_ROOT_TYPE);
	space_template_json_result_destroy(&result);

	result = space_template_from_json_string(NULL);
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_PARSE);
	space_template_json_result_destroy(&result);
}

// --- persistence (uses a private temp dir via XDG_CONFIG_HOME; never
// touches the real user's config) ---------------------------------------

static char *make_temp_config_home(void) {
	char *tmpl = strdup("/tmp/space_template_test_XXXXXX");
	char *dir = mkdtemp(tmpl);
	if (!dir) {
		fprintf(stderr, "FATAL: mkdtemp failed\n");
		exit(1);
	}
	return dir; // same pointer as tmpl, now filled in
}

static void test_save_load_round_trip(void) {
	char *config_home = make_temp_config_home();
	setenv("XDG_CONFIG_HOME", config_home, 1);
	space_template_cache_clear();

	struct sway_space_template *template = build_valid_template();
	struct sway_space_template_json_result save_result = space_template_save(template);
	if (save_result.error != SPACE_TEMPLATE_JSON_OK) {
		fprintf(stderr, "FAIL save: %s (%s)\n", space_template_json_error_str(save_result.error),
			save_result.detail ? save_result.detail : "");
		failures++;
	}
	space_template_json_result_destroy(&save_result);

	char *path = space_template_path_for_name("dev");
	CHECK(access(path, R_OK) == 0);
	free(path);

	struct sway_space_template_json_result load_result = space_template_load("dev");
	if (load_result.error != SPACE_TEMPLATE_JSON_OK) {
		fprintf(stderr, "FAIL load: %s\n", space_template_json_error_str(load_result.error));
		failures++;
	} else {
		CHECK(strcmp(load_result.template->name, "dev") == 0);
		CHECK(load_result.template->tiling->length == 1);
		space_template_destroy(load_result.template);
	}
	space_template_json_result_destroy(&load_result);

	// A second load should be served from the mtime-keyed cache but still
	// return an independently owned, structurally identical copy.
	struct sway_space_template_json_result load_again = space_template_load("dev");
	CHECK(load_again.error == SPACE_TEMPLATE_JSON_OK);
	CHECK(load_again.template != NULL);
	space_template_destroy(load_again.template);
	space_template_json_result_destroy(&load_again);

	space_template_destroy(template);
	space_template_cache_clear();
	free(config_home);
}

static void test_load_missing_template(void) {
	char *config_home = make_temp_config_home();
	setenv("XDG_CONFIG_HOME", config_home, 1);
	space_template_cache_clear();

	struct sway_space_template_json_result result = space_template_load("does-not-exist");
	CHECK(result.error == SPACE_TEMPLATE_JSON_ERROR_NOT_FOUND);
	space_template_json_result_destroy(&result);
	free(config_home);
}

static char *read_file(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *contents = malloc(size + 1);
	size_t n = fread(contents, 1, size, f);
	contents[n] = '\0';
	fclose(f);
	return contents;
}

static void test_save_failure_preserves_prior_file(void) {
	char *config_home = make_temp_config_home();
	setenv("XDG_CONFIG_HOME", config_home, 1);
	space_template_cache_clear();

	struct sway_space_template *good = build_valid_template();
	struct sway_space_template_json_result save_result = space_template_save(good);
	CHECK(save_result.error == SPACE_TEMPLATE_JSON_OK);
	space_template_json_result_destroy(&save_result);

	char *path = space_template_path_for_name("dev");
	char *before = read_file(path);
	CHECK(before != NULL);

	// An invalid template (empty children on an inner node) with the same
	// name must fail validation before any write happens.
	struct sway_space_template *bad = space_template_create("dev");
	struct sway_space_template_node *empty_inner =
		space_template_node_create_inner(SPACE_TEMPLATE_LAYOUT_HORIZONTAL);
	space_template_add_tiling(bad, empty_inner);
	struct sway_space_template_json_result bad_result = space_template_save(bad);
	CHECK(bad_result.error == SPACE_TEMPLATE_JSON_ERROR_STRUCTURAL);
	space_template_json_result_destroy(&bad_result);

	char *after = read_file(path);
	CHECK(after != NULL && before != NULL && strcmp(before, after) == 0);

	free(before);
	free(after);
	free(path);
	space_template_destroy(good);
	space_template_destroy(bad);
	space_template_cache_clear();
	free(config_home);
}

static void test_corrupt_file_reported_not_crashed(void) {
	char *config_home = make_temp_config_home();
	setenv("XDG_CONFIG_HOME", config_home, 1);
	space_template_cache_clear();

	struct sway_space_template *template = build_valid_template();
	struct sway_space_template_json_result save_result = space_template_save(template);
	CHECK(save_result.error == SPACE_TEMPLATE_JSON_OK);
	space_template_json_result_destroy(&save_result);

	char *path = space_template_path_for_name("dev");
	FILE *f = fopen(path, "wb");
	fputs("{ this is not valid json", f);
	fclose(f);

	struct sway_space_template_json_result load_result = space_template_load("dev");
	CHECK(load_result.error == SPACE_TEMPLATE_JSON_ERROR_PARSE);
	space_template_json_result_destroy(&load_result);

	free(path);
	space_template_destroy(template);
	space_template_cache_clear();
	free(config_home);
}

int main(void) {
	test_json_round_trip_in_memory();
	test_unknown_keys_ignored();
	test_unsupported_version_rejected();
	test_unsafe_name_rejected();
	test_invalid_enum_rejected();
	test_invalid_regex_rejected();
	test_structural_errors_delegate_to_g1();
	test_missing_required_fields();
	test_malformed_document_rejected();
	test_save_load_round_trip();
	test_load_missing_template();
	test_save_failure_preserves_prior_file();
	test_corrupt_file_reported_not_crashed();

	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("all space_template_json checks passed\n");
	return 0;
}
