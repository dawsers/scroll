#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <wayland-server-protocol.h>
#include "log.h"
#include "list.h"
#include "util.h"

int wrap(int i, int max) {
	return ((i % max) + max) % max;
}

bool parse_color(const char *color, uint32_t *result) {
	if (color[0] == '#') {
		++color;
	}
	int len = strlen(color);
	if ((len != 6 && len != 8) || !isxdigit(color[0]) || !isxdigit(color[1])) {
		return false;
	}
	char *ptr;
	uint32_t parsed = strtoul(color, &ptr, 16);
	if (*ptr != '\0') {
		return false;
	}
	*result = len == 6 ? ((parsed << 8) | 0xFF) : parsed;
	return true;
}

void color_to_rgba(float dest[static 4], uint32_t color) {
	dest[0] = ((color >> 24) & 0xff) / 255.0;
	dest[1] = ((color >> 16) & 0xff) / 255.0;
	dest[2] = ((color >> 8) & 0xff) / 255.0;
	dest[3] = (color & 0xff) / 255.0;
}

bool parse_boolean(const char *boolean, bool current) {
	if (strcasecmp(boolean, "1") == 0
			|| strcasecmp(boolean, "yes") == 0
			|| strcasecmp(boolean, "on") == 0
			|| strcasecmp(boolean, "true") == 0
			|| strcasecmp(boolean, "enable") == 0
			|| strcasecmp(boolean, "enabled") == 0
			|| strcasecmp(boolean, "active") == 0) {
		return true;
	} else if (strcasecmp(boolean, "toggle") == 0) {
		return !current;
	}
	// All other values are false to match i3
	return false;
}

float parse_float(const char *value) {
	errno = 0;
	char *end;
	float flt = strtof(value, &end);
	if (*end || errno) {
		sway_log(SWAY_DEBUG, "Invalid float value '%s', defaulting to NAN", value);
		return NAN;
	}
	return flt;
}

bool parse_integer(const char *arg, long *n) {
	char *endptr = NULL;
	errno = 0;
	*n = strtol(arg, &endptr, 10);
	if (errno != 0 || endptr == arg) {
		return false;
	}
	return true;
}

enum movement_unit parse_movement_unit(const char *unit) {
	if (strcasecmp(unit, "px") == 0) {
		return MOVEMENT_UNIT_PX;
	}
	if (strcasecmp(unit, "ppt") == 0) {
		return MOVEMENT_UNIT_PPT;
	}
	if (strcasecmp(unit, "default") == 0) {
		return MOVEMENT_UNIT_DEFAULT;
	}
	return MOVEMENT_UNIT_INVALID;
}

int parse_movement_amount(int argc, char **argv,
		struct movement_amount *amount) {
	if (!sway_assert(argc > 0, "Expected args in parse_movement_amount")) {
		amount->amount = 0;
		amount->unit = MOVEMENT_UNIT_INVALID;
		return 0;
	}

	char *err;
	amount->amount = (int)strtol(argv[0], &err, 10);
	if (*err) {
		// e.g. 10px
		amount->unit = parse_movement_unit(err);
		return 1;
	}
	if (argc == 1) {
		amount->unit = MOVEMENT_UNIT_DEFAULT;
		return 1;
	}
	// Try the second argument
	amount->unit = parse_movement_unit(argv[1]);
	if (amount->unit == MOVEMENT_UNIT_INVALID) {
		amount->unit = MOVEMENT_UNIT_DEFAULT;
		return 1;
	}
	return 2;
}

const char *sway_wl_output_subpixel_to_string(enum wl_output_subpixel subpixel) {
	switch (subpixel) {
	case WL_OUTPUT_SUBPIXEL_UNKNOWN:
		return "unknown";
	case WL_OUTPUT_SUBPIXEL_NONE:
		return "none";
	case WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB:
		return "rgb";
	case WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR:
		return "bgr";
	case WL_OUTPUT_SUBPIXEL_VERTICAL_RGB:
		return "vrgb";
	case WL_OUTPUT_SUBPIXEL_VERTICAL_BGR:
		return "vbgr";
	}
	sway_assert(false, "Unknown value for wl_output_subpixel.");
	return NULL;
}

bool sway_set_cloexec(int fd, bool cloexec) {
	int flags = fcntl(fd, F_GETFD);
	if (flags == -1) {
		sway_log_errno(SWAY_ERROR, "fcntl failed");
		return false;
	}
	if (cloexec) {
		flags = flags | FD_CLOEXEC;
	} else {
		flags = flags & ~FD_CLOEXEC;
	}
	if (fcntl(fd, F_SETFD, flags) == -1) {
		sway_log_errno(SWAY_ERROR, "fcntl failed");
		return false;
	}
	return true;
}

void array_remove_at(struct wl_array *arr, size_t offset, size_t size) {
	assert(arr->size >= offset + size);

	char *data = arr->data;
	memmove(&data[offset], &data[offset + size], arr->size - offset - size);
	arr->size -= size;
}

bool array_realloc(struct wl_array *arr, size_t size) {
	// If the size is less than 1/4th of the allocation size, we shrink it.
	// 1/4th is picked to provide hysteresis, without which an array with size
	// arr->alloc would constantly reallocate if an element is added and then
	// removed continously.
	size_t alloc;
	if (arr->alloc > 0 && size > arr->alloc / 4) {
		alloc = arr->alloc;
	} else {
		alloc = 16;
	}

	while (alloc < size) {
		alloc *= 2;
	}

	if (alloc == arr->alloc) {
		return true;
	}

	void *data = realloc(arr->data, alloc);
	if (data == NULL) {
		return false;
	}
	arr->data = data;
	arr->alloc = alloc;
	return true;
}

bool env_parse_bool(const char *option) {
	const char *env = getenv(option);
	if (env) {
		sway_log(SWAY_INFO, "Loading %s option: %s", option, env);
	}

	if (!env || strcmp(env, "0") == 0) {
		return false;
	} else if (strcmp(env, "1") == 0) {
		return true;
	}

	sway_log(SWAY_ERROR, "Unknown %s option: %s", option, env);
	return false;
}

size_t env_parse_switch(const char *option, const char **switches) {
	const char *env = getenv(option);
	if (env) {
		sway_log(SWAY_INFO, "Loading %s option: %s", option, env);
	} else {
		return 0;
	}

	for (ssize_t i = 0; switches[i]; i++) {
		if (strcmp(env, switches[i]) == 0) {
			return i;
		}
	}

	sway_log(SWAY_ERROR, "Unknown %s option: %s", option, env);
	return 0;
}

int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

int64_t timespec_to_nsec(const struct timespec *a) {
	return (int64_t)a->tv_sec * NSEC_PER_SEC + a->tv_nsec;
}

void timespec_from_nsec(struct timespec *r, int64_t nsec) {
	r->tv_sec = nsec / NSEC_PER_SEC;
	r->tv_nsec = nsec % NSEC_PER_SEC;
}

int64_t get_current_time_msec(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return timespec_to_msec(&now);
}

void timespec_sub(struct timespec *r, const struct timespec *a,
		const struct timespec *b) {
	r->tv_sec = a->tv_sec - b->tv_sec;
	r->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

static void skip_spaces(char **head) {
	while (**head == ' ') {
		++*head;
	}
}

// Parse an array and return a list
list_t *parse_double_array(char *str) {
	if (str[0] != '[' || str[strlen(str) - 1] != ']') {
		return NULL;
	}
	++str;
	list_t *list = create_list();
	while(*str != ']') {
		double *val;
		val = malloc(sizeof(double));
		char *end;
		*val = strtod(str, &end);
		list_add(list, val);
		if (*end) {
			skip_spaces(&end);
			if (end == str) {
				list_free_items_and_destroy(list);
				return NULL;
			}
			str = end;
		}
	}
	return list;
}

// Copies a list of doubles into a new list
list_t *copy_double_list(list_t *src) {
	if (src->length == 0) {
		return NULL;
	}
	list_t *list = create_list();
	for (int i = 0; i < src->length; ++i) {
		double *item = src->items[i];
		double *val = malloc(sizeof(double));
		*val = *item;
		list_add(list, val);
	}
	return list;
}

int max(int a, int b) {
    return a > b ? a : b;
}

int min(int a, int b) {
    return a < b ? a : b;
}

double linear_scale(double a, double b, double t) {
	return a + t * (b - a);
}
