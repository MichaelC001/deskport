// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only sizing consumer for another client's Mutter RecordVirtual stream.
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <stdlib.h>

static void process(void *data) {
    struct pw_stream *stream = *(struct pw_stream **)data;
    struct pw_buffer *buffer;
    while ((buffer = pw_stream_dequeue_buffer(stream))) pw_stream_queue_buffer(stream, buffer);
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    pw_init(NULL, NULL);
    struct pw_main_loop *loop = pw_main_loop_new(NULL);
    if (!loop) return 1;
    struct pw_stream *stream = NULL;
    const struct pw_stream_events events = { .version = PW_VERSION_STREAM_EVENTS, .process = process };
    stream = pw_stream_new_simple(pw_main_loop_get_loop(loop), "DeskPort foreign-output fixture",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", NULL), &events, &stream);
    if (!stream) return 1;
    uint8_t bytes[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(bytes, sizeof(bytes));
    const struct spa_rectangle size = SPA_RECTANGLE(atoi(argv[2]), atoi(argv[3]));
    const struct spa_fraction rate = SPA_FRACTION(0, 1);
    const struct spa_pod *format = spa_pod_builder_add_object(&builder,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_BGRx),
        SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&size),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&rate));
    if (pw_stream_connect(stream, PW_DIRECTION_INPUT, strtoul(argv[1], NULL, 10),
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, &format, 1) < 0) return 1;
    pw_main_loop_run(loop);
    pw_stream_destroy(stream);
    pw_main_loop_destroy(loop);
    pw_deinit();
    return 0;
}
