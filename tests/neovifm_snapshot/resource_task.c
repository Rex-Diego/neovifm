#include <stic.h>

#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <test-utils.h>

#include "../../src/neovifm/resource_task.h"

#ifndef _WIN32
static const char *const FUSE_ZIP = SANDBOX_PATH "/resource-fuse-zip";
static const char *const UNMOUNT = SANDBOX_PATH "/resource-umount";
static const char *const ARCHIVE = SANDBOX_PATH "/resource-bundle.zip";

static void
install_helpers(void)
{
	make_file(FUSE_ZIP,
		"#!/bin/sh\n"
		"test \"$1\" = -r || exit 11\n"
		"test -f \"$2\" || exit 12\n"
		"touch \"$3/.mounted\" || exit 13\n"
		"exit 0\n");
	make_file(UNMOUNT,
		"#!/bin/sh\n"
		"rm -f \"$1/.mounted\" || exit 21\n"
		"rmdir \"$1\" || exit 22\n"
		"exit 0\n");
	assert_success(chmod(FUSE_ZIP, 0700));
	assert_success(chmod(UNMOUNT, 0700));
	make_file(ARCHIVE, "archive");
}

static void
remove_helpers(void)
{
	const char *const paths[] = { FUSE_ZIP, UNMOUNT, ARCHIVE };
	for(size_t i = 0U; i < sizeof(paths)/sizeof(paths[0]); ++i)
	{
		if(access(paths[i], F_OK) == 0) remove_file(paths[i]);
	}
}

static int
pop_terminal(nv_resource_task_queue_t *queue, nv_resource_task_event_t *event,
		int *saw_queued, int *saw_running)
{
	for(int attempt = 0; attempt < 800; ++attempt)
	{
		if(nv_resource_task_queue_pop(queue, event) == 1)
		{
			if(event->state == NV_RESOURCE_TASK_QUEUED) *saw_queued = 1;
			if(event->state == NV_RESOURCE_TASK_RUNNING) *saw_running = 1;
			if(event->state == NV_RESOURCE_TASK_DONE ||
					event->state == NV_RESOURCE_TASK_FAILED ||
					event->state == NV_RESOURCE_TASK_CANCELLED) return 1;
			nv_resource_task_event_free(event);
		}
		else
		{
			usleep(5000);
		}
	}
	return 0;
}

TEST(resource_task_mount_and_unmount_emit_lifecycle_events)
{
	install_helpers();
	const nv_resource_mount_options_t options = {
		.fuse_zip_path = FUSE_ZIP,
		.unmount_path = UNMOUNT,
	};
	nv_resource_task_queue_t *const queue = nv_resource_task_queue_alloc();
	assert_non_null(queue);
	const nv_resource_task_request_t request = {
		.kind = NV_RESOURCE_TASK_MOUNT_ARCHIVE,
		.pane = 0U,
		.tab_id = 7U,
		.command_sequence = 12U,
		.source_path = ARCHIVE,
		.mount_options = &options,
	};
	uint64_t task_id = 0U;
	assert_success(nv_resource_task_queue_submit(queue, &request, &task_id));
	assert_true(task_id != 0U);
	int saw_queued = 0, saw_running = 0;
	nv_resource_task_event_t event = {};
	assert_true(pop_terminal(queue, &event, &saw_queued, &saw_running));
	assert_true(saw_queued);
	assert_true(saw_running);
	assert_int_equal(NV_RESOURCE_TASK_DONE, event.state);
	assert_int_equal(task_id, event.task_id);
	assert_string_equal(ARCHIVE, event.source_path);
	assert_non_null(event.mount_point);
	assert_string_equal(UNMOUNT, event.unmount_path);
	assert_success(access(event.mount_point, F_OK));
	char *const mount_point = strdup(event.mount_point);
	char *const unmount_path = strdup(event.unmount_path);
	assert_non_null(mount_point);
	assert_non_null(unmount_path);
	nv_resource_task_event_free(&event);

	const nv_resource_task_request_t unmount = {
		.kind = NV_RESOURCE_TASK_UNMOUNT,
		.pane = 0U,
		.tab_id = 7U,
		.command_sequence = 13U,
		.mount_point = mount_point,
		.unmount_path = unmount_path,
	};
	assert_success(nv_resource_task_queue_submit(queue, &unmount, NULL));
	saw_queued = 0;
	saw_running = 0;
	assert_true(pop_terminal(queue, &event, &saw_queued, &saw_running));
	assert_int_equal(NV_RESOURCE_TASK_DONE, event.state);
	assert_false(access(mount_point, F_OK) == 0);
	nv_resource_task_event_free(&event);
	free(mount_point);
	free(unmount_path);
	nv_resource_task_queue_free(queue);
	remove_helpers();
}

TEST(resource_task_rejects_missing_mounter_and_cancels_pending_work)
{
	install_helpers();
	nv_resource_task_queue_t *const queue = nv_resource_task_queue_alloc_paused();
	assert_non_null(queue);
	const nv_resource_mount_options_t options = {
		.fuse_zip_path = SANDBOX_PATH "/missing-resource-helper",
		.unmount_path = UNMOUNT,
	};
	const nv_resource_task_request_t request = {
		.kind = NV_RESOURCE_TASK_MOUNT_ARCHIVE,
		.pane = 1U,
		.tab_id = 8U,
		.command_sequence = 14U,
		.source_path = ARCHIVE,
		.mount_options = &options,
	};
	uint64_t task_id = 0U;
	assert_success(nv_resource_task_queue_submit(queue, &request, &task_id));
	assert_success(nv_resource_task_queue_cancel(queue, task_id));
	assert_success(nv_resource_task_queue_start(queue));
	int saw_queued = 0, saw_running = 0;
	nv_resource_task_event_t event = {};
	assert_true(pop_terminal(queue, &event, &saw_queued, &saw_running));
	assert_true(saw_queued);
	assert_int_equal(NV_RESOURCE_TASK_CANCELLED, event.state);
	nv_resource_task_event_free(&event);
	nv_resource_task_queue_free(queue);
	remove_helpers();
}

TEST(resource_task_reports_missing_mounter_when_started)
{
	install_helpers();
	const nv_resource_mount_options_t options = {
		.fuse_zip_path = SANDBOX_PATH "/missing-resource-helper",
		.unmount_path = UNMOUNT,
	};
	nv_resource_task_queue_t *const queue = nv_resource_task_queue_alloc();
	assert_non_null(queue);
	const nv_resource_task_request_t request = {
		.kind = NV_RESOURCE_TASK_MOUNT_ARCHIVE,
		.pane = 0U,
		.tab_id = 9U,
		.command_sequence = 15U,
		.source_path = ARCHIVE,
		.mount_options = &options,
	};
	assert_success(nv_resource_task_queue_submit(queue, &request, NULL));
	int saw_queued = 0, saw_running = 0;
	nv_resource_task_event_t event = {};
	assert_true(pop_terminal(queue, &event, &saw_queued, &saw_running));
	assert_true(saw_running);
	assert_int_equal(NV_RESOURCE_TASK_FAILED, event.state);
	assert_string_equal("resource-mounter-unavailable", event.error_code);
	nv_resource_task_event_free(&event);
	nv_resource_task_queue_free(queue);
	remove_helpers();
}
#endif

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
