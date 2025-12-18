#include "wlr/version.h"

int wlr_version_get_major(void) {
	return WLR_VERSION_MAJOR;
}

int wlr_version_get_minor(void) {
	return WLR_VERSION_MINOR;
}

int wlr_version_get_micro(void) {
	return WLR_VERSION_MICRO;
}
