#include "h264-sps.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    sps_t sample {};
    sample.num_ref_frames = 6;
    sample.vui.bitstream_restriction_flag = 1;
    sample.vui.max_dec_frame_buffering = 16;
    minimizeH264DecodeBuffer(sample);
    assert(sample.num_ref_frames == 6 && sample.vui.max_dec_frame_buffering == 6);
    sample.vui.num_reorder_frames = 2;
    sample.vui.max_dec_frame_buffering = 16;
    minimizeH264DecodeBuffer(sample);
    assert(sample.vui.max_dec_frame_buffering == 16);
    sample.vui.bitstream_restriction_flag = 0;
    minimizeH264DecodeBuffer(sample);
    assert(sample.vui.bitstream_restriction_flag == 0);
    sample.num_ref_frames = 1;
    sample.vui.num_reorder_frames = 0;
    sample.vui.bitstream_restriction_flag = 1;
    minimizeH264DecodeBuffer(sample);
    assert(sample.num_ref_frames == 1 && sample.vui.max_dec_frame_buffering == 1);
    unsigned char input[4096], output[8192];
    int size = fread(input, 1, sizeof(input), stdin);
    if (!size) return 0;
    auto stream = h264_new();
    assert(read_nal_unit(stream, input, size) > 0);
    if (argc > 1 && strcmp(argv[1], "legacy") == 0) {
        stream->sps->num_ref_frames = 1;
        stream->sps->vui.max_dec_frame_buffering = 1;
    } else {
        minimizeH264DecodeBuffer(*stream->sps);
    }
    int written = write_nal_unit(stream, output, sizeof(output));
    assert(written > 0);
    // h264bitstream reserves an extra leading byte, as in writeBuffer().
    fwrite(output + 1, 1, written - 1, stdout);
    h264_free(stream);
}
