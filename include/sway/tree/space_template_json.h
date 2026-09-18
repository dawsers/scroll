#ifndef _SWAY_SPACE_TEMPLATE_JSON_H
#define _SWAY_SPACE_TEMPLATE_JSON_H

#include <stdbool.h>
#include "sway/tree/space_template.h"

// Maximum bytes accepted for a single template JSON file/document. Bounds
// worst-case parse time/allocation for an untrusted file; well above any
// realistic hand-authored or saved template.
#define SPACE_TEMPLATE_JSON_MAX_BYTES (1 * 1024 * 1024)

// Maximum length of a single view_hint regex string, and of a template name.
#define SPACE_TEMPLATE_JSON_MAX_REGEX_LEN 256
#define SPACE_TEMPLATE_JSON_MAX_NAME_LEN 100

enum sway_space_template_json_error {
	SPACE_TEMPLATE_JSON_OK = 0,
	SPACE_TEMPLATE_JSON_ERROR_PARSE, // malformed JSON syntax
	SPACE_TEMPLATE_JSON_ERROR_ROOT_TYPE, // document root is not an object
	SPACE_TEMPLATE_JSON_ERROR_MISSING_FIELD,
	SPACE_TEMPLATE_JSON_ERROR_WRONG_TYPE,
	SPACE_TEMPLATE_JSON_ERROR_UNSUPPORTED_VERSION,
	SPACE_TEMPLATE_JSON_ERROR_INVALID_ENUM,
	SPACE_TEMPLATE_JSON_ERROR_INVALID_NUMBER, // non-finite / out of range
	SPACE_TEMPLATE_JSON_ERROR_INVALID_REGEX, // PCRE2 failed to compile a hint
	SPACE_TEMPLATE_JSON_ERROR_TOO_LARGE, // document/regex exceeds a size limit
	SPACE_TEMPLATE_JSON_ERROR_STRUCTURAL, // failed space_template_validate() (see .path/.detail)
	SPACE_TEMPLATE_JSON_ERROR_UNSAFE_NAME, // name is not a safe filename component
	SPACE_TEMPLATE_JSON_ERROR_NOT_FOUND, // named template file does not exist
	SPACE_TEMPLATE_JSON_ERROR_IO, // open/read/write/rename/mkdir failure
};

// Result of a parse, save, or load operation.
//
// `path` is a JSON-Pointer-flavored locator (e.g.
// "$.tiling[0].children[1].view_hint.app_id") pointing at the first
// offending field, in stable document order; NULL when not applicable.
// `detail` is an optional human-readable elaboration (errno strerror(),
// PCRE2's compile error message, the underlying G1 validation error string);
// may be NULL. Both are heap-allocated; caller owns them via
// space_template_json_result_destroy().
//
// `template` is non-NULL only when error == SPACE_TEMPLATE_JSON_OK and this
// was a parse/load call (unused/NULL for space_template_save()). Ownership
// of `template` is independent of the result struct: destroy_result() never
// frees it. The caller must space_template_destroy() it separately.
struct sway_space_template_json_result {
	enum sway_space_template_json_error error;
	char *path;
	char *detail;
	struct sway_space_template *template;
};

const char *space_template_json_error_str(enum sway_space_template_json_error error);

// Frees `result->path` and `result->detail` and resets them to NULL. Does
// NOT touch `result->template` -- the caller owns that independently.
void space_template_json_result_destroy(struct sway_space_template_json_result *result);

// True iff `name` is safe to use as a template file's base name: non-empty,
// bounded length, contains only [A-Za-z0-9_-], and is therefore never ".",
// "..", nor able to contain a path separator. This is the only gate between
// a template name (user input, or the "name" field of a parsed JSON
// document) and a filesystem path.
bool space_template_name_is_safe(const char *name);

// Directory templates are stored under: $XDG_CONFIG_HOME/scroll/templates,
// falling back to $HOME/.config/scroll/templates when XDG_CONFIG_HOME is
// unset/empty. Caller must free(). Returns NULL if neither environment
// variable is usable.
char *space_template_dir(void);

// Returns "<space_template_dir()>/<name>.json" for a name that passes
// space_template_name_is_safe(). Caller must free(). Returns NULL if `name`
// is unsafe or the directory cannot be resolved.
char *space_template_path_for_name(const char *name);

// --- serialization (pure, no filesystem I/O) --------------------------

// Serializes an already-structurally-valid template
// (space_template_validate(template).error == SPACE_TEMPLATE_VALID) to a
// json_object tree. Caller owns the result and must json_object_put() it.
// Does not itself re-validate; callers that cannot guarantee validity should
// call space_template_validate() first.
struct json_object *space_template_to_json(const struct sway_space_template *template);

// Convenience wrapper: space_template_to_json() rendered as a
// pretty-printed, heap-allocated string. Caller must free().
char *space_template_to_json_string(const struct sway_space_template *template);

// Parses and fully validates a JSON document already loaded into a
// json_object (does not take ownership of `root`). On success, `.template`
// is set and `.error == SPACE_TEMPLATE_JSON_OK`. On any failure -- malformed
// types, unsupported version, invalid enum/number/regex, or a structural
// violation caught by space_template_validate() -- `.template` is NULL, no
// partially-built tree is leaked, and `.error`/`.path`/`.detail` describe
// the first offending field.
struct sway_space_template_json_result space_template_from_json(struct json_object *root);

// Convenience wrapper: tokenizes `text` (rejecting documents over
// SPACE_TEMPLATE_JSON_MAX_BYTES) and calls space_template_from_json().
struct sway_space_template_json_result space_template_from_json_string(const char *text);

// --- persistence (filesystem I/O under space_template_dir()) ----------

// Validates `template`, serializes it, creates the template directory if
// missing, and atomically replaces
// "<space_template_dir()>/<template->name>.json" via a same-directory
// temp file + rename. On any failure (invalid template, unsafe name, I/O
// error) the previous file, if any, is left byte-for-byte untouched.
// `.template` is unused (NULL) in the result; check `.error` for success.
struct sway_space_template_json_result space_template_save(const struct sway_space_template *template);

// Loads the named template. Consults and refreshes a small in-memory cache
// keyed by name + file mtime, so repeated loads of an unchanged file do not
// re-parse it. Returns SPACE_TEMPLATE_JSON_ERROR_NOT_FOUND if the file does
// not exist and SPACE_TEMPLATE_JSON_ERROR_UNSAFE_NAME if `name` fails
// space_template_name_is_safe(). On a parse/validation failure the cache is
// left exactly as it was: a previously cached valid entry for this name (if
// any) is not evicted or corrupted -- only this call's result reports the
// failure. On success, `.template` is a fresh, independently owned deep
// copy; the cache keeps its own.
struct sway_space_template_json_result space_template_load(const char *name);

// Drops `name` from the in-memory cache, if present. Never touches files.
void space_template_cache_invalidate(const char *name);

// Drops every entry from the in-memory cache. Never touches files.
void space_template_cache_clear(void);

#endif // _SWAY_SPACE_TEMPLATE_JSON_H
