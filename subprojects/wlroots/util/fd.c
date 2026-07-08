#include <fcntl.h>
#include <wlr/util/log.h>

#include "util/fd.h"

bool set_cloexec(int fd, bool cloexec) {
	int flags = fcntl(fd, F_GETFD);
	if (flags == -1) {
		wlr_log_errno(WLR_ERROR, "fcntl failed");
		return false;
	}
	if (cloexec) {
		flags = flags | FD_CLOEXEC;
	} else {
		flags = flags & ~FD_CLOEXEC;
	}
	if (fcntl(fd, F_SETFD, flags) == -1) {
		wlr_log_errno(WLR_ERROR, "fcntl failed");
		return false;
	}
	return true;
}
