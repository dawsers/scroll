#include "util.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/tree/container.h"

struct cmd_results *cmd_decoration(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "decoration", EXPECTED_AT_LEAST, 2))) {
		return error;
	}
	struct sway_container *container = config->handler_context.container;
	if (!container || !container->view) {
		return cmd_results_new(CMD_INVALID, "Only views can have decorations");
	}
	int i = 0;
	while (i < argc) {
		if (strcmp(argv[i], "border_radius") == 0) {
			container->pending.decoration.border_radius = atoi(argv[++i]); ++i;
		} else if (strcmp(argv[i], "shadow") == 0) {
			container->pending.decoration.shadow = parse_boolean(argv[++i], true); ++i;
		} else if (strcmp(argv[i], "shadow_dynamic") == 0) {
			container->pending.decoration.shadow_dynamic = parse_boolean(argv[++i], true); ++i;
		} else if (strcmp(argv[i], "shadow_size") == 0) {
			container->pending.decoration.shadow_size = atoi(argv[++i]); ++i;
		} else if (strcmp(argv[i], "shadow_blur") == 0) {
			container->pending.decoration.shadow_blur = atoi(argv[++i]); ++i;
		} else if (strcmp(argv[i], "shadow_offset") == 0) {
			container->pending.decoration.shadow_offset_x = atoi(argv[++i]);
			container->pending.decoration.shadow_offset_y = atoi(argv[++i]); ++i;
		} else if (strcmp(argv[i], "shadow_color") == 0 && (i + 1 < argc && argv[i+ 1][0] == '#')) {
			uint32_t color;
			parse_color(argv[++i], &color);
			float fcolor[4];
			color_to_rgba(fcolor, color);
			container->pending.decoration.shadow_color_r = fcolor[0];
			container->pending.decoration.shadow_color_g = fcolor[1];
			container->pending.decoration.shadow_color_b = fcolor[2];
			container->pending.decoration.shadow_color_a = fcolor[3];
			++i;
		} else if (strcmp(argv[i], "dim") == 0) {
			container->pending.decoration.dim = parse_boolean(argv[++i], true); ++i;
		} else if (strcmp(argv[i], "dim_color") == 0 && (i + 1 < argc && argv[i+ 1][0] == '#')) {
			uint32_t color;
			parse_color(argv[++i], &color);
			float fcolor[4];
			color_to_rgba(fcolor, color);
			container->pending.decoration.dim_color_r = fcolor[0];
			container->pending.decoration.dim_color_g = fcolor[1];
			container->pending.decoration.dim_color_b = fcolor[2];
			container->pending.decoration.dim_color_a = fcolor[3];
			++i;
		} else {
			return cmd_results_new(CMD_INVALID,
				"Expected 'decoration [border_radius <n>] [shadow true|false] "
				"[shadow_dynamic true|false] [shadow_size <n>] [shadow_blur <n>] [shadow_offset x y] "
				"[shadow_color #RRGGBBAA] [dim true|false] [dim_color #RRGGBBAA]'");
		}
	}
	container_update(container);
	node_set_dirty(&container->node);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_default_decoration(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "default_decoration", EXPECTED_AT_LEAST, 2))) {
		return error;
	}
	int i = 0;
	while (i < argc) {
		if (strcmp(argv[i], "border_radius") == 0) {
			config->decoration.border_radius = atoi(argv[++i]); ++i;
		} else if (strcmp(argv[i], "shadow") == 0) {
			config->decoration.shadow = parse_boolean(argv[++i], true); ++i;
		} else if (strcmp(argv[i], "shadow_dynamic") == 0) {
			config->decoration.shadow_dynamic = parse_boolean(argv[++i], true); ++i;
		} else if (strcmp(argv[i], "shadow_size") == 0) {
			config->decoration.shadow_size = atoi(argv[++i]); ++i;
		} else if (strcmp(argv[i], "shadow_blur") == 0) {
			config->decoration.shadow_blur = atoi(argv[++i]); ++i;
		} else if (strcmp(argv[i], "shadow_offset") == 0) {
			config->decoration.shadow_offset[0] = atoi(argv[++i]);
			config->decoration.shadow_offset[1] = atoi(argv[++i]); ++i;
		} else if (strcmp(argv[i], "shadow_color") == 0 && (i + 1 < argc && argv[i+ 1][0] == '#')) {
			uint32_t color;
			parse_color(argv[++i], &color);
			color_to_rgba(config->decoration.shadow_color, color);
			++i;
		} else if (strcmp(argv[i], "dim") == 0) {
			config->decoration.dim = parse_boolean(argv[++i], true); ++i;
		} else if (strcmp(argv[i], "dim_color") == 0 && (i + 1 < argc && argv[i+ 1][0] == '#')) {
			uint32_t color;
			parse_color(argv[++i], &color);
			color_to_rgba(config->decoration.dim_color, color);
			++i;
		} else {
			return cmd_results_new(CMD_INVALID,
				"Expected 'default_decoration [border_radius <n>] [shadow true|false] "
				"[shadow_dynamic true|false] [shadow_size <n>] [shadow_blur <n>] [shadow_offset x y] "
				"[shadow_color #RRGGBBAA] [dim true|false] [dim_color #RRGGBBAA]'");
		}
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}
