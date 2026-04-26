#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include "render/dmabuf.h"
#include "render/drm_syncobj_merger.h"

#include "config.h"

#if HAVE_LINUX_SYNC_FILE

#include <linux/sync_file.h>
#include <sys/ioctl.h>

static int sync_file_merge(int fd1, int fd2) {
	// The kernel will automatically prune signalled fences
	struct sync_merge_data merge_data = { .fd2 = fd2 };
	if (ioctl(fd1, SYNC_IOC_MERGE, &merge_data) < 0) {
		wlr_log_errno(WLR_ERROR, "ioctl(SYNC_IOC_MERGE) failed");
		return -1;
	}

	return merge_data.fence;
}

#else

static int sync_file_merge(int fd1, int fd2) {
	wlr_log(WLR_ERROR, "sync_file support is unavailable");
	return -1;
}

#endif

struct wlr_drm_syncobj_merger *wlr_drm_syncobj_merger_create(
		struct wlr_drm_syncobj_timeline *dst_timeline, uint64_t dst_point) {
	struct wlr_drm_syncobj_merger *merger = calloc(1, sizeof(*merger));
	if (merger == NULL) {
		return NULL;
	}
	merger->n_ref = 1;
	merger->dst_timeline = wlr_drm_syncobj_timeline_ref(dst_timeline);
	merger->dst_point = dst_point;
	merger->sync_fd = -1;
	return merger;
}

struct wlr_drm_syncobj_merger *wlr_drm_syncobj_merger_ref(
		struct wlr_drm_syncobj_merger *merger) {
	assert(merger->n_ref > 0);
	merger->n_ref++;
	return merger;
}

void wlr_drm_syncobj_merger_unref(struct wlr_drm_syncobj_merger *merger) {
	if (merger == NULL) {
		return;
	}
	assert(merger->n_ref > 0);
	merger->n_ref--;
	if (merger->n_ref > 0) {
		return;
	}

	if (merger->sync_fd != -1) {
		wlr_drm_syncobj_timeline_import_sync_file(merger->dst_timeline,
			merger->dst_point, merger->sync_fd);
		close(merger->sync_fd);
	} else {
		wlr_drm_syncobj_timeline_signal(merger->dst_timeline, merger->dst_point);
	}
	wlr_drm_syncobj_timeline_unref(merger->dst_timeline);
	free(merger);
}

static bool merger_add_exportable(struct wlr_drm_syncobj_merger *merger,
		struct wlr_drm_syncobj_timeline *src_timeline, uint64_t src_point) {
	int new_sync = wlr_drm_syncobj_timeline_export_sync_file(src_timeline, src_point);
	return wlr_drm_syncobj_merger_add_sync_file(merger, new_sync);
}

struct export_waiter {
	struct wlr_drm_syncobj_timeline_waiter waiter;
	struct wlr_drm_syncobj_merger *merger;
	struct wlr_drm_syncobj_timeline *src_timeline;
	uint64_t src_point;
};

static void export_waiter_handle_ready(struct wlr_drm_syncobj_timeline_waiter *waiter) {
	struct export_waiter *add = wl_container_of(waiter, add, waiter);
	merger_add_exportable(add->merger, add->src_timeline, add->src_point);
	wlr_drm_syncobj_merger_unref(add->merger);
	wlr_drm_syncobj_timeline_unref(add->src_timeline);
	wlr_drm_syncobj_timeline_waiter_finish(&add->waiter);
	free(add);
}

bool wlr_drm_syncobj_merger_add(struct wlr_drm_syncobj_merger *merger,
		struct wlr_drm_syncobj_timeline *src_timeline, uint64_t src_point,
		struct wl_event_loop *loop) {
	assert(loop != NULL);
	bool exportable = false;
	int flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE;
	if (!wlr_drm_syncobj_timeline_check(src_timeline, src_point, flags, &exportable)) {
		return false;
	}
	if (exportable) {
		return merger_add_exportable(merger, src_timeline, src_point);
	}
	struct export_waiter *add = calloc(1, sizeof(*add));
	if (add == NULL) {
		return false;
	}
	if (!wlr_drm_syncobj_timeline_waiter_init(&add->waiter, src_timeline, src_point,
			flags, loop, export_waiter_handle_ready)) {
		return false;
	}
	add->merger = merger;
	add->src_timeline = wlr_drm_syncobj_timeline_ref(src_timeline);
	add->src_point = src_point;
	merger->n_ref++;
	return true;
}

bool wlr_drm_syncobj_merger_add_sync_file(struct wlr_drm_syncobj_merger *merger,
		int fd) {
	int new_sync = fd;
	if (merger->sync_fd != -1) {
		new_sync = sync_file_merge(merger->sync_fd, fd);
		close(fd);
		close(merger->sync_fd);
	}
	merger->sync_fd = new_sync;
	return merger->sync_fd != -1;
}

struct poll_waiter {
	struct wl_event_source *event_source;
	struct wlr_drm_syncobj_merger *merger;
};

static int poll_waiter_handle_done(int fd, uint32_t mask, void *data) {
	struct poll_waiter *waiter = data;
	wlr_drm_syncobj_merger_unref(waiter->merger);
	wl_event_source_remove(waiter->event_source);
	free(waiter);
	return 0;
}

bool wlr_drm_syncobj_merger_add_dmabuf(struct wlr_drm_syncobj_merger *merger,
		struct wlr_buffer *buffer, struct wl_event_loop *event_loop) {
	struct wlr_dmabuf_attributes dmabuf_attributes;
	if (!wlr_buffer_get_dmabuf(buffer, &dmabuf_attributes)) {
		return true;
	}

	bool res = true;
	for (int i = 0; i < dmabuf_attributes.n_planes; ++i) {
		int sync_fd = dmabuf_export_sync_file(dmabuf_attributes.fd[i], DMA_BUF_SYNC_WRITE);
		if (sync_fd == -1) {
			res = false;
			break;
		}
		if (!wlr_drm_syncobj_merger_add_sync_file(merger, sync_fd)) {
			return false;
		}
	}

	if (res) {
		return true;
	}

	uint32_t mask = WL_EVENT_ERROR | WL_EVENT_HANGUP | WL_EVENT_WRITABLE;
	for (int i = 0; i < dmabuf_attributes.n_planes; ++i) {
		struct poll_waiter *waiter = calloc(1, sizeof(*waiter));
		if (waiter == NULL) {
			return false;
		}
		waiter->merger = wlr_drm_syncobj_merger_ref(merger);
		waiter->event_source = wl_event_loop_add_fd(event_loop,
			dmabuf_attributes.fd[i], mask, poll_waiter_handle_done, waiter);
		if (waiter->event_source == NULL) {
			wlr_drm_syncobj_merger_unref(waiter->merger);
			free(waiter);
			return false;
		}
	}
	return true;
}
