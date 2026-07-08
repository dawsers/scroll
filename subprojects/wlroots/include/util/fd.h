#ifndef UTIL_FD_H
#define UTIL_FD_H

#include <stdbool.h>

bool set_cloexec(int fd, bool cloexec);

#endif
