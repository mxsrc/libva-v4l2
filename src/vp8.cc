#include "vp8.h"

#include <cstring>

extern "C" {
#include <linux/v4l2-controls.h>

#include <va/va.h>
#include <va/va_dec_vp8.h>
}

#include "buffer.h"
#include "context.h"
#include "request.h"
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

static struct v4l2_vp8_segment segment(VAPictureParameterBufferVP8* picture) {
	struct v4l2_vp8_segment result = {
		.quant_update = { 0 },  // FIXME
		.lf_update = { 0 },  // FIXME: picture->loop_filter_level? Already used those...
		.flags = 
			((picture->pic_fields.bits.segmentation_enabled) ? V4L2_VP8_SEGMENT_FLAG_ENABLED : 0u) | 
			((picture->pic_fields.bits.update_mb_segmentation_map) ? V4L2_VP8_SEGMENT_FLAG_UPDATE_MAP : 0u) | 
			((picture->pic_fields.bits.update_segment_feature_data) ? V4L2_VP8_SEGMENT_FLAG_UPDATE_FEATURE_DATA : 0u) | 
			((false) ? V4L2_VP8_SEGMENT_FLAG_DELTA_VALUE_MODE : 0u),  // FIXME
	};

	memcpy(result.segment_probs, picture->mb_segment_tree_probs, sizeof(picture->mb_segment_tree_probs));

	return result;
}


static struct v4l2_vp8_loop_filter lf(VAPictureParameterBufferVP8* picture) {
	struct v4l2_vp8_loop_filter result = {
		.ref_frm_delta = { 0 },
		.mb_mode_delta = { 0 },
		.sharpness_level = static_cast<uint8_t>(picture->pic_fields.bits.sharpness_level),
		.level = picture->loop_filter_level[0],  // FIXME: Which segment?
		.flags =
			((picture->pic_fields.bits.loop_filter_adj_enable) ? V4L2_VP8_LF_ADJ_ENABLE : 0u) | 
			((picture->pic_fields.bits.mode_ref_lf_delta_update) ? V4L2_VP8_LF_DELTA_UPDATE : 0u) |
			((picture->pic_fields.bits.filter_type) ? V4L2_VP8_LF_FILTER_TYPE_SIMPLE : 0u),
	};

	memcpy(result.ref_frm_delta, picture->loop_filter_deltas_ref_frame, sizeof(picture->loop_filter_deltas_ref_frame));
	memcpy(result.mb_mode_delta, picture->loop_filter_deltas_mode, sizeof(picture->loop_filter_deltas_mode));

	return result;
}


static struct v4l2_vp8_quantization quant(VAIQMatrixBufferVP8* iqmatrix) {
	// FIXME adding the remaining values skews the colors in the output. Why?
	struct v4l2_vp8_quantization result = {
		.y_ac_qi = static_cast<uint8_t>(iqmatrix->quantization_index[0][0]),
		//.y_dc_delta = iqmatrix->quantization_index[0][1],
		//.y2_dc_delta = iqmatrix->quantization_index[0][2],
		//.y2_ac_delta = iqmatrix->quantization_index[0][3],
		//.uv_dc_delta = iqmatrix->quantization_index[0][4],
		//.uv_ac_delta = iqmatrix->quantization_index[0][5],
	};
	return result;
}

static struct v4l2_vp8_entropy entropy(VAPictureParameterBufferVP8* picture, VAProbabilityDataBufferVP8 *probabilities) {
	struct v4l2_vp8_entropy result = { };

	memcpy(result.coeff_probs, probabilities->dct_coeff_probs, sizeof(probabilities->dct_coeff_probs));
	memcpy(result.y_mode_probs, picture->y_mode_probs, sizeof(picture->y_mode_probs));
	memcpy(result.uv_mode_probs, picture->uv_mode_probs, sizeof(picture->uv_mode_probs));
	memcpy(result.mv_probs, picture->mv_probs, sizeof(picture->mv_probs));

	return result;
}

static struct v4l2_vp8_entropy_coder_state coder_state(VABoolCoderContextVPX* bool_coder_context) {
	struct v4l2_vp8_entropy_coder_state result = {
		.range = bool_coder_context->range,
		.value = bool_coder_context->value,
		.bit_count = bool_coder_context->count,
	};
	return result;
}


static struct v4l2_ctrl_vp8_frame va_to_v4l2_frame(RequestData *data, VAPictureParameterBufferVP8 *picture, VASliceParameterBufferVP8 *slice, VAIQMatrixBufferVP8 *iqmatrix, VAProbabilityDataBufferVP8 *probabilities) {
	struct object_surface* last_ref = SURFACE(data, picture->last_ref_frame);
	struct object_surface* golden_ref = SURFACE(data, picture->golden_ref_frame);
	struct object_surface* alt_ref = SURFACE(data, picture->alt_ref_frame);

	// FIXME
	// - resolve confusion around segments
	// - determine remaining values
	struct v4l2_ctrl_vp8_frame result = {
		.segment = segment(picture),
		.lf = lf(picture),
		.quant = quant(iqmatrix),
		.entropy = entropy(picture, probabilities),
		.coder_state = coder_state(&picture->bool_coder_ctx),
		.width = static_cast<uint16_t>(picture->frame_width),
		.height = static_cast<uint16_t>(picture->frame_height),
		.horizontal_scale = VP8_UPSCALE_NONE,  // Not available via VA-API
		.vertical_scale = VP8_UPSCALE_NONE,  // Not available via VA-API
		.version = static_cast<uint8_t>(picture->pic_fields.bits.version),
		.prob_skip_false = picture->prob_skip_false,
		.prob_intra = picture->prob_intra,
		.prob_last = picture->prob_last,
		.prob_gf = picture->prob_gf,
		.num_dct_parts = static_cast<uint8_t>(slice->num_of_partitions - 1),
		.first_part_size = slice->slice_data_size - slice->partition_size[1],  // FIXME: Needs to be sum of all partitions
		.first_part_header_bits = slice->macroblock_offset,
		.last_frame_ts = last_ref ? v4l2_timeval_to_ns(&last_ref->timestamp): 0,
		.golden_frame_ts = golden_ref ? v4l2_timeval_to_ns(&golden_ref->timestamp) : 0,
		.alt_frame_ts = alt_ref ? v4l2_timeval_to_ns(&alt_ref->timestamp) : 0,
		.flags = 
			((picture->pic_fields.bits.key_frame == VP8_KEYFRAME) ? V4L2_VP8_FRAME_FLAG_KEY_FRAME : 0u) |
			(false ? V4L2_VP8_FRAME_FLAG_EXPERIMENTAL : 0u) |
			(true ? V4L2_VP8_FRAME_FLAG_SHOW_FRAME : 0u) |  // not provided by libva, assume all frames are shown
			(picture->pic_fields.bits.mb_no_coeff_skip ? V4L2_VP8_FRAME_FLAG_MB_NO_SKIP_COEFF : 0u) |
			(picture->pic_fields.bits.sign_bias_alternate ? V4L2_VP8_FRAME_FLAG_SIGN_BIAS_ALT : 0u) |
			(picture->pic_fields.bits.sign_bias_golden ? V4L2_VP8_FRAME_FLAG_SIGN_BIAS_GOLDEN : 0u),
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
static size_t prefix_data(uint8_t* data, const VAPictureParameterBufferVP8* picture, const VASliceParameterBufferVP8* slice) {
	const uint32_t first_part_size = slice->slice_data_size - slice->partition_size[1];  // FIXME: Needs to be sum of all partitions

	data[0] =
		(picture->pic_fields.bits.key_frame & 0x01) |
		((picture->pic_fields.bits.version & 0x07) << 1) |
		((true & 0x01) << 4) |  // not provided by libva, assume all frames are shown
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


VAStatus vp8_store_buffer(RequestData *driver_data,
			  struct object_surface *surface_object,
			  struct object_buffer *buffer_object)
{
	switch (buffer_object->type) {
	case VASliceDataBufferType:
		/*
		 * Since there is no guarantee that the allocation
		 * order is the same as the submission order (via
		 * RenderPicture), we can't use a V4L2 buffer directly
		 * and have to copy from a regular buffer.
		 */
		surface_object->slices_size += prefix_data(
			surface_object->source_data + surface_object->slices_size,
			&surface_object->params.vp8.picture,
			&surface_object->params.vp8.slice
		);

		memcpy(surface_object->source_data +
			       surface_object->slices_size,
		       buffer_object->data,
		       buffer_object->size * buffer_object->count);
		surface_object->slices_size +=
			buffer_object->size * buffer_object->count;
		surface_object->slices_count++;
		break;

	case VAPictureParameterBufferType:
		memcpy(&surface_object->params.vp8.picture,
		       buffer_object->data,
		       sizeof(surface_object->params.vp8.picture));
		break;

	case VASliceParameterBufferType:
		memcpy(&surface_object->params.vp8.slice,
		       buffer_object->data,
		       sizeof(surface_object->params.vp8.slice));
		break;

	case VAIQMatrixBufferType:
		memcpy(&surface_object->params.vp8.iqmatrix,
		       buffer_object->data,
		       sizeof(surface_object->params.vp8.iqmatrix));
		break;

	case VAProbabilityBufferType:
		memcpy(&surface_object->params.vp8.probabilities,
		       buffer_object->data,
		       sizeof(surface_object->params.vp8.probabilities));
		break;

	default:
		return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
	}

	return VA_STATUS_SUCCESS;
}

int vp8_set_controls(RequestData *data, const Context& context, struct object_surface *surface) {
	struct v4l2_ctrl_vp8_frame frame = va_to_v4l2_frame(
		data,
		&surface->params.vp8.picture,
		&surface->params.vp8.slice,
		&surface->params.vp8.iqmatrix,
		&surface->params.vp8.probabilities
	);

	int rc = v4l2_set_control(data->device.video_fd,
			      surface->request_fd,
			      V4L2_CID_STATELESS_VP8_FRAME,
			      &frame, sizeof(frame));
	if (rc < 0)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	return VA_STATUS_SUCCESS;
}
