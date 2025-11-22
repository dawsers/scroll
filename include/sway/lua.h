#ifndef _SWAY_LUA_H
#define _SWAY_LUA_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "list.h"

struct sway_lua_script {
	char *name;
	int state;
};

struct sway_lua_closure {
	int cb_function;
	int cb_data;
};

struct sway_lua {
	lua_State *state;
	list_t *scripts;
	list_t *cbs_view_map;
	list_t *cbs_view_unmap;
	list_t *cbs_view_urgent;
	list_t *cbs_view_focus;
	list_t *cbs_view_float;
	list_t *cbs_workspace_create;
};

int luaopen_scroll(lua_State *L);

#endif
