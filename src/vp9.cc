/*
 * Copyright (C) 2024 Max Schettler <max.schettler@posteo.de>
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

#include "vp9.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include <gst/codecparsers/gstvp9parser.h>
#include <gst/codecs/gstvp9statefulparser.h>

extern "C" {
#include <va/va_dec_vp8.h>
#include <va/va_dec_vp9.h>
}

#include "driver.h"
#include "surface.h"
#include "v4l2.h"

namespace {

/**
 * The structured data libVA passed doesn't contain all information we need, so we parse the headers ourselves (i.e.
 * have gstreamer do it).
 */
int parse_frame_header(std::span<uint8_t> data, GstVp9FrameHeader* header)
{
    std::unique_ptr<GstVp9StatefulParser, decltype(&gst_vp9_stateful_parser_free)> parser(
        gst_vp9_stateful_parser_new(), &gst_vp9_stateful_parser_free);

    if (!parser) {
        return -1;
    }

    if (gst_vp9_stateful_parser_parse_uncompressed_frame_header(parser.get(), header, data.data(), data.size())
        != GST_VP9_PARSER_OK) {
        return -1;
    }
    if (gst_vp9_stateful_parser_parse_compressed_frame_header(
            parser.get(), header, data.data() + header->frame_header_length_in_bytes, data.size())
        != GST_VP9_PARSER_OK) {
        return -1;
    }
    return 0;
}

v4l2_ctrl_vp9_frame va_to_v4l2_frame(DriverData* data, VADecPictureParameterBufferVP9* picture,
    VASliceParameterBufferVP9* slice, GstVp9FrameHeader* header)
{
    const auto last_ref_frame = data->surfaces.find(picture->reference_frames[picture->pic_fields.bits.last_ref_frame]);
    const auto golden_ref_frame
        = data->surfaces.find(picture->reference_frames[picture->pic_fields.bits.golden_ref_frame]);
    const auto alt_ref_frame = data->surfaces.find(picture->reference_frames[picture->pic_fields.bits.alt_ref_frame]);

    v4l2_ctrl_vp9_frame result = {
        .lf = {
            //.ref_deltas = {10, 0, 0, 0},
            //.mode_deltas = {10, 0},
            .level = picture->filter_level,
            .sharpness = picture->sharpness_level,
            .flags = static_cast<uint8_t>(
                ((header->loop_filter_params.loop_filter_delta_enabled) ? V4L2_VP9_LOOP_FILTER_FLAG_DELTA_ENABLED : 0) | ((header->loop_filter_params.loop_filter_delta_update) ? V4L2_VP9_LOOP_FILTER_FLAG_DELTA_UPDATE : 0)),
        },
        .quant = {
            .base_q_idx = header->quantization_params.base_q_idx,
            .delta_q_y_dc = header->quantization_params.delta_q_y_dc,
            .delta_q_uv_dc = header->quantization_params.delta_q_uv_dc,
            .delta_q_uv_ac = header->quantization_params.delta_q_uv_ac,
        },
        .seg = {
            .flags = static_cast<uint8_t>( // UPDATE_DATA, ABS_OR_DELTA_UPDATE?
                ((picture->pic_fields.bits.segmentation_enabled) ? V4L2_VP9_SEGMENTATION_FLAG_ENABLED : 0) | ((picture->pic_fields.bits.segmentation_update_map) ? V4L2_VP9_SEGMENTATION_FLAG_UPDATE_MAP : 0) | ((picture->pic_fields.bits.segmentation_temporal_update) ? V4L2_VP9_SEGMENTATION_FLAG_TEMPORAL_UPDATE : 0)),
        },
        .flags = // COLOR_RANGE_FULL_SWING?
        ((picture->pic_fields.bits.frame_type == 0) ? V4L2_VP9_FRAME_FLAG_KEY_FRAME : 0u) | ((picture->pic_fields.bits.show_frame) ? V4L2_VP9_FRAME_FLAG_SHOW_FRAME : 0u) | ((picture->pic_fields.bits.error_resilient_mode) ? V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT : 0u) | ((picture->pic_fields.bits.intra_only) ? V4L2_VP9_FRAME_FLAG_INTRA_ONLY : 0u) | ((picture->pic_fields.bits.allow_high_precision_mv) ? V4L2_VP9_FRAME_FLAG_ALLOW_HIGH_PREC_MV : 0u) | ((picture->pic_fields.bits.refresh_frame_context) ? V4L2_VP9_FRAME_FLAG_REFRESH_FRAME_CTX : 0u) | ((picture->pic_fields.bits.frame_parallel_decoding_mode) ? V4L2_VP9_FRAME_FLAG_PARALLEL_DEC_MODE : 0u) | ((picture->pic_fields.bits.subsampling_x) ? V4L2_VP9_FRAME_FLAG_X_SUBSAMPLING : 0u) | ((picture->pic_fields.bits.subsampling_y) ? V4L2_VP9_FRAME_FLAG_Y_SUBSAMPLING : 0u),
        .compressed_header_size = picture->first_partition_size,
        .uncompressed_header_size = picture->frame_header_length_in_bytes,
        .frame_width_minus_1 = static_cast<uint16_t>(picture->frame_width - 1),
        .frame_height_minus_1 = static_cast<uint16_t>(picture->frame_height - 1),
        .render_width_minus_1 = static_cast<uint16_t>(picture->frame_width - 1),
        .render_height_minus_1 = static_cast<uint16_t>(picture->frame_height - 1),
        .last_frame_ts = (last_ref_frame != data->surfaces.end()) ? v4l2_timeval_to_ns(&last_ref_frame->second.timestamp) : 0,
        .golden_frame_ts = (golden_ref_frame != data->surfaces.end()) ? v4l2_timeval_to_ns(&golden_ref_frame->second.timestamp) : 0,
        .alt_frame_ts = (alt_ref_frame != data->surfaces.end()) ? v4l2_timeval_to_ns(&alt_ref_frame->second.timestamp) : 0,
        .ref_frame_sign_bias = static_cast<uint8_t>(((picture->pic_fields.bits.last_ref_frame_sign_bias) ? V4L2_VP9_SIGN_BIAS_LAST : 0) | ((picture->pic_fields.bits.golden_ref_frame_sign_bias) ? V4L2_VP9_SIGN_BIAS_GOLDEN : 0) | ((picture->pic_fields.bits.alt_ref_frame_sign_bias) ? V4L2_VP9_SIGN_BIAS_ALT : 0)),
        .reset_frame_context = static_cast<uint8_t>((picture->pic_fields.bits.reset_frame_context > 0) ? picture->pic_fields.bits.reset_frame_context - 1 : 0), // V4L2 codes the value differently
        .profile = picture->profile,
        .bit_depth = picture->bit_depth,
        .interpolation_filter = header->interpolation_filter,
        .tile_cols_log2 = picture->log2_tile_columns,
        .tile_rows_log2 = picture->log2_tile_rows,
    };

    memcpy(result.lf.ref_deltas, header->loop_filter_params.loop_filter_ref_deltas, GST_VP9_MAX_REF_LF_DELTAS);
    memcpy(result.lf.mode_deltas, header->loop_filter_params.loop_filter_mode_deltas, GST_VP9_MAX_MODE_LF_DELTAS);

    for (size_t i = 0; i < 8; i += 1) {
        result.seg.feature_enabled[i] = // ALT_Q, ALT_L?
            (slice->seg_param[i].segment_flags.fields.segment_reference_enabled
                    ? V4L2_VP9_SEGMENT_FEATURE_ENABLED(V4L2_VP9_SEG_LVL_REF_FRAME)
                    : 0)
            | (slice->seg_param[i].segment_flags.fields.segment_reference_skipped
                    ? V4L2_VP9_SEGMENT_FEATURE_ENABLED(V4L2_VP9_SEG_LVL_SKIP)
                    : 0);
    }
    memcpy(result.seg.tree_probs, picture->mb_segment_tree_probs, 7);

    return result;
}

} // namespace

v4l2_ctrl_vp9_compressed_hdr gst_to_v4l2_compressed_header(GstVp9FrameHeader* header)
{
    v4l2_ctrl_vp9_compressed_hdr result = {
        .tx_mode = static_cast<uint8_t>(header->tx_mode),
    };

    memcpy(result.tx8, header->delta_probabilities.tx_probs_8x8, GST_VP9_TX_SIZE_CONTEXTS * (GST_VP9_TX_SIZES - 3));
    memcpy(result.tx16, header->delta_probabilities.tx_probs_16x16, GST_VP9_TX_SIZE_CONTEXTS * (GST_VP9_TX_SIZES - 2));
    memcpy(result.tx32, header->delta_probabilities.tx_probs_32x32, GST_VP9_TX_SIZE_CONTEXTS * (GST_VP9_TX_SIZES - 1));
    memcpy(result.coef, header->delta_probabilities.coef, 4 * 2 * 2 * 6 * 6 * 3);
    memcpy(result.skip, header->delta_probabilities.skip, GST_VP9_SKIP_CONTEXTS);
    memcpy(result.inter_mode, header->delta_probabilities.inter_mode,
        GST_VP9_INTER_MODE_CONTEXTS * (GST_VP9_INTER_MODES - 1));
    memcpy(result.interp_filter, header->delta_probabilities.interp_filter,
        GST_VP9_INTERP_FILTER_CONTEXTS * (GST_VP9_SWITCHABLE_FILTERS - 1));
    memcpy(result.is_inter, header->delta_probabilities.is_inter, GST_VP9_IS_INTER_CONTEXTS);
    memcpy(result.comp_mode, header->delta_probabilities.comp_mode, GST_VP9_COMP_MODE_CONTEXTS);
    memcpy(result.single_ref, header->delta_probabilities.single_ref, GST_VP9_REF_CONTEXTS * 2);
    memcpy(result.comp_ref, header->delta_probabilities.comp_ref, GST_VP9_REF_CONTEXTS);
    memcpy(result.y_mode, header->delta_probabilities.y_mode, GST_VP9_BLOCK_SIZE_GROUPS * (GST_VP9_INTRA_MODES - 1));
    memcpy(result.partition, header->delta_probabilities.partition,
        GST_VP9_PARTITION_CONTEXTS * (GST_VP9_PARTITION_TYPES - 1));

    assert(sizeof(v4l2_vp9_mv_probs) == sizeof(GstVp9MvDeltaProbs));
    memcpy(&result.mv, &header->delta_probabilities.mv, sizeof(v4l2_vp9_mv_probs));

    return result;
}

VAStatus VP9Context::store_buffer(const Buffer& buffer) const
{
    auto& surface = driver_data->surfaces.at(render_surface_id);

    const auto source_data = surface.source_buffer->get().mapping()[0];

    switch (buffer.type) {
    case VAPictureParameterBufferType:
        surface.params.vp9.picture = reinterpret_cast<VADecPictureParameterBufferVP9*>(buffer.data.get());
        return VA_STATUS_SUCCESS;

    case VASliceParameterBufferType:
        surface.params.vp9.slice = reinterpret_cast<VASliceParameterBufferVP9*>(buffer.data.get());
        return VA_STATUS_SUCCESS;

    case VASliceDataBufferType:
        /*
         * Since there is no guarantee that the allocation
         * order is the same as the submission order (via
         * RenderPicture), we can't use a V4L2 buffer directly
         * and have to copy from a regular buffer.
         */
        if (surface.source_size_used + buffer.size * buffer.count > source_data.size()) {
            return VA_STATUS_ERROR_NOT_ENOUGH_BUFFER;
        }
        memcpy(source_data.data() + surface.source_size_used, buffer.data.get(), buffer.size * buffer.count);
        surface.source_size_used += buffer.size * buffer.count;
        return VA_STATUS_SUCCESS;

    default:
        return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
    }
}

int VP9Context::set_controls()
{
    auto& surface = driver_data->surfaces.at(render_surface_id);

    GstVp9FrameHeader header = {};

    if (parse_frame_header(surface.source_buffer->get().mapping()[0], &header)) {
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    v4l2_ctrl_vp9_frame frame
        = va_to_v4l2_frame(driver_data, surface.params.vp9.picture, surface.params.vp9.slice, &header);
    v4l2_ctrl_vp9_compressed_hdr hdr = gst_to_v4l2_compressed_header(&header);

    v4l2_ext_control controls[2] = { {
                                         .id = V4L2_CID_STATELESS_VP9_FRAME,
                                         .size = sizeof(frame),
                                         .ptr = &frame,
                                     },
        {
            .id = V4L2_CID_STATELESS_VP9_COMPRESSED_HDR,
            .size = sizeof(hdr),
            .ptr = &hdr,
        } };

    try {
        device.set_ext_controls(surface.request_fd, std::span(controls, 2));
    } catch (std::runtime_error& e) {
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    return VA_STATUS_SUCCESS;
}

std::set<VAProfile> VP9Context::supported_profiles(const V4L2M2MDevice& device)
{
    // TODO: query `va_profile` control for more details
    return (device.format_supported(device.output_buf_type, V4L2_PIX_FMT_VP9_FRAME))
        ? std::set<VAProfile>(
              { VAProfileVP9Profile0, VAProfileVP9Profile1, VAProfileVP9Profile2, VAProfileVP9Profile3 })
        : std::set<VAProfile>();
}
