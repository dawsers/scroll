#include <getopt.h>
#include <pango/pangocairo.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include <wlr/version.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/server.h"
#include "sway/swaynag.h"
#include "sway/desktop/transaction.h"
#include "sway/tree/root.h"
#include "sway/ipc-server.h"
#include "ipc-client.h"
#include "log.h"
#include "stringop.h"
#include "util.h"

static bool terminate_request = false;
static int exit_value = 0;
static struct rlimit original_nofile_rlimit = {0};
struct sway_server server = {0};
struct sway_debug debug = {0};

void sway_terminate(int exit_code) {
	if (!server.wl_display) {
		// Running as IPC client
		exit(exit_code);
	} else {
		// Running as server
		terminate_request = true;
		exit_value = exit_code;
		ipc_event_shutdown("exit");
		wl_display_terminate(server.wl_display);
	}
}

void run_as_ipc_client(char *command, char *socket_path) {
	int socketfd = ipc_open_socket(socket_path);
	uint32_t len = strlen(command);
	char *resp = ipc_single_command(socketfd, IPC_COMMAND, command, &len);
	printf("%s\n", resp);
	free(resp);
	close(socketfd);
}

static void log_env(void) {
	const char *log_vars[] = {
		"LD_LIBRARY_PATH",
		"LD_PRELOAD",
		"PATH",
		"SCROLLSOCK",
	};
	for (size_t i = 0; i < sizeof(log_vars) / sizeof(char *); ++i) {
		char *value = getenv(log_vars[i]);
		sway_log(SWAY_INFO, "%s=%s", log_vars[i], value != NULL ? value : "");
	}
}

static void log_file(FILE *f) {
	char *line = NULL;
	size_t line_size = 0;
	ssize_t nread;
	while ((nread = getline(&line, &line_size, f)) != -1) {
		if (line[nread - 1] == '\n') {
			line[nread - 1] = '\0';
		}
		sway_log(SWAY_INFO, "%s", line);
	}
	free(line);
}

static void log_distro(void) {
	const char *paths[] = {
		"/etc/lsb-release",
		"/etc/os-release",
		"/etc/debian_version",
		"/etc/redhat-release",
		"/etc/gentoo-release",
	};
	for (size_t i = 0; i < sizeof(paths) / sizeof(char *); ++i) {
		FILE *f = fopen(paths[i], "r");
		if (f) {
			sway_log(SWAY_INFO, "Contents of %s:", paths[i]);
			log_file(f);
			fclose(f);
		}
	}
}

static void log_kernel(void) {
	FILE *f = popen("uname -a", "r");
	if (!f) {
		sway_log(SWAY_INFO, "Unable to determine kernel version");
		return;
	}
	log_file(f);
	pclose(f);
}

static void restore_nofile_limit(void) {
	if (original_nofile_rlimit.rlim_cur == 0) {
		return;
	}
	if (setrlimit(RLIMIT_NOFILE, &original_nofile_rlimit) != 0) {
		sway_log_errno(SWAY_ERROR, "Failed to restore max open files limit: "
			"setrlimit(NOFILE) failed");
	}
}

static void increase_nofile_limit(void) {
	if (getrlimit(RLIMIT_NOFILE, &original_nofile_rlimit) != 0) {
		sway_log_errno(SWAY_ERROR, "Failed to bump max open files limit: "
			"getrlimit(NOFILE) failed");
		return;
	}

	struct rlimit new_rlimit = original_nofile_rlimit;
	new_rlimit.rlim_cur = new_rlimit.rlim_max;
	if (setrlimit(RLIMIT_NOFILE, &new_rlimit) != 0) {
		sway_log_errno(SWAY_ERROR, "Failed to bump max open files limit: "
			"setrlimit(NOFILE) failed");
		sway_log(SWAY_INFO, "Running with %d max open files",
			(int)original_nofile_rlimit.rlim_cur);
		return;
	}

	pthread_atfork(NULL, NULL, restore_nofile_limit);
}

static int term_signal(int signal, void *data) {
	sway_terminate(EXIT_SUCCESS);
	return 0;
}

static void restore_signals(void) {
	sigset_t set;
	sigemptyset(&set);
	sigprocmask(SIG_SETMASK, &set, NULL);

	struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
	sigaction(SIGCHLD, &sa_dfl, NULL);
	sigaction(SIGPIPE, &sa_dfl, NULL);
}

static void init_signals(void) {
	wl_event_loop_add_signal(server.wl_event_loop, SIGTERM, term_signal, NULL);
	wl_event_loop_add_signal(server.wl_event_loop, SIGINT, term_signal, NULL);

	struct sigaction sa_ign = { .sa_handler = SIG_IGN };
	// avoid need to reap children
	sigaction(SIGCHLD, &sa_ign, NULL);
	// prevent ipc write errors from crashing sway
	sigaction(SIGPIPE, &sa_ign, NULL);

	pthread_atfork(NULL, NULL, restore_signals);
}

void enable_debug_flag(const char *flag) {
	if (strcmp(flag, "noatomic") == 0) {
		debug.noatomic = true;
	} else if (strcmp(flag, "txn-wait") == 0) {
		debug.txn_wait = true;
	} else if (strcmp(flag, "txn-timings") == 0) {
		debug.txn_timings = true;
	} else if (has_prefix(flag, "txn-timeout=")) {
		server.txn_timeout_ms = atoi(&flag[strlen("txn-timeout=")]);
	} else {
		sway_log(SWAY_ERROR, "Unknown debug flag: %s", flag);
	}
}

static sway_log_importance_t convert_wlr_log_importance(
		enum wlr_log_importance importance) {
	switch (importance) {
	case WLR_ERROR:
		return SWAY_ERROR;
	case WLR_INFO:
		return SWAY_INFO;
	default:
		return SWAY_DEBUG;
	}
}

static void handle_wlr_log(enum wlr_log_importance importance,
		const char *fmt, va_list args) {
	static char sway_fmt[1024];
	snprintf(sway_fmt, sizeof(sway_fmt), "[wlr] %s", fmt);
	_sway_vlog(convert_wlr_log_importance(importance), sway_fmt, args);
}

static const struct option long_options[] = {
	{"help", no_argument, NULL, 'h'},
	{"config", required_argument, NULL, 'c'},
	{"validate", no_argument, NULL, 'C'},
	{"debug", no_argument, NULL, 'd'},
	{"version", no_argument, NULL, 'v'},
	{"verbose", no_argument, NULL, 'V'},
	{"get-socketpath", no_argument, NULL, 'p'},
	{"unsupported-gpu", no_argument, NULL, 'u'},
	{0, 0, 0, 0}
};

static const char usage[] =
	"Usage: scroll [options] [command]\n"
	"\n"
	"  -h, --help             Show help message and quit.\n"
	"  -c, --config <config>  Specify a config file.\n"
	"  -C, --validate         Check the validity of the config file, then exit.\n"
	"  -d, --debug            Enables full logging, including debug information.\n"
	"  -v, --version          Show the version number and quit.\n"
	"  -V, --verbose          Enables more verbose logging.\n"
	"      --get-socketpath   Gets the IPC socket path and prints it, then exits.\n"
	"\n";

int main(int argc, char **argv) {
	static bool verbose = false, debug = false, validate = false;

	char *config_path = NULL;

	int c;
	while (1) {
		int option_index = 0;
		c = getopt_long(argc, argv, "hCdD:vVc:", long_options, &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'h': // help
			printf("%s", usage);
			exit(EXIT_SUCCESS);
			break;
		case 'c': // config
			free(config_path);
			config_path = strdup(optarg);
			break;
		case 'C': // validate
			validate = true;
			break;
		case 'd': // debug
			debug = true;
			break;
		case 'D': // extended debug options
			enable_debug_flag(optarg);
			break;
		case 'u':
			allow_unsupported_gpu = true;
			break;
		case 'v': // version
			printf("scroll version " SWAY_VERSION "\n");
			exit(EXIT_SUCCESS);
			break;
		case 'V': // verbose
			verbose = true;
			break;
		case 'p': // --get-socketpath
			if (getenv("SCROLLSOCK")) {
				printf("%s\n", getenv("SCROLLSOCK"));
				exit(EXIT_SUCCESS);
			} else {
				fprintf(stderr, "scroll socket not detected.\n");
				exit(EXIT_FAILURE);
			}
			break;
		default:
			fprintf(stderr, "%s", usage);
			exit(EXIT_FAILURE);
		}
	}

	// Since wayland requires XDG_RUNTIME_DIR to be set, abort with just the
	// clear error message (when not running as an IPC client).
	if (!getenv("XDG_RUNTIME_DIR") && optind == argc) {
		fprintf(stderr,
				"XDG_RUNTIME_DIR is not set in the environment. Aborting.\n");
		exit(EXIT_FAILURE);
	}

	// As the 'callback' function for wlr_log is equivalent to that for
	// sway, we do not need to override it.
	if (debug) {
		sway_log_init(SWAY_DEBUG, sway_terminate);
		wlr_log_init(WLR_DEBUG, handle_wlr_log);
	} else if (verbose) {
		sway_log_init(SWAY_INFO, sway_terminate);
		wlr_log_init(WLR_INFO, handle_wlr_log);
	} else {
		sway_log_init(SWAY_ERROR, sway_terminate);
		wlr_log_init(WLR_ERROR, handle_wlr_log);
	}

	sway_log(SWAY_INFO, "Scroll version " SWAY_VERSION);
	sway_log(SWAY_INFO, "wlroots version " WLR_VERSION_STR);
	log_kernel();
	log_distro();
	log_env();

	if (optind < argc) { // Behave as IPC client
		if (optind != 1) {
			sway_log(SWAY_ERROR,
					"Detected both options and positional arguments. If you "
					"are trying to use the IPC client, options are not "
					"supported. Otherwise, check the provided arguments for "
					"issues. See `man 1 scroll` or `scroll -h` for usage. If you "
					"are trying to generate a debug log, use "
					"`scroll -d 2>scroll.log`.");
			exit(EXIT_FAILURE);
		}
		char *socket_path = getenv("SCROLLSOCK");
		if (!socket_path) {
			sway_log(SWAY_ERROR, "Unable to retrieve socket path");
			exit(EXIT_FAILURE);
		}
		char *command = join_args(argv + optind, argc - optind);
		run_as_ipc_client(command, socket_path);
		free(command);
		return 0;
	}

	increase_nofile_limit();

	sway_log(SWAY_INFO, "Starting scroll version " SWAY_VERSION);

	if (!server_init(&server)) {
		return 1;
	}

	init_signals();

	if (server.linux_dmabuf_v1) {
		sway_scene_set_linux_dmabuf_v1(root->root_scene, server.linux_dmabuf_v1);
	}

	if (validate) {
		bool valid = load_main_config(config_path, false, true);
		free(config_path);
		return valid ? 0 : 1;
	}

	ipc_init(&server);

	setenv("WAYLAND_DISPLAY", server.socket, true);
	if (!load_main_config(config_path, false, false)) {
		sway_terminate(EXIT_FAILURE);
		goto shutdown;
	}

	set_rr_scheduling();

	if (!server_start(&server)) {
		sway_terminate(EXIT_FAILURE);
		goto shutdown;
	}

	config->active = true;
	force_modeset();
	load_swaybars();
	run_deferred_commands();
	run_deferred_bindings();
	transaction_commit_dirty();

	if (config->swaynag_config_errors.client != NULL) {
		swaynag_show(&config->swaynag_config_errors);
	}

	server_run(&server);

shutdown:
	sway_log(SWAY_INFO, "Shutting down scroll");

	server_fini(&server);
	root_destroy(root);
	root = NULL;

	free(config_path);
	free_config(config);

	pango_cairo_font_map_set_default(NULL);

	return exit_value;
}
