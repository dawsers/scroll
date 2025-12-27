#include "sway/commands.h"

static struct sway_lua_script *find_script(list_t *scripts, const char *name) {
	for (int i = 0; i < scripts->length; ++i) {
		struct sway_lua_script *script = scripts->items[i];
		if (strcmp(script->name, name) == 0) {
			return script;
		}
	}
	return NULL;
}

struct cmd_results *cmd_lua(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "lua", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	int err = luaL_loadfile(config->lua.state, argv[0]);
	if (err != LUA_OK) {
		return cmd_results_new(CMD_FAILURE, "Error %d loading lua script %s", err, argv[0]);
	}

	// Search if there is already a state for this script
	struct sway_lua_script *script = find_script(config->lua.scripts, argv[0]);
	if (!script) {
		script = malloc(sizeof(struct sway_lua_script));
		script->name = strdup(argv[0]);
		lua_createtable(config->lua.state, 0, 0);
		script->state = luaL_ref(config->lua.state, LUA_REGISTRYINDEX);
		list_add(config->lua.scripts, script);
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
			return cmd_results_new(CMD_FAILURE, "Error %s executing lua script %s", str, argv[0]);
		}
		return cmd_results_new(CMD_FAILURE, "Error %d executing lua script %s", err, argv[0]);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
