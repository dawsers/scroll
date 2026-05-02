#include <assert.h>
#include <stdio.h>
#include <wayland-util.h>

#include "render/vulkan.h"

#define BUF_SIZE 1024
#define ALLOC_FAIL ((VkDeviceSize)-1)

static void stage_buffer_init(struct wlr_vk_stage_buffer *buf) {
	*buf = (struct wlr_vk_stage_buffer){
		.buf_size = BUF_SIZE,
	};
	wl_array_init(&buf->watermarks);
}

static void stage_buffer_finish(struct wlr_vk_stage_buffer *buf) {
	wl_array_release(&buf->watermarks);
}

static void push_watermark(struct wlr_vk_stage_buffer *buf,
		uint64_t timeline_point) {
	struct wlr_vk_stage_watermark *mark = wl_array_add(
		&buf->watermarks, sizeof(*mark));
	assert(mark != NULL);
	*mark = (struct wlr_vk_stage_watermark){
		.head = buf->head,
		.timeline_point = timeline_point,
	};
}

static size_t watermark_count(const struct wlr_vk_stage_buffer *buf) {
	return buf->watermarks.size / sizeof(struct wlr_vk_stage_watermark);
}

static void test_alloc_simple(void) {
	struct wlr_vk_stage_buffer buf;
	stage_buffer_init(&buf);

	assert(vulkan_stage_buffer_alloc(&buf, 100, 1) == 0);
	assert(buf.head == 100);
	assert(vulkan_stage_buffer_alloc(&buf, 200, 1) == 100);
	assert(buf.head == 300);
	assert(buf.tail == 0);

	stage_buffer_finish(&buf);
}

static void test_alloc_alignment(void) {
	struct wlr_vk_stage_buffer buf;
	stage_buffer_init(&buf);

	assert(vulkan_stage_buffer_alloc(&buf, 7, 1) == 0);
	assert(buf.head == 7);

	assert(vulkan_stage_buffer_alloc(&buf, 4, 16) == 16);
	assert(buf.head == 20);

	assert(vulkan_stage_buffer_alloc(&buf, 8, 8) == 24);
	assert(buf.head == 32);

	stage_buffer_finish(&buf);
}

static void test_alloc_limit(void) {
	struct wlr_vk_stage_buffer buf;
	stage_buffer_init(&buf);

	// We do not allow allocations that would cause head to equal tail
	assert(vulkan_stage_buffer_alloc(&buf, BUF_SIZE, 1) == ALLOC_FAIL);
	assert(buf.head == 0);

	assert(vulkan_stage_buffer_alloc(&buf, BUF_SIZE-1, 1) == 0);
	assert(buf.head == BUF_SIZE-1);

	stage_buffer_finish(&buf);
}

static void test_alloc_wrap(void) {
	struct wlr_vk_stage_buffer buf;
	stage_buffer_init(&buf);

	// Fill the first 924 bytes
	assert(vulkan_stage_buffer_alloc(&buf, BUF_SIZE - 100, 1) == 0);
	push_watermark(&buf, 1);

	// Fill the end of the buffer
	assert(vulkan_stage_buffer_alloc(&buf, 50, 1) == 924);
	push_watermark(&buf, 2);

	// First, check that we don't wrap prematurely
	assert(vulkan_stage_buffer_alloc(&buf, 50, 1) == ALLOC_FAIL);
	assert(vulkan_stage_buffer_alloc(&buf, 100, 1) == ALLOC_FAIL);

	// Free the beginning of the buffer and try to wrap again
	vulkan_stage_buffer_reclaim(&buf, 1);
	assert(vulkan_stage_buffer_alloc(&buf, 50, 1) == 0);
	assert(buf.tail == 924);
	assert(buf.head == 50);

	// Check that freeing from the end of the buffer still works
	vulkan_stage_buffer_reclaim(&buf, 2);
	assert(buf.tail == 974);
	assert(buf.head == 50);

	// Check that allocations still work
	assert(vulkan_stage_buffer_alloc(&buf, 100, 1) == 50);
	assert(buf.tail == 974);
	assert(buf.head == 150);

	stage_buffer_finish(&buf);
}

static void test_reclaim_empty(void) {
	struct wlr_vk_stage_buffer buf;
	stage_buffer_init(&buf);

	// Fresh buffer with no watermarks and head == tail == 0 is drained.
	vulkan_stage_buffer_reclaim(&buf, 0);
	assert(buf.head == buf.tail);
	assert(buf.tail == 0);

	stage_buffer_finish(&buf);
}

static void test_reclaim_pending_not_completed(void) {
	struct wlr_vk_stage_buffer buf;
	stage_buffer_init(&buf);

	assert(vulkan_stage_buffer_alloc(&buf, 100, 1) == 0);
	push_watermark(&buf, 1);

	// current point hasn't reached the watermark yet.
	vulkan_stage_buffer_reclaim(&buf, 0);
	assert(buf.head != buf.tail);
	assert(buf.tail == 0);
	assert(watermark_count(&buf) == 1);

	stage_buffer_finish(&buf);
}

static void test_reclaim_partial(void) {
	struct wlr_vk_stage_buffer buf;
	stage_buffer_init(&buf);

	assert(vulkan_stage_buffer_alloc(&buf, 100, 1) == 0);
	push_watermark(&buf, 1);
	assert(vulkan_stage_buffer_alloc(&buf, 100, 1) == 100);
	push_watermark(&buf, 2);

	// Only the first watermark is reached.
	vulkan_stage_buffer_reclaim(&buf, 1);
	assert(buf.head != buf.tail);
	assert(buf.tail == 100);
	assert(watermark_count(&buf) == 1);

	const struct wlr_vk_stage_watermark *remaining = buf.watermarks.data;
	assert(remaining[0].head == 200);
	assert(remaining[0].timeline_point == 2);

	stage_buffer_finish(&buf);
}

static void test_reclaim_all(void) {
	struct wlr_vk_stage_buffer buf;
	stage_buffer_init(&buf);

	assert(vulkan_stage_buffer_alloc(&buf, 100, 1) == 0);
	push_watermark(&buf, 1);
	assert(vulkan_stage_buffer_alloc(&buf, 100, 1) == 100);
	push_watermark(&buf, 2);
	assert(vulkan_stage_buffer_alloc(&buf, 100, 1) == 200);
	push_watermark(&buf, 3);

	vulkan_stage_buffer_reclaim(&buf, 100);
	assert(buf.head == buf.tail);
	assert(buf.tail == 300);
	assert(watermark_count(&buf) == 0);

	stage_buffer_finish(&buf);
}

int main(void) {
#ifdef NDEBUG
	fprintf(stderr, "NDEBUG must be disabled for tests\n");
	return 1;
#endif

	test_alloc_simple();
	test_alloc_alignment();
	test_alloc_limit();
	test_alloc_wrap();

	test_reclaim_empty();
	test_reclaim_pending_not_completed();
	test_reclaim_partial();
	test_reclaim_all();

	return 0;
}
