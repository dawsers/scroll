#include <lua.h>
#include <lauxlib.h>
#include "log.h"
#include "sway/lua.h"
#include "sway/commands.h"
#include "sway/tree/view.h"
#include "sway/tree/container.h"
#include "sway/tree/workspace.h"
#include "sway/output.h"

#if 0
static void print_stack(lua_State *L) {
	int top = lua_gettop(L);
	sway_log(SWAY_DEBUG, "Printing Lua stack...");
	for (int i = 1; i <= top; ++i) {
		sway_log(SWAY_DEBUG, "%d\t%s", i, luaL_typename(L, i));
		switch (lua_type(L, i)) {
		case LUA_TNUMBER:
			sway_log(SWAY_DEBUG, "%g", lua_tonumber(L, i));
			break;
		case LUA_TSTRING:
			sway_log(SWAY_DEBUG, "%s", lua_tostring(L, i));
			break;
		case LUA_TBOOLEAN:
			sway_log(SWAY_DEBUG, "%s", lua_toboolean(L, i) ? "true" : "false");
			break;
		case LUA_TNIL:
			sway_log(SWAY_DEBUG, "%s", "nil");
			break;
		default:
			sway_log(SWAY_DEBUG, "%p", lua_topointer(L, i));
			break;
		}
	}
}
#endif

static const int STACK_MIN = 5;

static int scroll_log(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc < 1) {
		return 0;
	}
	const char *str = luaL_checkstring(L, -1);
	if (str) {
		sway_log(SWAY_DEBUG, "%s", str);
	}
	return 0;
}

static int scroll_state_get_value(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc < 2) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_lua_script *script = lua_touserdata(L, -2);
	const char *key = luaL_checkstring(L, -1);
	if (!script || !key) {
		lua_pushnil(L);
		return 1;
	}
	for (int i = 0; i < config->lua.scripts->length; ++i) {
		if (script == config->lua.scripts->items[i]) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, script->state);
			lua_getfield(L, -1, key);
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

static int scroll_state_set_value(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc < 3) {
		return 0;
	}
	struct sway_lua_script *script = lua_touserdata(L, -3);
	const char *key = luaL_checkstring(L, -2);
	if (!script || !key) {
		return 0;
	}
	for (int i = 0; i < config->lua.scripts->length; ++i) {
		if (script == config->lua.scripts->items[i]) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, script->state);
			lua_pushvalue(L, -2);
			lua_setfield(L, -2, key);
		}
	}
	return 0;
}

static bool find_view(struct sway_container *container, void *data) {
	struct sway_view *view = data;
	return container->view == view;
}

static bool find_container(struct sway_container *container, void *data) {
	struct sway_container *con = data;
	return container == con;
}

static bool find_workspace(struct sway_workspace *workspace, void *data) {
	struct sway_workspace *ws = data;
	return workspace == ws;
}

static int scroll_command_error(lua_State *L, const char *error) {
	lua_createtable(L, 1, 0);
	lua_pushstring(L, error);
	lua_rawseti(L, -2, 1);
	return 1;
}

// scroll.command(container|workspace|nil, command)
static int scroll_command(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc < 2) {
		return scroll_command_error(L, "Error: scroll_command() received a wrong number of parameters");
	}
	void *node = lua_isnil(L, 1) ? NULL : lua_touserdata(L, 1);
	struct sway_seat *seat = input_manager_current_seat();
	struct sway_container *container = node;
	if (container && container->node.type == N_CONTAINER) {
		struct sway_container *found = root_find_container(find_container, container);
		if (!found) {
			return scroll_command_error(L, "Error: scroll_command() received a container parameter that does not exist");
		}
		seat_set_raw_focus(seat, &container->node);
		seat = NULL;
	} else {
		container = NULL;
		struct sway_workspace *workspace = node;
		if (workspace && workspace->node.type == N_WORKSPACE) {
			struct sway_workspace *found = root_find_workspace(find_workspace, workspace);
			if (!found) {
				return scroll_command_error(L, "Error: scroll_command() received a workspace parameter that does not exist");
			}
			seat_set_raw_focus(seat, &workspace->node);
		} else if (node == NULL) {
			seat = NULL;
		} else {
			return scroll_command_error(L, "Error: scroll_command() received a parameter that is neither a container nor a workspace");
		}
	}
	const char *lua_cmd = luaL_checkstring(L, 2);
	char *cmd = strdup(lua_cmd);
	list_t *results = execute_command(cmd, seat, container);
	lua_checkstack(L, results->length + STACK_MIN);
	lua_createtable(L, results->length, 0);
	for (int i = 0; i < results->length; ++i) {
		struct cmd_results *result = results->items[i];
		if (result->error) {
			lua_pushstring(L, result->error);
		} else {
			lua_pushinteger(L, result->status);
		}
		lua_rawseti(L, -2, i + 1);
	}
	free(cmd);
	return 1;
}

static struct sway_node *get_focused_node() {
	struct sway_seat *seat = input_manager_current_seat();
	struct sway_node *node = seat_get_focus_inactive(seat, &root->node);
	return node;
}

static int scroll_focused_view(lua_State *L) {
	struct sway_node *node = get_focused_node();
	if (!node) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_container *container = node->type == N_CONTAINER ?
		node->sway_container : NULL;

	if (container && container->view) {
		lua_pushlightuserdata(L, container->view);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int scroll_focused_container(lua_State *L) {
	struct sway_node *node = get_focused_node();
	if (!node) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_container *container = node->type == N_CONTAINER ?
		node->sway_container : NULL;

	if (container) {
		lua_pushlightuserdata(L, container);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int scroll_focused_workspace(lua_State *L) {
	struct sway_node *node = get_focused_node();
	if (!node) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_workspace *workspace;
	if (node->type == N_WORKSPACE) {
		workspace = node->sway_workspace;
	} else if (node->type == N_CONTAINER) {
		workspace = node->sway_container->pending.workspace;
	} else {
		workspace = NULL;
	}

	if (workspace) {
		lua_pushlightuserdata(L, workspace);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static bool find_urgent(struct sway_container *container, void *data) {
	return (container->view && view_is_urgent(container->view));
}

static int scroll_urgent_view(lua_State *L) {
	struct sway_container *container = root_find_container(find_urgent, NULL);
	if (container && container->view) {
		lua_pushlightuserdata(L, container->view);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int scroll_view_mapped(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushboolean(L, 0);
		return 1;
	}
	struct sway_view *view = lua_touserdata(L, -1);
	if (view) {
		struct sway_container *found = root_find_container(find_view, view);
		if (found) {
			lua_pushboolean(L, 1);
			return 1;
		}
	}
	lua_pushboolean(L, 0);
	return 1;
}

static int scroll_view_get_container(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_view *view = lua_touserdata(L, -1);
	lua_pushlightuserdata(L, view ? view->container : NULL);
	return 1;
}

static int scroll_view_get_app_id(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_view *view = lua_touserdata(L, -1);
	if (!view) {
		lua_pushnil(L);
		return 1;
	}
	const char *app_id = view_get_app_id(view);
	lua_pushstring(L, app_id);
	return 1;
}

static int scroll_view_get_title(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_view *view = lua_touserdata(L, -1);
	if (!view) {
		lua_pushnil(L);
		return 1;
	}
	const char *app_id = view_get_title(view);
	lua_pushstring(L, app_id);
	return 1;
}

static int scroll_view_get_pid(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_view *view = lua_touserdata(L, -1);
	if (!view) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, view->pid);
	return 1;
}

static pid_t get_parent_pid(pid_t pid) {
	unsigned int v = 0;
	FILE *f;
	char buf[256];
	snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", (unsigned) pid);
	if (!(f = fopen(buf, "r")))
		return 0;
	fscanf(f, "%*u %*s %*c %u", &v);
	fclose(f);
	return (pid_t) v;
}

static bool find_pid(struct sway_container *container, void *data) {
	pid_t *pid = data;
	return (container->view && container->view->pid == *pid);
}

static int scroll_view_get_parent_view(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_view *view = lua_touserdata(L, -1);
	if (!view) {
		lua_pushnil(L);
		return 1;
	}
	pid_t pid = view->pid;
	struct sway_container *container = NULL;
	while (true) {
		pid = get_parent_pid(pid);
		if (pid == 0) {
			break;
		}
		// Search for a view with pid
		container = root_find_container(find_pid, &pid);
		if (container) {
			break;
		}
	};
	if (container && container->view) {
		lua_pushlightuserdata(L, container->view);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int scroll_view_get_urgent(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushboolean(L, 0);
		return 1;
	}
	struct sway_view *view = lua_touserdata(L, -1);
	if (!view) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushboolean(L, view_is_urgent(view));
	return 1;
}

static int scroll_view_set_urgent(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc < 2) {
		return 0;
	}
	bool urgent = lua_toboolean(L, 2);
	struct sway_view *view = lua_touserdata(L, 1);
	if (view) {
		view_set_urgent(view, urgent);
	}
	return 0;
}

static int scroll_view_get_shell(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_view *view = lua_touserdata(L, -1);
	if (!view) {
		lua_pushnil(L);
		return 1;
	}
	const char *shell = view_get_shell(view);
	lua_pushstring(L, shell);
	return 1;
}

static int scroll_view_get_tag(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_view *view = lua_touserdata(L, -1);
	if (!view) {
		lua_pushnil(L);
		return 1;
	}
	const char *tag = view_get_tag(view);
	if (tag) {
		lua_pushstring(L, tag);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int scroll_view_close(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		return 0;
	}
	struct sway_view *view = lua_touserdata(L, -1);
	if (view) {
		view_close(view);
	}
	return 0;
}

static int scroll_container_get_workspace(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlightuserdata(L, container->pending.workspace);
	return 1;
}

static int scroll_container_get_marks(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	lua_checkstack(L, container->marks->length + STACK_MIN);
	lua_createtable(L, container->marks->length, 0);
	for (int i = 0; i < container->marks->length; ++i) {
		char *mark = container->marks->items[i];
		lua_pushstring(L, mark);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static int scroll_container_get_floating(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushboolean(L, 0);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, container_is_floating(container));
	return 1;
}

static int scroll_container_get_opacity(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushnumber(L, container->alpha);
	return 1;
}

static int scroll_container_get_sticky(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushboolean(L, 0);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, container->is_sticky);
	return 1;
}

static int scroll_container_get_width_fraction(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnumber(L, 0.0);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushnumber(L, 0.0);
		return 1;
	}
	lua_pushnumber(L, container->width_fraction);
	return 1;
}

static int scroll_container_get_height_fraction(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnumber(L, 0.0);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushnumber(L, 0.0);
		return 1;
	}
	lua_pushnumber(L, container->height_fraction);
	return 1;
}

static int scroll_container_get_width(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnumber(L, 0.0);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushnumber(L, 0.0);
		return 1;
	}
	lua_pushnumber(L, container->pending.width);
	return 1;
}

static int scroll_container_get_height(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnumber(L, 0.0);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushnumber(L, 0.0);
		return 1;
	}
	lua_pushnumber(L, container->pending.height);
	return 1;
}

static int scroll_container_get_fullscreen_mode(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushstring(L, "none");
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushstring(L, "none");
		return 1;
	}
	switch (container->pending.fullscreen_mode) {
	case FULLSCREEN_NONE:
		lua_pushstring(L, "none");
		break;
	case FULLSCREEN_WORKSPACE:
		lua_pushstring(L, "workspace");
		break;
	case FULLSCREEN_GLOBAL:
		lua_pushstring(L, "global");
		break;
	}
	return 1;
}

static int scroll_container_get_fullscreen_app_mode(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushstring(L, "disabled");
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushstring(L, "disabled");
		return 1;
	}
	switch (container->pending.fullscreen_application) {
	case FULLSCREEN_DISABLED:
		lua_pushstring(L, "disabled");
		break;
	case FULLSCREEN_ENABLED:
		lua_pushstring(L, "enabled");
		break;
	}
	return 1;
}

static int scroll_container_get_fullscreen_view_mode(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushstring(L, "disabled");
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushstring(L, "disabled");
		return 1;
	}
	switch (container->pending.fullscreen_container) {
	case FULLSCREEN_DISABLED:
		lua_pushstring(L, "disabled");
		break;
	case FULLSCREEN_ENABLED:
		lua_pushstring(L, "enabled");
		break;
	}
	return 1;
}

static int scroll_container_get_fullscreen_layout_mode(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushstring(L, "disabled");
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushstring(L, "disabled");
		return 1;
	}
	switch (container->pending.fullscreen_layout) {
	case FULLSCREEN_DISABLED:
		lua_pushstring(L, "disabled");
		break;
	case FULLSCREEN_ENABLED:
		lua_pushstring(L, "enabled");
		break;
	}
	return 1;
}

static int scroll_container_get_parent(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER ||
		container->pending.parent == NULL) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlightuserdata(L, container->pending.parent);
	return 1;
}

static int scroll_container_get_children(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER ||
		container->pending.children == NULL) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	int len = container->pending.children->length;
	lua_checkstack(L, len + STACK_MIN);
	lua_createtable(L, len, 0);
	for (int i = 0; i < len; ++i) {
		struct sway_container *con = container->pending.children->items[i];
		lua_pushlightuserdata(L, con);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static int scroll_container_get_views(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	if (container->view) {
		lua_createtable(L, 1, 0);
		lua_pushlightuserdata(L, container->view);
		lua_rawseti(L, -2, 1);
	} else {
		int len = container->pending.children->length;
		lua_checkstack(L, len + STACK_MIN);
		lua_createtable(L, len, 0);
		for (int i = 0; i < len; ++i) {
			struct sway_container *con = container->pending.children->items[i];
			lua_pushlightuserdata(L, con->view);
			lua_rawseti(L, -2, i + 1);
		}
	}
	return 1;
}

static int scroll_container_get_id(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_container *container = lua_touserdata(L, -1);
	if (!container || container->node.type != N_CONTAINER) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, container->node.id);
	return 1;
}

static int scroll_workspace_get_name(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_workspace *workspace = lua_touserdata(L, -1);
	if (!workspace || workspace->node.type != N_WORKSPACE) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushstring(L, workspace->name);
	return 1;
}

static int scroll_workspace_get_tiling(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	struct sway_workspace *workspace = lua_touserdata(L, -1);
	if (!workspace || workspace->node.type != N_WORKSPACE ||
		workspace->tiling->length == 0) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	lua_checkstack(L, workspace->tiling->length + STACK_MIN);
	lua_createtable(L, workspace->tiling->length, 0);
	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct sway_container *container = workspace->tiling->items[i];
		lua_pushlightuserdata(L, container);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static int scroll_workspace_get_floating(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	struct sway_workspace *workspace = lua_touserdata(L, -1);
	if (!workspace || workspace->node.type != N_WORKSPACE ||
		workspace->floating->length == 0) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	lua_checkstack(L, workspace->floating->length + STACK_MIN);
	lua_createtable(L, workspace->floating->length, 0);
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *container = workspace->floating->items[i];
		lua_pushlightuserdata(L, container);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static int scroll_workspace_get_mode(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	struct sway_workspace *workspace = lua_touserdata(L, -1);
	if (!workspace || workspace->node.type != N_WORKSPACE) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	lua_newtable(L);
	enum sway_container_layout mode = layout_modifiers_get_mode(workspace);
	switch (mode) {
	case L_NONE:
		lua_pushstring(L, "none");
		break;
	case L_HORIZ:
		lua_pushstring(L, "horizontal");
		break;
	case L_VERT:
		lua_pushstring(L, "vertical");
		break;
	}
	lua_setfield(L, -2, "mode");

	enum sway_layout_insert insert = layout_modifiers_get_insert(workspace);
	switch (insert) {
	case INSERT_BEFORE:
		lua_pushstring(L, "before");
		break;
	case INSERT_AFTER:
		lua_pushstring(L, "after");
		break;
	case INSERT_BEGINNING:
		lua_pushstring(L, "beginning");
		break;
	case INSERT_END:
		lua_pushstring(L, "end");
		break;
	}
	lua_setfield(L, -2, "insert");

	enum sway_layout_reorder reorder = layout_modifiers_get_reorder(workspace);
	switch (reorder) {
	case REORDER_AUTO:
		lua_pushstring(L, "auto");
		break;
	case REORDER_LAZY:
		lua_pushstring(L, "lazy");
		break;
	}
	lua_setfield(L, -2, "reorder");

	lua_pushboolean(L, layout_modifiers_get_focus(workspace) ? 1 : 0);
	lua_setfield(L, -2, "focus");

	lua_pushboolean(L, layout_modifiers_get_center_horizontal(workspace) ? 1 : 0);
	lua_setfield(L, -2, "center_horizontal");
	
	lua_pushboolean(L, layout_modifiers_get_center_vertical(workspace) ? 1 : 0);
	lua_setfield(L, -2, "center_vertical");

	return 1;
}

static int scroll_workspace_set_mode(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc < 2) {
		return 0;
	}
	struct sway_workspace *workspace = lua_touserdata(L, 1);
	if (!workspace || workspace->node.type != N_WORKSPACE) {
		return 0;
	}
	if (lua_getfield(L, 2, "mode") == LUA_TSTRING) {
		const char *mode = lua_tostring(L, 3);
		if (strcmp(mode, "vertical") == 0) {
			layout_modifiers_set_mode(workspace, L_VERT);
		} else if (strcmp(mode, "horizontal") == 0) {
			layout_modifiers_set_mode(workspace, L_HORIZ);
		}
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 2, "insert") == LUA_TSTRING) {
		const char *mode = lua_tostring(L, 3);
		if (strcmp(mode, "before") == 0) {
			layout_modifiers_set_insert(workspace, INSERT_BEFORE);
		} else if (strcmp(mode, "after") == 0) {
			layout_modifiers_set_insert(workspace, INSERT_AFTER);
		} else if (strcmp(mode, "beginning") == 0) {
			layout_modifiers_set_insert(workspace, INSERT_BEGINNING);
		} else if (strcmp(mode, "end") == 0) {
			layout_modifiers_set_insert(workspace, INSERT_END);
		}
	}
	lua_pop(L, 1);
	
	if (lua_getfield(L, 2, "reorder") == LUA_TSTRING) {
		const char *mode = lua_tostring(L, 3);
		if (strcmp(mode, "auto") == 0) {
			layout_modifiers_set_reorder(workspace, REORDER_AUTO);
		} else if (strcmp(mode, "lazy") == 0) {
			layout_modifiers_set_reorder(workspace, REORDER_LAZY);
		}
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 2, "focus") == LUA_TBOOLEAN) {
		layout_modifiers_set_focus(workspace, lua_toboolean(L, 3));
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 2, "center_horizontal") == LUA_TBOOLEAN) {
		layout_modifiers_set_center_horizontal(workspace, lua_toboolean(L, 3));
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 2, "center_vertical") == LUA_TBOOLEAN) {
		layout_modifiers_set_center_vertical(workspace, lua_toboolean(L, 3));
	}
	lua_pop(L, 1);

	return 0;
}

static int scroll_workspace_get_layout_type(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_workspace *workspace = lua_touserdata(L, -1);
	if (!workspace || workspace->node.type != N_WORKSPACE) {
		lua_pushnil(L);
		return 1;
	}
	enum sway_container_layout type = layout_get_type(workspace);
	switch (type) {
	case L_HORIZ:
		lua_pushstring(L, "horizontal");
		break;
	case L_VERT:
		lua_pushstring(L, "vertical");
		break;
	default:
		lua_pushnil(L);
		break;
	}
	return 1;
}

static int scroll_workspace_set_layout_type(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc < 2) {
		return 0;
	}
	struct sway_workspace *workspace = lua_touserdata(L, 1);
	if (!workspace || workspace->node.type != N_WORKSPACE) {
		return 0;
	}
	const char *layout = luaL_checkstring(L, 2);
	if (strcmp(layout, "horizontal") == 0) {
		layout_set_type(workspace, L_HORIZ);
	} else if (strcmp(layout, "vertical") == 0) {
		layout_set_type(workspace, L_VERT);
	}
	return 0;
}

static int scroll_workspace_get_width(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_workspace *workspace = lua_touserdata(L, -1);
	if (!workspace || workspace->node.type != N_WORKSPACE) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, workspace->width);
	return 1;
}

static int scroll_workspace_get_height(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_workspace *workspace = lua_touserdata(L, -1);
	if (!workspace || workspace->node.type != N_WORKSPACE) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, workspace->height);
	return 1;
}

static int scroll_scratchpad_get_containers(lua_State *L) {
	lua_checkstack(L, root->scratchpad->length + STACK_MIN);
	lua_createtable(L, root->scratchpad->length, 0);
	for (int i = 0; i < root->scratchpad->length; ++i) {
		struct sway_container *container = root->scratchpad->items[i];
		lua_pushlightuserdata(L, container);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static int scroll_output_get_active_workspace(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_output *output = lua_touserdata(L, -1);
	if (!output || output->node.type != N_OUTPUT) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlightuserdata(L, output->current.active_workspace);
	return 1;
}

static int scroll_output_get_enabled(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushboolean(L, 0);
		return 1;
	}
	struct sway_output *output = lua_touserdata(L, -1);
	if (!output || output->node.type != N_OUTPUT) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, output->enabled);
	return 1;
}

static int scroll_output_get_name(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_output *output = lua_touserdata(L, -1);
	if (!output || output->node.type != N_OUTPUT) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushstring(L, output->wlr_output->name);
	return 1;
}

static int scroll_output_get_workspaces(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc == 0) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	struct sway_output *output = lua_touserdata(L, -1);
	if (!output || output->node.type != N_OUTPUT) {
		lua_createtable(L, 0, 0);
		return 1;
	}
	lua_checkstack(L, output->workspaces->length + STACK_MIN);
	lua_createtable(L, output->workspaces->length, 0);
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct sway_workspace *workspace = output->workspaces->items[i];
		lua_pushlightuserdata(L, workspace);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static int scroll_root_get_outputs(lua_State *L) {
	lua_checkstack(L, root->outputs->length + STACK_MIN);
	lua_createtable(L, root->outputs->length, 0);
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		lua_pushlightuserdata(L, output);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// local id = scroll.add_callback(event, on_create, data)
static int scroll_add_callback(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc < 3) {
		lua_pushnil(L);
		return 1;
	}
	struct sway_lua_closure *closure = malloc(sizeof(struct sway_lua_closure));
	closure->cb_data = luaL_ref(L, LUA_REGISTRYINDEX);
	closure->cb_function = luaL_ref(L, LUA_REGISTRYINDEX);
	const char *event = luaL_checkstring(L, 1);
	if (strcmp(event, "view_map") == 0) {
		list_add(config->lua.cbs_view_map, closure);
	} else if (strcmp(event, "view_unmap") == 0) {
		list_add(config->lua.cbs_view_unmap, closure);
	} else if (strcmp(event, "view_urgent") == 0) {
		list_add(config->lua.cbs_view_urgent, closure);
	} else if (strcmp(event, "view_focus") == 0) {
		list_add(config->lua.cbs_view_focus, closure);
	} else if (strcmp(event, "workspace_create") == 0) {
		list_add(config->lua.cbs_workspace_create, closure);
	} else {
		free(closure);
		lua_pushnil(L);
		return 1;
	}
	lua_pushlightuserdata(L, closure);
	return 1;
}

//scroll.remove_callback(id)
static int scroll_remove_callback(lua_State *L) {
	int argc = lua_gettop(L);
	if (argc < 1) {
		return 0;
	}
	struct sway_lua_closure *closure = lua_touserdata(L, 1);
	for (int i = 0; i < config->lua.cbs_view_map->length; ++i) {
		if (config->lua.cbs_view_map->items[i] == closure) {
			list_del(config->lua.cbs_view_map, i);
			luaL_unref(config->lua.state, LUA_REGISTRYINDEX, closure->cb_function);
			luaL_unref(config->lua.state, LUA_REGISTRYINDEX, closure->cb_data);
			free(closure);
			return 0;
		}
	}
	for (int i = 0; i < config->lua.cbs_view_unmap->length; ++i) {
		if (config->lua.cbs_view_unmap->items[i] == closure) {
			list_del(config->lua.cbs_view_unmap, i);
			luaL_unref(config->lua.state, LUA_REGISTRYINDEX, closure->cb_function);
			luaL_unref(config->lua.state, LUA_REGISTRYINDEX, closure->cb_data);
			free(closure);
			return 0;
		}
	}
	for (int i = 0; i < config->lua.cbs_view_urgent->length; ++i) {
		if (config->lua.cbs_view_urgent->items[i] == closure) {
			list_del(config->lua.cbs_view_urgent, i);
			luaL_unref(config->lua.state, LUA_REGISTRYINDEX, closure->cb_function);
			luaL_unref(config->lua.state, LUA_REGISTRYINDEX, closure->cb_data);
			free(closure);
			return 0;
		}
	}
	for (int i = 0; i < config->lua.cbs_view_focus->length; ++i) {
		if (config->lua.cbs_view_focus->items[i] == closure) {
			list_del(config->lua.cbs_view_focus, i);
			luaL_unref(config->lua.state, LUA_REGISTRYINDEX, closure->cb_function);
			luaL_unref(config->lua.state, LUA_REGISTRYINDEX, closure->cb_data);
			free(closure);
			return 0;
		}
	}
	for (int i = 0; i < config->lua.cbs_workspace_create->length; ++i) {
		if (config->lua.cbs_workspace_create->items[i] == closure) {
			list_del(config->lua.cbs_workspace_create, i);
			luaL_unref(config->lua.state, LUA_REGISTRYINDEX, closure->cb_function);
			luaL_unref(config->lua.state, LUA_REGISTRYINDEX, closure->cb_data);
			free(closure);
			return 0;
		}
	}
	return 0;
}

// Module functions
static luaL_Reg const scroll_lib[] = {
	{ "log", scroll_log },
	{ "state_set_value", scroll_state_set_value },
	{ "state_get_value", scroll_state_get_value },
	{ "command", scroll_command },
	{ "focused_view", scroll_focused_view },
	{ "focused_container", scroll_focused_container },
	{ "focused_workspace", scroll_focused_workspace },
	{ "urgent_view", scroll_urgent_view },
	{ "view_mapped", scroll_view_mapped },
	{ "view_get_container", scroll_view_get_container },
	{ "view_get_app_id", scroll_view_get_app_id },
	{ "view_get_title", scroll_view_get_title },
	{ "view_get_pid", scroll_view_get_pid },
	{ "view_get_parent_view", scroll_view_get_parent_view },
	{ "view_get_urgent", scroll_view_get_urgent },
	{ "view_set_urgent", scroll_view_set_urgent },
	{ "view_get_shell", scroll_view_get_shell },
	{ "view_get_tag", scroll_view_get_tag },
	{ "view_close", scroll_view_close },
	{ "container_get_workspace", scroll_container_get_workspace },
	{ "container_get_marks", scroll_container_get_marks },
	{ "container_get_floating", scroll_container_get_floating },
	{ "container_get_opacity", scroll_container_get_opacity },
	{ "container_get_sticky", scroll_container_get_sticky },
	{ "container_get_width_fraction", scroll_container_get_width_fraction },
	{ "container_get_height_fraction", scroll_container_get_height_fraction },
	{ "container_get_width", scroll_container_get_width },
	{ "container_get_height", scroll_container_get_height },
	{ "container_get_fullscreen_mode", scroll_container_get_fullscreen_mode },
	{ "container_get_fullscreen_app_mode", scroll_container_get_fullscreen_app_mode },
	{ "container_get_fullscreen_view_mode", scroll_container_get_fullscreen_view_mode },
	{ "container_get_fullscreen_layout_mode", scroll_container_get_fullscreen_layout_mode },
	{ "container_get_parent", scroll_container_get_parent },
	{ "container_get_children", scroll_container_get_children },
	{ "container_get_views", scroll_container_get_views },
	{ "container_get_id", scroll_container_get_id },
	{ "workspace_get_name", scroll_workspace_get_name },
	{ "workspace_get_tiling", scroll_workspace_get_tiling },
	{ "workspace_get_floating", scroll_workspace_get_floating },
	{ "workspace_get_mode", scroll_workspace_get_mode },
	{ "workspace_set_mode", scroll_workspace_set_mode },
	{ "workspace_get_layout_type", scroll_workspace_get_layout_type },
	{ "workspace_set_layout_type", scroll_workspace_set_layout_type },
	{ "workspace_get_width", scroll_workspace_get_width },
	{ "workspace_get_height", scroll_workspace_get_height },
	{ "output_get_enabled", scroll_output_get_enabled },
	{ "output_get_name", scroll_output_get_name },
	{ "output_get_active_workspace", scroll_output_get_active_workspace },
	{ "output_get_workspaces", scroll_output_get_workspaces },
	{ "root_get_outputs", scroll_root_get_outputs },
	{ "scratchpad_get_containers", scroll_scratchpad_get_containers },
	{ "add_callback", scroll_add_callback },
	{ "remove_callback", scroll_remove_callback },
	{ NULL, NULL }
};

// Module Loader
int luaopen_scroll(lua_State *L) {
	luaL_newlib(L, scroll_lib);
	return 1;
}
