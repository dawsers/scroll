#include <stdlib.h>
#include "internal.h"

struct sway_animation_group *animation_group_create(
		struct sway_animation_manager *manager,
		sway_animation_group_done_func_t done, void *data) {
	struct sway_animation_group *group =
		calloc(1, sizeof(struct sway_animation_group));
	if (!group) {
		return NULL;
	}
	group->manager = manager;
	group->refs = 1;
	group->start = animation_manager_now(manager);
	group->done = done;
	group->data = data;
	wl_list_init(&group->link);
	return group;
}

struct sway_animation_group *animation_group_ref(struct sway_animation_group *group) {
	++group->refs;
	return group;
}

void animation_group_unref(struct sway_animation_group *group) {
	if (!group) {
		return;
	}
	if (--group->refs == 0) {
		free(group);
	}
}

// The group is done. Notify its creator.
static void group_check_done(struct sway_animation_group *group) {
	if (!group->committed || group->members > 0 || group->queued) {
		return;
	}
	group->queued = true;
	animation_group_ref(group);
	wl_list_insert(group->manager->finished.prev, &group->link);
	animation_manager_schedule_flush(group->manager);
}

void animation_group_commit(struct sway_animation_group *group) {
	if (!group) {
		return;
	}
	group->committed = true;
	group_check_done(group);
}

bool animation_group_empty(struct sway_animation_group *group) {
	return group->members == 0;
}

void animation_group_join(struct sway_animation_group *group) {
	++group->members;
	animation_group_ref(group);
}

void animation_group_leave(struct sway_animation_group *group) {
	--group->members;
	group_check_done(group);
	animation_group_unref(group);
}
