#include <assert.h>
#include <stddef.h>
#include <wlr/util/box.h>

static void test_box_empty(void) {
	// NULL is empty
	assert(wlr_box_empty(NULL));

	// Zero width/height
	struct wlr_box box = { .x = 0, .y = 0, .width = 0, .height = 10 };
	assert(wlr_box_empty(&box));
	box = (struct wlr_box){ .x = 0, .y = 0, .width = 10, .height = 0 };
	assert(wlr_box_empty(&box));

	// Negative width/height
	box = (struct wlr_box){ .x = 0, .y = 0, .width = -1, .height = 10 };
	assert(wlr_box_empty(&box));
	box = (struct wlr_box){ .x = 0, .y = 0, .width = 10, .height = -1 };
	assert(wlr_box_empty(&box));

	// Valid box
	box = (struct wlr_box){ .x = 0, .y = 0, .width = 10, .height = 10 };
	assert(!wlr_box_empty(&box));
}

static void test_box_intersection(void) {
	struct wlr_box dest;

	// Overlapping
	struct wlr_box a = { .x = 0, .y = 0, .width = 100, .height = 100 };
	struct wlr_box b = { .x = 50, .y = 50, .width = 100, .height = 100 };
	assert(wlr_box_intersection(&dest, &a, &b));
	assert(dest.x == 50 && dest.y == 50 &&
		dest.width == 50 && dest.height == 50);

	// Non-overlapping
	b = (struct wlr_box){ .x = 200, .y = 200, .width = 50, .height = 50 };
	assert(!wlr_box_intersection(&dest, &a, &b));
	assert(dest.width == 0 && dest.height == 0);

	// Touching edges
	b = (struct wlr_box){ .x = 100, .y = 0, .width = 50, .height = 50 };
	assert(!wlr_box_intersection(&dest, &a, &b));

	// Self-intersection
	assert(wlr_box_intersection(&dest, &a, &a));
	assert(dest.x == a.x && dest.y == a.y &&
		dest.width == a.width && dest.height == a.height);

	// Empty input
	struct wlr_box empty = { .x = 0, .y = 0, .width = 0, .height = 0 };
	assert(!wlr_box_intersection(&dest, &a, &empty));

	// NULL input
	assert(!wlr_box_intersection(&dest, &a, NULL));
	assert(!wlr_box_intersection(&dest, NULL, &a));
}

static void test_box_intersects_box(void) {
	// Overlapping
	struct wlr_box a = { .x = 0, .y = 0, .width = 100, .height = 100 };
	struct wlr_box b = { .x = 50, .y = 50, .width = 100, .height = 100 };
	assert(wlr_box_intersects(&a, &b));

	// Non-overlapping
	b = (struct wlr_box){ .x = 200, .y = 200, .width = 50, .height = 50 };
	assert(!wlr_box_intersects(&a, &b));

	// Touching edges
	b = (struct wlr_box){ .x = 100, .y = 0, .width = 50, .height = 50 };
	assert(!wlr_box_intersects(&a, &b));

	// Self-intersection
	assert(wlr_box_intersects(&a, &a));

	// Empty input
	struct wlr_box empty = { .x = 0, .y = 0, .width = 0, .height = 0 };
	assert(!wlr_box_intersects(&a, &empty));

	// NULL input
	assert(!wlr_box_intersects(&a, NULL));
	assert(!wlr_box_intersects(NULL, &a));
}

static void test_box_contains_point(void) {
	struct wlr_box box = { .x = 10, .y = 20, .width = 100, .height = 50 };

	// Interior point
	assert(wlr_box_contains_point(&box, 50, 40));

	// Inclusive lower bound
	assert(wlr_box_contains_point(&box, 10, 20));

	// Exclusive upper bound
	assert(!wlr_box_contains_point(&box, 110, 70));
	assert(!wlr_box_contains_point(&box, 110, 40));
	assert(!wlr_box_contains_point(&box, 50, 70));

	// Outside
	assert(!wlr_box_contains_point(&box, 5, 40));
	assert(!wlr_box_contains_point(&box, 50, 15));

	// Empty box
	struct wlr_box empty = { .x = 0, .y = 0, .width = 0, .height = 0 };
	assert(!wlr_box_contains_point(&empty, 0, 0));

	// NULL
	assert(!wlr_box_contains_point(NULL, 0, 0));
}

static void test_box_contains_box(void) {
	struct wlr_box outer = { .x = 0, .y = 0, .width = 100, .height = 100 };

	// Fully contained
	struct wlr_box inner = { .x = 10, .y = 10, .width = 50, .height = 50 };
	assert(wlr_box_contains_box(&outer, &inner));

	// Self-containment
	assert(wlr_box_contains_box(&outer, &outer));

	// Partial overlap — not contained
	struct wlr_box partial = { .x = 50, .y = 50, .width = 100, .height = 100 };
	assert(!wlr_box_contains_box(&outer, &partial));

	// Empty inner
	struct wlr_box empty = { .x = 0, .y = 0, .width = 0, .height = 0 };
	assert(!wlr_box_contains_box(&outer, &empty));

	// Empty outer
	assert(!wlr_box_contains_box(&empty, &inner));

	// NULL
	assert(!wlr_box_contains_box(&outer, NULL));
	assert(!wlr_box_contains_box(NULL, &outer));
}

int main(void) {
#ifdef NDEBUG
	fprintf(stderr, "NDEBUG must be disabled for tests\n");
	return 1;
#endif

	test_box_empty();
	test_box_intersection();
	test_box_intersects_box();
	test_box_contains_point();
	test_box_contains_box();
	return 0;
}
