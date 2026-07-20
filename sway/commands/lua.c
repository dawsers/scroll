#include <libgen.h>
#include <unistd.h>
#include <wordexp.h>
#include "sway/commands.h"
#include "log.h"
#include "sway/lua.h"

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

	int top = lua_gettop(config->lua.state);

	struct sway_container *old_context_container = config->lua.context_container;
	if (config->handler_context.node_overridden) {
		config->lua.context_container = config->handler_context.container;
	} else {
		config->lua.context_container = NULL;
	}

	int err = luaL_loadfile(config->lua.state, expanded_path);
	if (err != LUA_OK) {
		const char *str = luaL_checkstring(config->lua.state, -1);
		if (str) {
			res = cmd_results_new(
					CMD_FAILURE, "Error %s loading lua script %s", str, expanded_path);
		} else {
			res = cmd_results_new(
					CMD_FAILURE, "Error %d loading lua script %s", err, expanded_path);
		}
		goto restore_context;
	}

	// Search if there is already a state for this script
	struct sway_lua_script *script = sway_lua_get_or_create_script(expanded_path);
	if (!script) {
		res = cmd_results_new(CMD_FAILURE, "Failed to allocate memory");
		goto restore_context;
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
		goto restore_context;
	}

	res = cmd_results_new(CMD_SUCCESS, NULL);

restore_context:
	config->lua.context_container = old_context_container;
	free(expanded_path);
	lua_settop(config->lua.state, top);
	return res;
}

struct cmd_results *cmd_lua_eval(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "lua_eval", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	int top = lua_gettop(config->lua.state);

	struct sway_container *old_context_container = config->lua.context_container;
	if (config->handler_context.node_overridden) {
		config->lua.context_container = config->handler_context.container;
	} else {
		config->lua.context_container = NULL;
	}

	struct cmd_results *res = NULL;

	int err = luaL_loadstring(config->lua.state, argv[0]);
	if (err != LUA_OK) {
		const char *str = luaL_checkstring(config->lua.state, -1);
		if (str) {
			res = cmd_results_new(CMD_FAILURE, "Error %s loading lua string", str);
		} else {
			res = cmd_results_new(CMD_FAILURE, "Error %d loading lua string", err);
		}
		goto restore_context;
	}

	// Search if there is already a state for this script
	struct sway_lua_script *script = sway_lua_get_or_create_script("");
	if (!script) {
		res = cmd_results_new(CMD_FAILURE, "Failed to allocate memory");
		goto restore_context;
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
			res = cmd_results_new(CMD_FAILURE, "Error %s executing lua string", str);
		} else {
			res = cmd_results_new(CMD_FAILURE, "Error %d executing lua string", err);
		}
		goto restore_context;
	}

	res = cmd_results_new(CMD_SUCCESS, NULL);

restore_context:
	config->lua.context_container = old_context_container;
	lua_settop(config->lua.state, top);
	return res;
}
