#include <stic.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <test-utils.h>

#include "../../src/neovifm/preview_task.h"

static void
make_bytes(const char path[], const unsigned char bytes[], size_t length)
{
	const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	assert_true(fd >= 0);
	assert_int_equal((long long)length, (long long)write(fd, bytes, length));
	assert_int_equal(0, close(fd));
}

static int
pop_terminal_event(nv_preview_queue_t *queue, nv_preview_event_t *event)
{
	for(int attempt = 0; attempt < 600; ++attempt)
	{
		if(nv_preview_queue_pop(queue, event) == 1)
		{
			if(event->state == NV_PREVIEW_TASK_DONE ||
					event->state == NV_PREVIEW_TASK_FAILED ||
					event->state == NV_PREVIEW_TASK_CANCELLED)
			{
				return 1;
			}
			nv_preview_event_free(event);
		}
		else
		{
			usleep(5000);
		}
	}
	return 0;
}

TEST(preview_queue_rejects_invalid_request_context)
{
	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_LEFT,
		.generation = 0U,
		.cwd_bytes_hex = "2f746d70",
		.path_bytes_hex = "2f746d70",
		.kind = NV_PREVIEW_KIND_TEXT,
		.max_bytes = 64U,
		.timeout_ms = 1000U,
	};
	assert_failure(nv_preview_queue_submit(queue, &request, NULL));
	static char oversized_arg[NV_PREVIEW_MAX_VIEWER_ARG_BYTES + 2U];
	memset(oversized_arg, 'x', sizeof(oversized_arg) - 1U);
	oversized_arg[sizeof(oversized_arg) - 1U] = '\0';
	const char *const viewer_argv[] = { oversized_arg };
	nv_preview_request_t oversized = request;
	oversized.generation = 1U;
	oversized.viewer_argv = viewer_argv;
	oversized.viewer_argc = 1U;
	assert_failure(nv_preview_queue_submit(queue, &oversized, NULL));
	nv_preview_queue_free(queue);
}

TEST(preview_queue_emits_bounded_text_completion_with_raw_identity)
{
	const char *const dir = SANDBOX_PATH "/preview-text";
	const char *const path = SANDBOX_PATH "/preview-text/example.txt";
	create_dir(dir);
	make_file(path, "abcdef");
	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_RIGHT,
		.generation = 7U,
		.cwd_bytes_hex = "",
		.path_bytes_hex = "",
		.kind = NV_PREVIEW_KIND_TEXT,
		.max_bytes = 4U,
		.timeout_ms = 1000U,
	};
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	nv_preview_request_t encoded = request;
	encoded.cwd_bytes_hex = cwd_hex;
	encoded.path_bytes_hex = path_hex;
	uint64_t task_id = 0U;
	assert_success(nv_preview_queue_submit(queue, &encoded, &task_id));
	assert_true(task_id > 0U);

	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_int_equal(task_id, event.task_id);
	assert_int_equal(7, event.generation);
	assert_int_equal(NV_PREVIEW_PANE_RIGHT, event.pane);
	assert_string_equal(cwd_hex, event.cwd_bytes_hex);
	assert_string_equal(path_hex, event.path_bytes_hex);
	assert_string_equal("abcd", event.content);
	assert_true(event.truncated);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_preserves_utf8_and_replaces_invalid_text_bytes)
{
	const char *const dir = SANDBOX_PATH "/preview-utf8";
	const char *const path = SANDBOX_PATH "/preview-utf8/example.txt";
	const unsigned char bytes[] = {
		0xe4, 0xb8, 0xad, 0xe6, 0x96, 0x87, '\n', 0xff, '\t', 0x1b,
	};
	const unsigned char expected[] = {
		0xe4, 0xb8, 0xad, 0xe6, 0x96, 0x87, '\n', '?', '\t', '?', '\0',
	};
	create_dir(dir);
	make_bytes(path, bytes, sizeof(bytes));
	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 2U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_TEXT, .max_bytes = 64U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_int_equal((long long)sizeof(expected) - 1LL,
			(long long)strlen(event.content));
	assert_true(memcmp(expected, event.content, sizeof(expected) - 1U) == 0);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_treats_markdown_as_bounded_text)
{
	const char *const dir = SANDBOX_PATH "/preview-markdown";
	const char *const path = SANDBOX_PATH "/preview-markdown/readme.md";
	create_dir(dir);
	make_file(path, "# heading\n\nbody");
	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 1U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_MARKDOWN, .max_bytes = 64U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_int_equal(NV_PREVIEW_KIND_MARKDOWN, event.kind);
	assert_string_equal("# heading\n\nbody", event.content);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_uses_structured_fileviewer_argv_before_builtin_renderer)
{
	const char *const dir = SANDBOX_PATH "/preview-fileviewer";
	const char *const path = SANDBOX_PATH "/preview-fileviewer/read me.txt";
	const char *const helper = SANDBOX_PATH "/preview-fileviewer/viewer.sh";
	create_dir(dir);
	make_file(path, "builtin content");
	make_file(helper,
			"#!/bin/sh\n"
			"[ \"$1\" = \"--tag\" ] || exit 91\n"
			"printf 'custom:%s\\n' \"$2\"\n");
	assert_success(chmod(helper, 0700));

	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const char *const viewer_argv[] = { helper, "--tag", path };
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 14U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_TEXT, .max_bytes = 512U, .timeout_ms = 1000U,
		.viewer_argv = viewer_argv,
		.viewer_argc = sizeof(viewer_argv)/sizeof(viewer_argv[0]),
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_true(strncmp(event.content, "custom:", strlen("custom:")) == 0);
	assert_non_null(strstr(event.content, path));
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(helper);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_reports_image_dimensions_without_streaming_binary_pixels)
{
	const char *const dir = SANDBOX_PATH "/preview-image";
	const char *const path = SANDBOX_PATH "/preview-image/photo.png";
	create_dir(dir);
	const unsigned char png_header[] = {
		0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
		0, 0, 0, 13, 'I', 'H', 'D', 'R',
		0, 0, 1, 0, 0, 0, 0, 0xc8,
	};
	make_bytes(path, png_header, sizeof(png_header));
	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 10U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_IMAGE, .max_bytes = 512U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_int_equal(NV_PREVIEW_KIND_IMAGE, event.kind);
	assert_non_null(strstr(event.content, "format: PNG"));
	assert_non_null(strstr(event.content, "size: 256x200"));
	assert_non_null(strstr(event.content, "metadata-only"));
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_uses_bounded_block_chafa_before_metadata_fallback)
{
	const char *const dir = SANDBOX_PATH "/preview-chafa";
	const char *const path = SANDBOX_PATH "/preview-chafa/photo image.png";
	const char *const helper = SANDBOX_PATH "/preview-chafa/chafa";
	create_dir(dir);
	const unsigned char png_header[] = {
		0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
		0, 0, 0, 13, 'I', 'H', 'D', 'R',
		0, 0, 1, 0, 0, 0, 0, 1,
	};
	make_bytes(path, png_header, sizeof(png_header));
	make_file(helper,
			"#!/bin/sh\n"
			"[ \"$#\" -eq 15 ] || exit 91\n"
			"[ \"$4\" = block ] || exit 93\n"
			"[ -f \"${15}\" ] || exit 92\n"
			"printf 'BLOCK-IMAGE\\n'\n");
	assert_success(chmod(helper, 0700));
	const char *const old_helper = getenv("NEOVIFM_CHAFA_EXECUTABLE");
	char *const old_copy = old_helper == NULL ? NULL : strdup(old_helper);
	assert_success(setenv("NEOVIFM_CHAFA_EXECUTABLE", helper, 1));

	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_RIGHT, .generation = 13U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_IMAGE, .max_bytes = 512U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_string_equal("BLOCK-IMAGE\n", event.content);
	assert_false(event.truncated);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);

	if(old_copy == NULL) assert_success(unsetenv("NEOVIFM_CHAFA_EXECUTABLE"));
	else assert_success(setenv("NEOVIFM_CHAFA_EXECUTABLE", old_copy, 1));
	free(old_copy);
	remove_file(helper);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_prefers_ascii_image_over_metadata_fileviewer)
{
	const char *const dir = SANDBOX_PATH "/preview-image-viewer";
	const char *const path = SANDBOX_PATH "/preview-image-viewer/photo.png";
	const char *const chafa = SANDBOX_PATH "/preview-image-viewer/chafa";
	const char *const viewer = SANDBOX_PATH "/preview-image-viewer/viewer";
	create_dir(dir);
	const unsigned char png_header[] = {
		0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
		0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 1, 0, 0, 0, 1,
	};
	make_bytes(path, png_header, sizeof(png_header));
	make_file(chafa, "#!/bin/sh\nprintf 'ASCII-FROM-IMAGE\n'\n");
	make_file(viewer, "#!/bin/sh\nprintf 'METADATA-FROM-FILEVIEWER\n'\n");
	assert_success(chmod(chafa, 0700));
	assert_success(chmod(viewer, 0700));
	const char *const old_helper = getenv("NEOVIFM_CHAFA_EXECUTABLE");
	char *const old_copy = old_helper == NULL ? NULL : strdup(old_helper);
	assert_success(setenv("NEOVIFM_CHAFA_EXECUTABLE", chafa, 1));
	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const char *const viewer_argv[] = { viewer };
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 15U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_IMAGE, .max_bytes = 512U, .timeout_ms = 1000U,
		.viewer_argv = viewer_argv, .viewer_argc = 1U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_string_equal("ASCII-FROM-IMAGE\n", event.content);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	if(old_copy == NULL) assert_success(unsetenv("NEOVIFM_CHAFA_EXECUTABLE"));
	else assert_success(setenv("NEOVIFM_CHAFA_EXECUTABLE", old_copy, 1));
	free(old_copy);
	remove_file(chafa);
	remove_file(viewer);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_reports_audio_and_video_metadata_fallbacks)
{
	const char *const dir = SANDBOX_PATH "/preview-media";
	const char *const audio = SANDBOX_PATH "/preview-media/track.mp3";
	const char *const video = SANDBOX_PATH "/preview-media/movie.mp4";
	create_dir(dir);
	const unsigned char mp3_header[] = { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0 };
	const unsigned char mp4_header[] = { 0, 0, 0, 0, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm' };
	make_bytes(audio, mp3_header, sizeof(mp3_header));
	make_bytes(video, mp4_header, sizeof(mp4_header));
	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], audio_hex[1024], video_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(audio, audio_hex, sizeof(audio_hex)));
	assert_success(nv_preview_hex_encode(video, video_hex, sizeof(video_hex)));
	const nv_preview_request_t audio_request = {
		.pane = NV_PREVIEW_PANE_RIGHT, .generation = 11U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = audio_hex,
		.kind = NV_PREVIEW_KIND_AUDIO, .max_bytes = 512U, .timeout_ms = 1000U,
	};
	const nv_preview_request_t video_request = {
		.pane = NV_PREVIEW_PANE_RIGHT, .target_pane = NV_PREVIEW_PANE_LEFT,
		.has_target_pane = 1, .generation = 12U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = video_hex,
		.kind = NV_PREVIEW_KIND_VIDEO, .max_bytes = 512U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &audio_request, NULL));
	assert_success(nv_preview_queue_submit(queue, &video_request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_int_equal(NV_PREVIEW_KIND_AUDIO, event.kind);
	assert_non_null(strstr(event.content, "format: MP3"));
	nv_preview_event_free(&event);
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_int_equal(NV_PREVIEW_KIND_VIDEO, event.kind);
	assert_non_null(strstr(event.content, "format: MP4/MOV"));
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(audio);
	remove_file(video);
	remove_dir(dir);
}

TEST(preview_queue_renders_pdf_first_page_after_empty_viewer_with_raster_helper)
{
	const char *const dir = SANDBOX_PATH "/preview-pdf-raster";
	const char *const path = SANDBOX_PATH "/preview-pdf-raster/read me.pdf";
	const char *const raster = SANDBOX_PATH "/preview-pdf-raster/pdftoppm";
	const char *const chafa = SANDBOX_PATH "/preview-pdf-raster/chafa";
	const char *const viewer = SANDBOX_PATH "/preview-pdf-raster/pdftotext";
	const char *const viewer_argv[] = { viewer };
	create_dir(dir);
	make_file(path, "%PDF-1.4\n");
	make_file(viewer, "#!/bin/sh\nexit 0\n");
	make_file(raster,
			"#!/bin/sh\n"
			"for arg do output=\"$arg\"; done\n"
			"printf 'fake-png' > \"$output.png\"\n");
	make_file(chafa,
			"#!/bin/sh\n"
			"printf 'PDF-ASCII\\n'\n");
	assert_success(chmod(raster, 0700));
	assert_success(chmod(chafa, 0700));
	assert_success(chmod(viewer, 0700));
	const char *const old_raster = getenv("NEOVIFM_PDF_RENDER_EXECUTABLE");
	char *const old_raster_copy = old_raster == NULL ? NULL : strdup(old_raster);
	const char *const old_chafa = getenv("NEOVIFM_CHAFA_EXECUTABLE");
	char *const old_chafa_copy = old_chafa == NULL ? NULL : strdup(old_chafa);
	assert_success(setenv("NEOVIFM_PDF_RENDER_EXECUTABLE", raster, 1));
	assert_success(setenv("NEOVIFM_CHAFA_EXECUTABLE", chafa, 1));

	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 21U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_PDF, .max_bytes = 512U, .timeout_ms = 5000U,
		.viewer_argv = viewer_argv, .viewer_argc = 1U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_string_equal("PDF-ASCII\n", event.content);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);

	if(old_raster_copy == NULL) assert_success(unsetenv("NEOVIFM_PDF_RENDER_EXECUTABLE"));
	else assert_success(setenv("NEOVIFM_PDF_RENDER_EXECUTABLE", old_raster_copy, 1));
	if(old_chafa_copy == NULL) assert_success(unsetenv("NEOVIFM_CHAFA_EXECUTABLE"));
	else assert_success(setenv("NEOVIFM_CHAFA_EXECUTABLE", old_chafa_copy, 1));
	free(old_raster_copy);
	free(old_chafa_copy);
	remove_file(chafa);
	remove_file(raster);
	remove_file(viewer);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_renders_video_first_frame_with_ffmpeg_and_chafa)
{
	const char *const dir = SANDBOX_PATH "/preview-video-frame";
	const char *const path = SANDBOX_PATH "/preview-video-frame/movie clip.mp4";
	const char *const ffmpeg = SANDBOX_PATH "/preview-video-frame/ffmpeg";
	const char *const chafa = SANDBOX_PATH "/preview-video-frame/chafa";
	create_dir(dir);
	make_file(path, "fake video");
	make_file(ffmpeg,
			"#!/bin/sh\n"
			"for arg do output=\"$arg\"; done\n"
			"printf 'fake-png' > \"$output\"\n");
	make_file(chafa,
			"#!/bin/sh\n"
			"printf 'VIDEO-ASCII\\n'\n");
	assert_success(chmod(ffmpeg, 0700));
	assert_success(chmod(chafa, 0700));
	const char *const old_ffmpeg = getenv("NEOVIFM_FFMPEG_EXECUTABLE");
	char *const old_ffmpeg_copy = old_ffmpeg == NULL ? NULL : strdup(old_ffmpeg);
	const char *const old_chafa = getenv("NEOVIFM_CHAFA_EXECUTABLE");
	char *const old_chafa_copy = old_chafa == NULL ? NULL : strdup(old_chafa);
	assert_success(setenv("NEOVIFM_FFMPEG_EXECUTABLE", ffmpeg, 1));
	assert_success(setenv("NEOVIFM_CHAFA_EXECUTABLE", chafa, 1));

	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_RIGHT, .generation = 22U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_VIDEO, .max_bytes = 512U, .timeout_ms = 5000U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_string_equal("VIDEO-ASCII\n", event.content);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);

	if(old_ffmpeg_copy == NULL) assert_success(unsetenv("NEOVIFM_FFMPEG_EXECUTABLE"));
	else assert_success(setenv("NEOVIFM_FFMPEG_EXECUTABLE", old_ffmpeg_copy, 1));
	if(old_chafa_copy == NULL) assert_success(unsetenv("NEOVIFM_CHAFA_EXECUTABLE"));
	else assert_success(setenv("NEOVIFM_CHAFA_EXECUTABLE", old_chafa_copy, 1));
	free(old_ffmpeg_copy);
	free(old_chafa_copy);
	remove_file(chafa);
	remove_file(ffmpeg);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_uses_ffprobe_for_audio_metadata_before_header_fallback)
{
	const char *const dir = SANDBOX_PATH "/preview-audio-probe";
	const char *const path = SANDBOX_PATH "/preview-audio-probe/track.mp3";
	const char *const ffprobe = SANDBOX_PATH "/preview-audio-probe/ffprobe";
	create_dir(dir);
	make_file(path, "ID3\004\0\0");
	make_file(ffprobe,
			"#!/bin/sh\n"
			"printf 'format_name=mp3\\nduration=12.500000\\ncodec_name=mp3\\n'\n");
	assert_success(chmod(ffprobe, 0700));
	const char *const old_ffprobe = getenv("NEOVIFM_FFPROBE_EXECUTABLE");
	char *const old_ffprobe_copy = old_ffprobe == NULL ? NULL : strdup(old_ffprobe);
	assert_success(setenv("NEOVIFM_FFPROBE_EXECUTABLE", ffprobe, 1));

	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 23U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_AUDIO, .max_bytes = 512U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_non_null(strstr(event.content, "duration=12.500000"));
	assert_non_null(strstr(event.content, "codec_name=mp3"));
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);

	if(old_ffprobe_copy == NULL) assert_success(unsetenv("NEOVIFM_FFPROBE_EXECUTABLE"));
	else assert_success(setenv("NEOVIFM_FFPROBE_EXECUTABLE", old_ffprobe_copy, 1));
	free(old_ffprobe_copy);
	remove_file(ffprobe);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_cancels_queued_generation_before_worker_runs_it)
{
	const char *const dir = SANDBOX_PATH "/preview-cancel";
	const char *const first = SANDBOX_PATH "/preview-cancel/first.txt";
	const char *const latest = SANDBOX_PATH "/preview-cancel/latest.txt";
	create_dir(dir);
	make_file(first, "first");
	make_file(latest, "latest");
	nv_preview_queue_t *const queue = nv_preview_queue_alloc_paused();
	assert_non_null(queue);
	char cwd_hex[1024], first_hex[1024], latest_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(first, first_hex, sizeof(first_hex)));
	assert_success(nv_preview_hex_encode(latest, latest_hex, sizeof(latest_hex)));
	const nv_preview_request_t first_request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 1U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = first_hex,
		.kind = NV_PREVIEW_KIND_TEXT, .max_bytes = 64U, .timeout_ms = 1000U,
	};
	const nv_preview_request_t latest_request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 2U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = latest_hex,
		.kind = NV_PREVIEW_KIND_TEXT, .max_bytes = 64U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &first_request, NULL));
	assert_success(nv_preview_queue_submit(queue, &latest_request, NULL));
	assert_success(nv_preview_queue_start(queue));

	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_CANCELLED, event.state);
	assert_int_equal(1, event.generation);
	nv_preview_event_free(&event);
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_int_equal(2, event.generation);
	assert_string_equal("latest", event.content);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(first);
	remove_file(latest);
	remove_dir(dir);
}

TEST(preview_queue_cancels_by_render_target_lane_across_source_panes)
{
	const char *const dir = SANDBOX_PATH "/preview-target-lane";
	const char *const first = SANDBOX_PATH "/preview-target-lane/first.txt";
	const char *const latest = SANDBOX_PATH "/preview-target-lane/latest.txt";
	create_dir(dir);
	make_file(first, "first");
	make_file(latest, "latest");
	nv_preview_queue_t *const queue = nv_preview_queue_alloc_paused();
	assert_non_null(queue);
	char cwd_hex[1024], first_hex[1024], latest_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(first, first_hex, sizeof(first_hex)));
	assert_success(nv_preview_hex_encode(latest, latest_hex, sizeof(latest_hex)));
	const nv_preview_request_t first_request = {
		.pane = NV_PREVIEW_PANE_LEFT, .target_pane = NV_PREVIEW_PANE_RIGHT,
		.has_target_pane = 1, .generation = 11U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = first_hex,
		.kind = NV_PREVIEW_KIND_TEXT, .max_bytes = 64U, .timeout_ms = 1000U,
	};
	const nv_preview_request_t latest_request = {
		.pane = NV_PREVIEW_PANE_RIGHT, .target_pane = NV_PREVIEW_PANE_RIGHT,
		.has_target_pane = 1, .generation = 12U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = latest_hex,
		.kind = NV_PREVIEW_KIND_TEXT, .max_bytes = 64U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &first_request, NULL));
	assert_success(nv_preview_queue_submit(queue, &latest_request, NULL));
	assert_success(nv_preview_queue_start(queue));

	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_CANCELLED, event.state);
	assert_int_equal(NV_PREVIEW_PANE_LEFT, event.pane);
	assert_int_equal(NV_PREVIEW_PANE_RIGHT, event.target_pane);
	nv_preview_event_free(&event);
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_int_equal(NV_PREVIEW_PANE_RIGHT, event.pane);
	assert_int_equal(NV_PREVIEW_PANE_RIGHT, event.target_pane);
	assert_string_equal("latest", event.content);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(first);
	remove_file(latest);
	remove_dir(dir);
}

TEST(preview_queue_reports_missing_path_as_structured_failure)
{
	const char *const dir = SANDBOX_PATH "/preview-error";
	const char *const path = SANDBOX_PATH "/preview-error/missing.txt";
	create_dir(dir);
	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 3U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_TEXT, .max_bytes = 64U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_FAILED, event.state);
	assert_int_equal(3, event.generation);
	assert_string_equal("preview-open-failed", event.error_code);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_dir(dir);
}

TEST(preview_queue_lists_directory_entries_with_bound)
{
	const char *const dir = SANDBOX_PATH "/preview-directory";
	create_dir(dir);
	make_file(SANDBOX_PATH "/preview-directory/one", "1");
	make_file(SANDBOX_PATH "/preview-directory/two", "2");
	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_RIGHT, .generation = 8U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = cwd_hex,
		.kind = NV_PREVIEW_KIND_DIRECTORY, .max_bytes = 64U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_DONE, event.state);
	assert_non_null(strstr(event.content, "one\n"));
	assert_non_null(strstr(event.content, "two\n"));
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(SANDBOX_PATH "/preview-directory/one");
	remove_file(SANDBOX_PATH "/preview-directory/two");
	remove_dir(dir);
}

TEST(preview_queue_reports_expired_request_as_timeout)
{
	const char *const dir = SANDBOX_PATH "/preview-timeout";
	const char *const path = SANDBOX_PATH "/preview-timeout/file";
	create_dir(dir);
	make_file(path, "slow");
	nv_preview_queue_t *const queue = nv_preview_queue_alloc_paused();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_LEFT, .generation = 4U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_TEXT, .max_bytes = 64U, .timeout_ms = 1U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	usleep(5000);
	assert_success(nv_preview_queue_start(queue));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_FAILED, event.state);
	assert_string_equal("preview-timeout", event.error_code);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(path);
	remove_dir(dir);
}

TEST(preview_queue_rejects_fifo_without_blocking_worker)
{
	const char *const dir = SANDBOX_PATH "/preview-fifo";
	const char *const path = SANDBOX_PATH "/preview-fifo/pipe";
	create_dir(dir);
	assert_int_equal(0, mkfifo(path, 0600));
	nv_preview_queue_t *const queue = nv_preview_queue_alloc();
	assert_non_null(queue);
	char cwd_hex[1024], path_hex[1024];
	assert_success(nv_preview_hex_encode(dir, cwd_hex, sizeof(cwd_hex)));
	assert_success(nv_preview_hex_encode(path, path_hex, sizeof(path_hex)));
	const nv_preview_request_t request = {
		.pane = NV_PREVIEW_PANE_RIGHT, .generation = 9U,
		.cwd_bytes_hex = cwd_hex, .path_bytes_hex = path_hex,
		.kind = NV_PREVIEW_KIND_TEXT, .max_bytes = 64U, .timeout_ms = 1000U,
	};
	assert_success(nv_preview_queue_submit(queue, &request, NULL));
	nv_preview_event_t event = {};
	assert_true(pop_terminal_event(queue, &event));
	assert_int_equal(NV_PREVIEW_TASK_FAILED, event.state);
	assert_string_equal("preview-not-regular-file", event.error_code);
	nv_preview_event_free(&event);
	nv_preview_queue_free(queue);
	remove_file(path);
	remove_dir(dir);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
