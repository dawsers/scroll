#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "sway/log.h"
#include "sway/ipc-server.h"

static terminate_callback_t log_terminate = exit;

void _sway_abort(const char *format, ...) {
	va_list args;
	va_start(args, format);
	_sway_vlog(SWAY_ERROR, format, args);
	va_end(args);
	log_terminate(EXIT_FAILURE);
}

bool _sway_assert(bool condition, const char *format, ...) {
	if (condition) {
		return true;
	}

	va_list args;
	va_start(args, format);
	_sway_vlog(SWAY_ERROR, format, args);
	va_end(args);

#ifndef NDEBUG
	raise(SIGABRT);
#endif

	return false;
}

static bool colored = true;
static sway_log_importance_t log_importance = SWAY_ERROR;
static struct timespec start_time = {-1, -1};

static const char *verbosity_colors[] = {
	[SWAY_SILENT] = "",
	[SWAY_ERROR ] = "\x1B[1;31m",
	[SWAY_INFO  ] = "\x1B[1;34m",
	[SWAY_DEBUG ] = "\x1B[1;90m",
};

static const char *verbosity_headers[] = {
	[SWAY_SILENT] = "",
	[SWAY_ERROR] = "[ERROR]",
	[SWAY_INFO] = "[INFO]",
	[SWAY_DEBUG] = "[DEBUG]",
};

static void timespec_sub(struct timespec *r, const struct timespec *a,
		const struct timespec *b) {
	const long NSEC_PER_SEC = 1000000000;
	r->tv_sec = a->tv_sec - b->tv_sec;
	r->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

static void init_start_time(void) {
	if (start_time.tv_sec >= 0) {
		return;
	}
	clock_gettime(CLOCK_MONOTONIC, &start_time);
}

static void sway_log_stderr(sway_log_importance_t verbosity, const char *fmt,
		va_list args) {
	init_start_time();

	if (verbosity > log_importance) {
		return;
	}

	struct timespec ts = {0};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	timespec_sub(&ts, &ts, &start_time);

	char *time = format_str("%02d:%02d:%02d.%03ld ", (int)(ts.tv_sec / 60 / 60),
		(int)(ts.tv_sec / 60 % 60), (int)(ts.tv_sec % 60),
		ts.tv_nsec / 1000000);
	fprintf(stderr, "%s", time);

	unsigned c = (verbosity < SWAY_LOG_IMPORTANCE_LAST) ? verbosity :
		SWAY_LOG_IMPORTANCE_LAST - 1;

	if (colored && isatty(STDERR_FILENO)) {
		fprintf(stderr, "%s", verbosity_colors[c]);
	} else {
		fprintf(stderr, "%s ", verbosity_headers[c]);
	}

	char *message = vformat_str(fmt, args);
	fprintf(stderr, "%s", message);

	if (colored && isatty(STDERR_FILENO)) {
		fprintf(stderr, "\x1B[0m");
	}
	fprintf(stderr, "\n");

	ipc_event_log(verbosity, time, message);
	free(time);
	free(message);
}

void sway_log_init(sway_log_importance_t verbosity, terminate_callback_t callback) {
	init_start_time();

	if (verbosity < SWAY_LOG_IMPORTANCE_LAST) {
		log_importance = verbosity;
	}
	if (callback) {
		log_terminate = callback;
	}
}

void _sway_vlog(sway_log_importance_t verbosity, const char *fmt, va_list args) {
	sway_log_stderr(verbosity, fmt, args);
}

void _sway_log(sway_log_importance_t verbosity, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	sway_log_stderr(verbosity, fmt, args);
	va_end(args);
}

sway_log_importance_t sway_log_get_verbosity() {
	return log_importance;
}

void sway_log_set_verbosity(sway_log_importance_t verbosity) {
	if (verbosity < SWAY_LOG_IMPORTANCE_LAST) {
		log_importance = verbosity;
	}
}

struct log_capture {
	FILE *stream;
	FILE *old_stderr;
	char *buffer;
	size_t buffer_len;
};

static struct log_capture log_capture = {
	.stream = NULL,
	.old_stderr = NULL,
	.buffer = NULL,
	.buffer_len = 0
};

bool sway_log_start_capture() {
	log_capture.stream = open_memstream(&log_capture.buffer, &log_capture.buffer_len);
	bool capturing = log_capture.stream != NULL;
	if (capturing) {
		log_capture.old_stderr = stderr;
		stderr = log_capture.stream;
		return true;
	}
	log_capture.buffer = NULL;
	log_capture.buffer_len = 0;
	return false;
}

char *sway_log_end_capture() {
	if (log_capture.stream)	{
		stderr = log_capture.old_stderr;
		fflush(log_capture.stream);
		fclose(log_capture.stream);
		char *buffer = log_capture.buffer;
		log_capture.stream = NULL;
		log_capture.old_stderr = NULL;
		log_capture.buffer = NULL;
		log_capture.buffer_len = 0;
		return buffer;
	}
	return NULL;
}
