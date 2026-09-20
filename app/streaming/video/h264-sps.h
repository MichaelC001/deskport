#pragma once

#include <algorithm>
#include <h264_stream.h>

// Reduce unnecessary decoder buffering without changing the reference-picture
// contract declared by the encoder. VideoToolbox may use multiple references
// even when the client requested one; shrinking num_ref_frames corrupts decoding.
inline void minimizeH264DecodeBuffer(sps_t& sps)
{
    if (sps.vui.bitstream_restriction_flag && sps.vui.num_reorder_frames == 0) {
        sps.vui.max_dec_frame_buffering = std::max(1, sps.num_ref_frames);
    }
}
