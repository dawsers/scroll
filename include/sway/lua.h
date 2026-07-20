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
	list_t *cbs_workspace_focus;
	list_t *cbs_ipc_view;
	list_t *cbs_ipc_workspace;
	list_t *cbs_jump_end;
	int command_data;
	list_t *cbs_command_end;
	struct sway_container *context_container;
};

int luaopen_scroll(lua_State *L);

// Takes whatever Lua object is top of the stack and assigns it to command_data
void lua_command_data_create();

struct sway_view;
struct sway_container;
struct sway_workspace;

void lua_execute_view_map_cbs(struct sway_view *view);
void lua_execute_view_unmap_cbs(struct sway_view *view);
void lua_execute_view_urgent_cbs(struct sway_view *view);
void lua_execute_view_focus_cbs(struct sway_view *view);
void lua_execute_view_float_cbs(struct sway_view *view);
void lua_execute_workspace_create_cbs(struct sway_workspace *workspace);
void lua_execute_workspace_focus_cbs(struct sway_workspace *workspace);
void lua_execute_ipc_view_cbs(struct sway_view *view, const char *change);
void lua_execute_ipc_workspace_cbs(struct sway_workspace *old_ws,
	struct sway_workspace *new_ws, const char *change);
void lua_execute_jump_end_cbs(struct sway_container *container);
struct sway_lua_script *sway_lua_get_or_create_script(const char *name);

struct json_object;
struct json_object *sway_lua_value_to_json(lua_State *L, int i);
struct json_object *sway_lua_table_to_json(lua_State *L, int index);
void sway_lua_push_json_to_lua(lua_State *L, struct json_object *obj);

#endif
