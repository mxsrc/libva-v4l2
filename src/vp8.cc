/*
 * Copyright (C) 2023 Max Schettler <max.schettler@posteo.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "vp8.h"

#include <cstring>
#include <stdexcept>

extern "C" {
#include <linux/v4l2-controls.h>

#include <va/va.h>
#include <va/va_dec_vp8.h>
}

#include "buffer.h"
#include "context.h"
#include "driver.h"
#include "surface.h"
#include "v4l2.h"

enum {
    VP8_UPSCALE_NONE = 0,
    VP8_UPSCALE_5_4 = 1,
    VP8_UPSCALE_5_3 = 2,
    VP8_UPSCALE_2 = 3,
};

enum {
    VP8_KEYFRAME = 0,
    VP8_INTERFRAME = 1,
};

namespace {

v4l2_vp8_segment segment(VAPictureParameterBufferVP8* picture)
{
    v4l2_vp8_segment result = {
        .quant_update = {}, // FIXME
        .lf_update = {}, // FIXME: picture->loop_filter_level? Already used those...
        .flags = ((picture->pic_fields.bits.segmentation_enabled) ? V4L2_VP8_SEGMENT_FLAG_ENABLED : 0u)
            | ((picture->pic_fields.bits.update_mb_segmentation_map) ? V4L2_VP8_SEGMENT_FLAG_UPDATE_MAP : 0u)
            | ((picture->pic_fields.bits.update_segment_feature_data) ? V4L2_VP8_SEGMENT_FLAG_UPDATE_FEATURE_DATA : 0u)
            | ((false) ? V4L2_VP8_SEGMENT_FLAG_DELTA_VALUE_MODE : 0u), // FIXME
    };

    memcpy(result.segment_probs, picture->mb_segment_tree_probs, sizeof(picture->mb_segment_tree_probs));

    return result;
}

v4l2_vp8_loop_filter lf(VAPictureParameterBufferVP8* picture)
{
    v4l2_vp8_loop_filter result = {
        .ref_frm_delta = {},
        .mb_mode_delta = {},
        .sharpness_level = static_cast<uint8_t>(picture->pic_fields.bits.sharpness_level),
        .level = picture->loop_filter_level[0], // FIXME: Which segment?
        .flags = ((picture->pic_fields.bits.loop_filter_adj_enable) ? V4L2_VP8_LF_ADJ_ENABLE : 0u)
            | ((picture->pic_fields.bits.mode_ref_lf_delta_update) ? V4L2_VP8_LF_DELTA_UPDATE : 0u)
            | ((picture->pic_fields.bits.filter_type) ? V4L2_VP8_LF_FILTER_TYPE_SIMPLE : 0u),
    };

    memcpy(result.ref_frm_delta, picture->loop_filter_deltas_ref_frame, sizeof(picture->loop_filter_deltas_ref_frame));
    memcpy(result.mb_mode_delta, picture->loop_filter_deltas_mode, sizeof(picture->loop_filter_deltas_mode));

    return result;
}

v4l2_vp8_quantization quant(VAIQMatrixBufferVP8* iqmatrix)
{
    // FIXME adding the remaining values skews the colors in the output. Why?
    v4l2_vp8_quantization result = {
        .y_ac_qi = static_cast<uint8_t>(iqmatrix->quantization_index[0][0]),
        //.y_dc_delta = iqmatrix->quantization_index[0][1],
        //.y2_dc_delta = iqmatrix->quantization_index[0][2],
        //.y2_ac_delta = iqmatrix->quantization_index[0][3],
        //.uv_dc_delta = iqmatrix->quantization_index[0][4],
        //.uv_ac_delta = iqmatrix->quantization_index[0][5],
    };
    return result;
}

v4l2_vp8_entropy entropy(VAPictureParameterBufferVP8* picture, VAProbabilityDataBufferVP8* probabilities)
{
    v4l2_vp8_entropy result = {};

    memcpy(result.coeff_probs, probabilities->dct_coeff_probs, sizeof(probabilities->dct_coeff_probs));
    memcpy(result.y_mode_probs, picture->y_mode_probs, sizeof(picture->y_mode_probs));
    memcpy(result.uv_mode_probs, picture->uv_mode_probs, sizeof(picture->uv_mode_probs));
    memcpy(result.mv_probs, picture->mv_probs, sizeof(picture->mv_probs));

    return result;
}

v4l2_vp8_entropy_coder_state coder_state(VABoolCoderContextVPX* bool_coder_context)
{
    v4l2_vp8_entropy_coder_state result = {
        .range = bool_coder_context->range,
        .value = bool_coder_context->value,
        .bit_count = bool_coder_context->count,
    };
    return result;
}

v4l2_ctrl_vp8_frame va_to_v4l2_frame(DriverData* data, VAPictureParameterBufferVP8* picture,
    VASliceParameterBufferVP8* slice, VAIQMatrixBufferVP8* iqmatrix, VAProbabilityDataBufferVP8* probabilities)
{
    const auto last_ref = data->surfaces.find(picture->last_ref_frame);
    const auto golden_ref = data->surfaces.find(picture->golden_ref_frame);
    const auto alt_ref = data->surfaces.find(picture->alt_ref_frame);

    // FIXME
    // - resolve confusion around segments
    // - determine remaining values
    v4l2_ctrl_vp8_frame result = {
        .segment = segment(picture),
        .lf = lf(picture),
        .quant = quant(iqmatrix),
        .entropy = entropy(picture, probabilities),
        .coder_state = coder_state(&picture->bool_coder_ctx),
        .width = static_cast<uint16_t>(picture->frame_width),
        .height = static_cast<uint16_t>(picture->frame_height),
        .horizontal_scale = VP8_UPSCALE_NONE, // Not available via VA-API
        .vertical_scale = VP8_UPSCALE_NONE, // Not available via VA-API
        .version = static_cast<uint8_t>(picture->pic_fields.bits.version),
        .prob_skip_false = picture->prob_skip_false,
        .prob_intra = picture->prob_intra,
        .prob_last = picture->prob_last,
        .prob_gf = picture->prob_gf,
        .num_dct_parts = static_cast<uint8_t>(slice->num_of_partitions - 1),
        .first_part_size
        = slice->slice_data_size - slice->partition_size[1], // FIXME: Needs to be sum of all partitions
        .first_part_header_bits = slice->macroblock_offset,
        .last_frame_ts = (last_ref != data->surfaces.end()) ? v4l2_timeval_to_ns(&last_ref->second.timestamp) : 0,
        .golden_frame_ts = (golden_ref != data->surfaces.end()) ? v4l2_timeval_to_ns(&golden_ref->second.timestamp) : 0,
        .alt_frame_ts = (alt_ref != data->surfaces.end()) ? v4l2_timeval_to_ns(&alt_ref->second.timestamp) : 0,
        .flags = ((picture->pic_fields.bits.key_frame == VP8_KEYFRAME) ? V4L2_VP8_FRAME_FLAG_KEY_FRAME : 0u)
            | (false ? V4L2_VP8_FRAME_FLAG_EXPERIMENTAL : 0u) | (true ? V4L2_VP8_FRAME_FLAG_SHOW_FRAME : 0u)
            | // not provided by libva, assume all frames are shown
            (picture->pic_fields.bits.mb_no_coeff_skip ? V4L2_VP8_FRAME_FLAG_MB_NO_SKIP_COEFF : 0u)
            | (picture->pic_fields.bits.sign_bias_alternate ? V4L2_VP8_FRAME_FLAG_SIGN_BIAS_ALT : 0u)
            | (picture->pic_fields.bits.sign_bias_golden ? V4L2_VP8_FRAME_FLAG_SIGN_BIAS_GOLDEN : 0u),
    };
    memcpy(result.dct_part_sizes, slice->partition_size + 1, sizeof(result.dct_part_sizes));

    return result;
}

/**
 * Reconstruct uncompressed data chunk.
 *
 * This is stripped from the data by libva, since it's (mostly) represented by the provided parsed buffers.
 * V4L2 expects it to be present though, so we reconstruct it here.
 */
size_t prefix_data(uint8_t* data, const VAPictureParameterBufferVP8* picture, const VASliceParameterBufferVP8* slice)
{
    const uint32_t first_part_size
        = slice->slice_data_size - slice->partition_size[1]; // FIXME: Needs to be sum of all partitions

    data[0] = (picture->pic_fields.bits.key_frame & 0x01) | ((picture->pic_fields.bits.version & 0x07) << 1)
        | ((true & 0x01) << 4) | // not provided by libva, assume all frames are shown
        (((first_part_size >> 0) & 0x07) << 5);
    data[1] = first_part_size >> 3;
    data[2] = first_part_size >> 11;

    if (picture->pic_fields.bits.key_frame == VP8_INTERFRAME) {
        return 3;
    }

    data[3] = 0x9d;
    data[4] = 0x01;
    data[5] = 0x2a;
    data[6] = picture->frame_width >> 0;
    data[7] = picture->frame_width >> 8;
    data[8] = picture->frame_height >> 0;
    data[9] = picture->frame_height >> 8;

    return 10;
}

} // namespace

VAStatus VP8Context::store_buffer(const Buffer& buffer) const
{
    auto& surface = driver_data->surfaces.at(render_surface_id);

    const auto source_data = surface.source_buffer->get().mapping()[0];
    switch (buffer.type) {
    case VASliceDataBufferType:
        /*
         * Since there is no guarantee that the allocation
         * order is the same as the submission order (via
         * RenderPicture), we can't use a V4L2 buffer directly
         * and have to copy from a regular buffer.
         */
        surface.source_size_used += prefix_data(
            source_data.data() + surface.source_size_used, surface.params.vp8.picture, surface.params.vp8.slice);

        if (surface.source_size_used + buffer.size * buffer.count > source_data.size()) {
            return VA_STATUS_ERROR_NOT_ENOUGH_BUFFER;
        }
        memcpy(source_data.data() + surface.source_size_used, buffer.data.get(), buffer.size * buffer.count);
        surface.source_size_used += buffer.size * buffer.count;
        break;

    case VAPictureParameterBufferType:
        surface.params.vp8.picture = reinterpret_cast<VAPictureParameterBufferVP8*>(buffer.data.get());
        break;

    case VASliceParameterBufferType:
        surface.params.vp8.slice = reinterpret_cast<VASliceParameterBufferVP8*>(buffer.data.get());
        break;

    case VAIQMatrixBufferType:
        surface.params.vp8.iqmatrix = reinterpret_cast<VAIQMatrixBufferVP8*>(buffer.data.get());
        break;

    case VAProbabilityBufferType:
        surface.params.vp8.probabilities = reinterpret_cast<VAProbabilityDataBufferVP8*>(buffer.data.get());
        break;

    default:
        return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
    }

    return VA_STATUS_SUCCESS;
}

int VP8Context::set_controls()
{
    auto& surface = driver_data->surfaces.at(render_surface_id);

    v4l2_ctrl_vp8_frame frame = va_to_v4l2_frame(driver_data, surface.params.vp8.picture, surface.params.vp8.slice,
        surface.params.vp8.iqmatrix, surface.params.vp8.probabilities);

    try {
        device.set_control(surface.request_fd, V4L2_CID_STATELESS_VP8_FRAME, &frame, sizeof(frame));
    } catch (std::runtime_error& e) {
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    return VA_STATUS_SUCCESS;
}

std::set<VAProfile> VP8Context::supported_profiles(const V4L2M2MDevice& device)
{
    return (device.format_supported(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_VP8_FRAME))
        ? std::set<VAProfile>({ VAProfileVP8Version0_3 })
        : std::set<VAProfile>();
};
