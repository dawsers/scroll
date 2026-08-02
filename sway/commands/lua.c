#include <libgen.h>
#include <unistd.h>
#include <wordexp.h>
#include "sway/commands.h"
#include "sway/log.h"

static struct sway_lua_script *find_or_create_script(list_t *scripts, const char *name) {
	for (int i = 0; i < scripts->length; ++i) {
		struct sway_lua_script *script = scripts->items[i];
		if (strcmp(script->name, name) == 0) {
			return script;
		}
	}
	struct sway_lua_script *script = malloc(sizeof(struct sway_lua_script));
	if (!script) {
		return NULL;
	}
	script->name = strdup(name);
	if (!script->name) {
		free(script);
		return NULL;
	}
	lua_createtable(config->lua.state, 0, 0);
	script->state = luaL_ref(config->lua.state, LUA_REGISTRYINDEX);
	list_add(config->lua.scripts, script);

	return script;
}

struct cmd_results *cmd_lua(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "lua", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	char *wd = NULL;
	char *config_dir = NULL;
	if (config->reading && config->current_config_path) {
		wd = getcwd(NULL, 0);
		char *conf = strdup(config->current_config_path);
		if (conf) {
			config_dir = strdup(dirname(conf));
			free(conf);
		}
		if (config_dir && chdir(config_dir) < 0) {
			sway_log(SWAY_ERROR, "failed to change working directory to config dir");
			free(config_dir);
			free(wd);
			return cmd_results_new(CMD_FAILURE, "Failed to change working directory to config dir");
		}
	}

	char *expanded_path = NULL;
	wordexp_t p;
	int err_we = wordexp(argv[0], &p, 0);

	if (wd && chdir(wd) < 0) {
		sway_log(SWAY_ERROR, "failed to restore working directory");
	}
	free(wd);

	if (err_we != 0) {
		free(config_dir);
		return cmd_results_new(CMD_FAILURE, "Error expanding path %s (code %d)", argv[0], err_we);
	}

	struct cmd_results *res = NULL;

	if (p.we_wordc == 0) {
		res = cmd_results_new(CMD_FAILURE, "Path expanded to nothing: %s", argv[0]);
		goto cleanup;
	} else if (p.we_wordc > 1) {
		res = cmd_results_new(CMD_FAILURE, "Path expanded to multiple files: %s", argv[0]);
		goto cleanup;
	}

	expanded_path = strdup(p.we_wordv[0]);
	if (!expanded_path) {
		res = cmd_results_new(CMD_FAILURE, "Failed to allocate memory");
		goto cleanup;
	}

	if (config->reading && expanded_path[0] != '/' && config_dir) {
		char *real_path = malloc(strlen(config_dir) + strlen(expanded_path) + 2);
		if (real_path) {
			sprintf(real_path, "%s/%s", config_dir, expanded_path);
			free(expanded_path);
			expanded_path = real_path;
		}
	}

cleanup:
	wordfree(&p);
	free(config_dir);

	if (res) {
		return res;
	}

	int err = luaL_loadfile(config->lua.state, expanded_path);
	if (err != LUA_OK) {
		struct cmd_results *res = cmd_results_new(CMD_FAILURE, "Error %d loading lua script %s", err, expanded_path);
		free(expanded_path);
		return res;
	}

	// Search if there is already a state for this script
	struct sway_lua_script *script = find_or_create_script(config->lua.scripts, expanded_path);
	if (!script) {
		res = cmd_results_new(CMD_FAILURE, "Failed to allocate memory for sway_lua_script");
		goto finish;
	}

	// Create args table before running the script
	lua_createtable(config->lua.state, argc - 1, 0);
	for (int i = 1; i < argc; ++i) {
		lua_pushstring(config->lua.state, argv[i]);
		lua_rawseti(config->lua.state, -2, i);
	}
	lua_pushlightuserdata(config->lua.state, script);

	err = lua_pcall(config->lua.state, 2, LUA_MULTRET, 0);
	if (err != LUA_OK) {
		const char *str = luaL_checkstring(config->lua.state, -1);
		if (str) {
			res = cmd_results_new(CMD_FAILURE, "Error %s executing lua script %s", str, expanded_path);
		} else {
			res = cmd_results_new(CMD_FAILURE, "Error %d executing lua script %s", err, expanded_path);
		}
		goto finish;
	}
	res =  cmd_results_new(CMD_SUCCESS, NULL);

finish:
	free(expanded_path);
	return res;
}

struct cmd_results *cmd_lua_eval(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "lua_eval", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	struct cmd_results *res = NULL;

	// Search if there is already a state for this script
	struct sway_lua_script *script = find_or_create_script(config->lua.scripts, argv[0]);
	if (!script) {
		res = cmd_results_new(CMD_FAILURE, "Failed to allocate memory for sway_lua_script");
		goto cleanup;
	}

	int err = luaL_loadstring(config->lua.state, argv[1]);
	if (err != LUA_OK) {
		const char *str = luaL_checkstring(config->lua.state, -1);
		if (str) {
			res = cmd_results_new(CMD_FAILURE, "Error %s loading lua string", str);
		} else {
			res = cmd_results_new(CMD_FAILURE, "Error %d loading lua string", err);
		}
		goto cleanup;
	}

	// Create args table before running the script
	lua_createtable(config->lua.state, argc - 2, 0);
	for (int i = 2; i < argc; ++i) {
		lua_pushstring(config->lua.state, argv[i]);
		lua_rawseti(config->lua.state, -2, i - 1);
	}
	lua_pushlightuserdata(config->lua.state, script);

	err = lua_pcall(config->lua.state, 2, LUA_MULTRET, 0);
	if (err != LUA_OK) {
		const char *str = luaL_checkstring(config->lua.state, -1);
		if (str) {
			res = cmd_results_new(CMD_FAILURE, "Error %s executing lua script %s", str, argv[0]);
		} else {
			res = cmd_results_new(CMD_FAILURE, "Error %d executing lua script %s", err, argv[0]);
		}
		goto cleanup;
	}
	res =  cmd_results_new(CMD_SUCCESS, NULL);

cleanup:
	return res;
}
