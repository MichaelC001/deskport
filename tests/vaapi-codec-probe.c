#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/error.h>

static void require(int result, const char *operation) {
    if (result < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, error, sizeof(error));
        fprintf(stderr, "%s: %s\n", operation, error);
        exit(1);
    }
}
static enum AVPixelFormat va_format(AVCodecContext *context, const enum AVPixelFormat *formats) {
    (void)context;
    for (; *formats != AV_PIX_FMT_NONE; ++formats)
        if (*formats == AV_PIX_FMT_VAAPI) return *formats;
    return AV_PIX_FMT_NONE; /* Refuse any software decoding fallback. */
}
static void mappings(void) {
    FILE *stream = fopen("/proc/self/maps", "r");
    if (!stream) return;
    char line[4096];
    while (fgets(line, sizeof(line), stream))
        if (strstr(line, "libva") || strstr(line, "libavcodec") || strstr(line, "libavutil")
            || strstr(line, "libgallium") || strstr(line, "libc.so") || strstr(line, "ld-linux"))
            fputs(line, stdout);
    fclose(stream);
}
int main(int argc, char **argv) {
    const char *device = argc > 1 ? argv[1] : "/dev/dri/renderD128";
    int encode_only = argc > 2 && strcmp(argv[2], "--encode-only") == 0;
    AVCodecContext *decoder = NULL;
    printf("FFmpeg=%s; avcodec=%u; avutil=%u\n", av_version_info(), avcodec_version(), avutil_version());
    AVBufferRef *hardware = NULL;
    int drm_fd = -1;
    VADisplay display = NULL;
    if (encode_only) {
        /* Sunshine's prepared FFmpeg lacks its CLI DRM discovery backend.
         * Match Sunshine: provide an already initialized VA display explicitly. */
        drm_fd = open(device, O_RDWR | O_CLOEXEC);
        if (drm_fd < 0) { perror(device); return 1; }
        display = vaGetDisplayDRM(drm_fd);
        int major = 0, minor = 0;
        if (vaInitialize(display, &major, &minor) != VA_STATUS_SUCCESS) return 1;
        printf("Host VA-API=%d.%d; vendor=%s\n", major, minor, vaQueryVendorString(display));
        hardware = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
        if (!hardware) return 1;
        AVHWDeviceContext *context = (AVHWDeviceContext *)hardware->data;
        ((AVVAAPIDeviceContext *)context->hwctx)->display = display;
        require(av_hwdevice_ctx_init(hardware), "Initialize host VAAPI device");
    } else {
        require(av_hwdevice_ctx_create(&hardware, AV_HWDEVICE_TYPE_VAAPI, device, NULL, 0), "VAAPI device");
    }
    AVBufferRef *frames = av_hwframe_ctx_alloc(hardware);
    if (!frames) return 1;
    AVHWFramesContext *pool = (AVHWFramesContext *)frames->data;
    pool->format = AV_PIX_FMT_VAAPI;
    pool->sw_format = AV_PIX_FMT_NV12;
    pool->width = 1280;
    pool->height = 720;
    pool->initial_pool_size = 20;
    require(av_hwframe_ctx_init(frames), "VAAPI frame pool");
    const AVCodec *codec = avcodec_find_encoder_by_name("h264_vaapi");
    if (!codec) { fputs("h264_vaapi unavailable\n", stderr); return 1; }
    AVCodecContext *encoder = avcodec_alloc_context3(codec);
    encoder->width = pool->width;
    encoder->height = pool->height;
    encoder->pix_fmt = AV_PIX_FMT_VAAPI;
    encoder->time_base = (AVRational){1, 30};
    encoder->framerate = (AVRational){30, 1};
    encoder->bit_rate = 5000000;
    encoder->gop_size = 12;
    encoder->max_b_frames = 0;
    encoder->hw_frames_ctx = av_buffer_ref(frames);
    require(avcodec_open2(encoder, codec, NULL), "Open h264_vaapi encoder");
    AVFrame *software = av_frame_alloc();
    software->format = AV_PIX_FMT_NV12;
    software->width = pool->width;
    software->height = pool->height;
    require(av_frame_get_buffer(software, 32), "CPU test image");
    for (int y = 0; y < software->height; ++y)
        for (int x = 0; x < software->width; ++x)
            software->data[0][y * software->linesize[0] + x] = 16 + (x / 64) * 10;
    for (int y = 0; y < software->height / 2; ++y)
        memset(software->data[1] + y * software->linesize[1], 128, software->width);
    AVPacket *packets[64] = {0};
    int count = 0;
    for (int index = 0; index <= 12; ++index) {
        AVFrame *image = NULL;
        if (index < 12) {
            image = av_frame_alloc();
            require(av_hwframe_get_buffer(frames, image, 0), "GPU test image");
            require(av_hwframe_transfer_data(image, software, 0), "Upload test image");
            image->pts = index;
        }
        require(avcodec_send_frame(encoder, image), "Submit GPU encode");
        av_frame_free(&image);
        for (;;) {
            AVPacket *packet = av_packet_alloc();
            int status = avcodec_receive_packet(encoder, packet);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) { av_packet_free(&packet); break; }
            require(status, "Receive encoded packet");
            if (count >= 64) return 1;
            packets[count++] = packet;
        }
    }
    if (count != 12) { fprintf(stderr, "Encoded %d/12 frames\n", count); return 1; }
    printf("PASS: h264_vaapi hardware encoded %d synthetic 1280x720 frames\n", count);
    if (encode_only) goto finished;
    const AVCodec *decode_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!decode_codec) { fputs("H.264 decoder unavailable\n", stderr); return 1; }
    decoder = avcodec_alloc_context3(decode_codec);
    decoder->hw_device_ctx = av_buffer_ref(hardware);
    decoder->get_format = va_format;
    decoder->thread_count = 1;
    require(avcodec_open2(decoder, decoder->codec, NULL), "Open forced VAAPI decoder");
    int decoded = 0;
    for (int index = 0; index <= count; ++index) {
        require(avcodec_send_packet(decoder, index < count ? packets[index] : NULL), "Submit GPU decode");
        for (;;) {
            AVFrame *image = av_frame_alloc();
            int status = avcodec_receive_frame(decoder, image);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) { av_frame_free(&image); break; }
            require(status, "Receive decoded image");
            if (image->format != AV_PIX_FMT_VAAPI || image->width != 1280 || image->height != 720) return 1;
            AVFrame *result = av_frame_alloc();
            require(av_hwframe_transfer_data(result, image, 0), "Download decoded image");
            int differences = 0;
            for (int x = 32; x < 1280; x += 64) {
                int actual = result->data[0][360 * result->linesize[0] + x];
                int expected = 16 + (x / 64) * 10;
                if (abs(actual - expected) > 4) ++differences;
            }
            if (differences) { fprintf(stderr, "Test image mismatch: %d\n", differences); return 1; }
            ++decoded;
            av_frame_free(&result);
            av_frame_free(&image);
        }
    }
    if (decoded != 12) { fprintf(stderr, "Decoded %d/12 frames\n", decoded); return 1; }
    printf("PASS: forced VAAPI hardware decoded %d frames; synthetic pixels verified\n", decoded);
finished:
    mappings();
    for (int i = 0; i < count; ++i) av_packet_free(&packets[i]);
    av_frame_free(&software);
    avcodec_free_context(&decoder);
    avcodec_free_context(&encoder);
    av_buffer_unref(&frames);
    av_buffer_unref(&hardware);
    if (display) vaTerminate(display);
    if (drm_fd >= 0) close(drm_fd);
    return 0;
}
