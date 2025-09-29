#ifndef _SWAY_UTIL_H
#define _SWAY_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <wayland-server-protocol.h>
#include "list.h"

enum movement_unit {
	MOVEMENT_UNIT_PX,
	MOVEMENT_UNIT_PPT,
	MOVEMENT_UNIT_DEFAULT,
	MOVEMENT_UNIT_INVALID,
};

struct movement_amount {
	int amount;
	enum movement_unit unit;
};

/*
 * Parse units such as "px" or "ppt"
 */
enum movement_unit parse_movement_unit(const char *unit);

/*
 * Parse arguments such as "10", "10px" or "10 px".
 * Returns the number of arguments consumed.
 */
int parse_movement_amount(int argc, char **argv,
		struct movement_amount *amount);

/**
 * Wrap i into the range [0, max]
 */
int wrap(int i, int max);

/**
 * Given a string that represents an RGB(A) color, result will be set to a
 * uint32_t version of the color, as long as it is valid. If it is invalid,
 * then false will be returned and result will be untouched.
 */
bool parse_color(const char *color, uint32_t *result);

void color_to_rgba(float dest[static 4], uint32_t color);

/**
 * Given a string that represents a boolean, return the boolean value. This
 * function also takes in the current boolean value to support toggling. If
 * toggling is not desired, pass in true for current so that toggling values
 * get parsed as not true.
 */
bool parse_boolean(const char *boolean, bool current);

/**
 * Given a string that represents a floating point value, return a float.
 * Returns NAN on error.
 */
float parse_float(const char *value);

/**
 * Given a string that represents an integer value, return the value in *n.
 * Returns true if arg is a valid integer, false if not.
 */
bool parse_integer(const char *arg, long *n);

const char *sway_wl_output_subpixel_to_string(enum wl_output_subpixel subpixel);

bool sway_set_cloexec(int fd, bool cloexec);

/**
 * Remove a chunk of memory of the specified size at the specified offset.
 */
void array_remove_at(struct wl_array *arr, size_t offset, size_t size);

/**
 * Grow or shrink the array to fit the specifized size.
 */
bool array_realloc(struct wl_array *arr, size_t size);

/**
 * Parse a bool from an environment variable.
 *
 * On success, the parsed value is returned. On error, false is returned.
 */
bool env_parse_bool(const char *option);

/**
 * Pick a choice from an environment variable.
 *
 * On success, the choice index is returned. On error, zero is returned.
 *
 * switches is a NULL-terminated array.
 */
size_t env_parse_switch(const char *option, const char **switches);

static const long NSEC_PER_SEC = 1000000000;

/**
 * Get the current time, in milliseconds.
 */
int64_t get_current_time_msec(void);

/**
 * Convert a timespec to milliseconds.
 */
int64_t timespec_to_msec(const struct timespec *a);

/**
 * Convert a timespec to nanoseconds.
 */
int64_t timespec_to_nsec(const struct timespec *a);

/**
 * Convert nanoseconds to a timespec.
 */
void timespec_from_nsec(struct timespec *r, int64_t nsec);

/**
 * Subtracts timespec `b` from timespec `a`, and stores the difference in `r`.
 */
void timespec_sub(struct timespec *r, const struct timespec *a,
		const struct timespec *b);

/**
 * Parse an array and return a list
 */
list_t *parse_double_array(char *str);

/**
 * Copies a list of doubles into the returned list
 */
list_t *copy_double_list(list_t *src);

/**
 * max of two integers
 */
int max(int a, int b);

/**
 * min of two integers
 */
int min(int a, int b);

/*
 * linear scale between two values
 */
double linear_scale(double a, double b, double t);

/*
 * Compute the smaller or equal, closest valid logical size for 'size',
 * according to the fractional scale oscale
 */
int valid_logical_size(float oscale, int size);

#endif
