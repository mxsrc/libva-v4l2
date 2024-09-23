/*
 * Copyright (C) 2007 Intel Corporation
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
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

#include "h264.h"

#include <cassert>
#include <climits>
#include <cstring>
#include <ctime>

extern "C" {
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <va/va.h>
}

#include "buffer.h"
#include "config.h"
#include "request.h"
#include "surface.h"
#include "v4l2.h"

enum h264_slice_type {
	H264_SLICE_P    = 0,
	H264_SLICE_B    = 1,
};

enum h264_profile {
	H264_PROFILE_BASELINE = 66,
	H264_PROFILE_MAIN = 77,
	H264_PROFILE_SCALABLE_BASELINE = 83,
	H264_PROFILE_SCALABLE_HIGH = 86,
	H264_PROFILE_EXTENDED = 88,
	H264_PROFILE_HIGH = 100,
	H264_PROFILE_HIGH10 = 110,
	H264_PROFILE_HIGH_422 = 122,
	H264_PROFILE_MULTIVIEW_HIGH = 118,
	H264_PROFILE_STEREO_HIGH = 128,
	H264_PROFILE_HIGH_444 = 244,
};

static uint8_t va_profile_to_profile_idc  (VAProfile profile) {
	switch (profile) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
		case VAProfileH264Baseline:
#pragma GCC diagnostic pop
			return H264_PROFILE_BASELINE;
		case VAProfileH264High:
			return H264_PROFILE_HIGH;
		case VAProfileH264Main:
			return H264_PROFILE_MAIN;
		case VAProfileH264MultiviewHigh:
			return H264_PROFILE_MULTIVIEW_HIGH;
		case VAProfileH264StereoHigh:
			return H264_PROFILE_STEREO_HIGH;
		default:
			return 0;
	}
}

static size_t prefix_data(uint8_t* data) {
	data[0] = 0;
	data[1] = 0;
	data[2] = 1;
	return 3;
}

static bool is_picture_null(VAPictureH264 *pic)
{
	return pic->picture_id == VA_INVALID_SURFACE;
}

static struct h264_dpb_entry *
dpb_find_invalid_entry(Context& context)
{
	unsigned int i;

	for (i = 0; i < H264_DPB_SIZE; i++) {
		struct h264_dpb_entry *entry = &context.codec_state.h264.dpb.entries[i];

		if (!entry->valid && !entry->reserved)
			return entry;
	}

	return NULL;
}

static struct h264_dpb_entry *
dpb_find_oldest_unused_entry(Context& context)
{
	unsigned int min_age = UINT_MAX;
	unsigned int i;
	struct h264_dpb_entry *match = NULL;

	for (i = 0; i < H264_DPB_SIZE; i++) {
		struct h264_dpb_entry *entry = &context.codec_state.h264.dpb.entries[i];

		if (!entry->used && (entry->age < min_age)) {
			min_age = entry->age;
			match = entry;
		}
	}

	return match;
}

static struct h264_dpb_entry *dpb_find_entry(Context& context)
{
	struct h264_dpb_entry *entry;

	entry = dpb_find_invalid_entry(context);
	if (!entry)
		entry = dpb_find_oldest_unused_entry(context);

	return entry;
}

static struct h264_dpb_entry *dpb_lookup(Context& context,
					 VAPictureH264 *pic, struct v4l2_h264_reference *ref)
{
	unsigned int i;

	for (i = 0; i < H264_DPB_SIZE; i++) {
		struct h264_dpb_entry *entry = &context.codec_state.h264.dpb.entries[i];

		if (!entry->valid)
			continue;

		if (entry->pic.picture_id == pic->picture_id) {
			if (ref) {
				ref->index = i;
				if (pic->flags & VA_BOTTOM_FIELD) {
					ref->fields |= V4L2_H264_BOTTOM_FIELD_REF;
				}
				if (pic->flags & VA_TOP_FIELD) {
					ref->fields |= V4L2_H264_TOP_FIELD_REF;
				}
			}

			return entry;
		}
	}

	return NULL;
}

static void dpb_clear_entry(struct h264_dpb_entry *entry, bool reserved)
{
	memset(entry, 0, sizeof(*entry));

	if (reserved)
		entry->reserved = true;
}

static void dpb_insert(Context& context, VAPictureH264 *pic,
		       struct h264_dpb_entry *entry)
{
	if (is_picture_null(pic))
		return;

	if (dpb_lookup(context, pic, NULL))
		return;

	if (!entry)
		entry = dpb_find_entry(context);

	memcpy(&entry->pic, pic, sizeof(entry->pic));
	entry->age = context.codec_state.h264.dpb.age;
	entry->valid = true;
	entry->reserved = false;

	if (!(pic->flags & VA_PICTURE_H264_INVALID))
		entry->used = true;
}

static void dpb_update(Context& context,
		       VAPictureParameterBufferH264 *parameters)
{
	unsigned int i;

	context.codec_state.h264.dpb.age++;

	for (i = 0; i < H264_DPB_SIZE; i++) {
		struct h264_dpb_entry *entry = &context.codec_state.h264.dpb.entries[i];

		entry->used = false;
	}

	for (i = 0; i < parameters->num_ref_frames; i++) {
		VAPictureH264 *pic = &parameters->ReferenceFrames[i];
		struct h264_dpb_entry *entry;

		if (is_picture_null(pic))
			continue;

		entry = dpb_lookup(context, pic, NULL);
		if (entry) {
			entry->age = context.codec_state.h264.dpb.age;
			entry->used = true;
		} else {
			dpb_insert(context, pic, NULL);
		}
	}
}

static void h264_fill_dpb(RequestData *data,
			  Context& context,
			  struct v4l2_ctrl_h264_decode_params *decode)
{
	int i;

	for (i = 0; i < H264_DPB_SIZE; i++) {
		struct v4l2_h264_dpb_entry *dpb = &decode->dpb[i];
		struct h264_dpb_entry *entry = &context.codec_state.h264.dpb.entries[i];

		const auto& surface = data->surfaces.find(entry->pic.picture_id);

		uint64_t timestamp;

		if (!entry->valid)
			continue;

		if (surface != data->surfaces.end()) {
			timestamp = v4l2_timeval_to_ns(&surface->second.timestamp);
			dpb->reference_ts = timestamp;
		}

		dpb->frame_num = entry->pic.frame_idx;
		dpb->top_field_order_cnt = entry->pic.TopFieldOrderCnt;
		dpb->bottom_field_order_cnt = entry->pic.BottomFieldOrderCnt;

		dpb->flags = V4L2_H264_DPB_ENTRY_FLAG_VALID;

		if (entry->used)
			dpb->flags |= V4L2_H264_DPB_ENTRY_FLAG_ACTIVE;

		if (entry->pic.flags & VA_PICTURE_H264_LONG_TERM_REFERENCE)
			dpb->flags |= V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM;
	}
}

static void h264_va_picture_to_v4l2(RequestData *driver_data,
				    Context& context,
				    VAPictureParameterBufferH264 *VAPicture,
				    struct v4l2_ctrl_h264_decode_params *decode,
				    struct v4l2_ctrl_h264_pps *pps,
				    struct v4l2_ctrl_h264_sps *sps)
{
	h264_fill_dpb(driver_data, context, decode);

	decode->top_field_order_cnt = VAPicture->CurrPic.TopFieldOrderCnt;
	decode->bottom_field_order_cnt = VAPicture->CurrPic.BottomFieldOrderCnt;

	pps->weighted_bipred_idc =
		VAPicture->pic_fields.bits.weighted_bipred_idc;
	pps->pic_init_qs_minus26 = VAPicture->pic_init_qs_minus26;
	pps->pic_init_qp_minus26 = VAPicture->pic_init_qp_minus26;
	pps->chroma_qp_index_offset = VAPicture->chroma_qp_index_offset;
	pps->second_chroma_qp_index_offset =
		VAPicture->second_chroma_qp_index_offset;

	if (VAPicture->pic_fields.bits.entropy_coding_mode_flag)
		pps->flags |= V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE;

	if (VAPicture->pic_fields.bits.weighted_pred_flag)
		pps->flags |= V4L2_H264_PPS_FLAG_WEIGHTED_PRED;

	if (VAPicture->pic_fields.bits.transform_8x8_mode_flag)
		pps->flags |= V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE;

	if (VAPicture->pic_fields.bits.constrained_intra_pred_flag)
		pps->flags |= V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED;

	if (VAPicture->pic_fields.bits.pic_order_present_flag)
		pps->flags |=
			V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT;

	if (VAPicture->pic_fields.bits.deblocking_filter_control_present_flag)
		pps->flags |=
			V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT;

	if (VAPicture->pic_fields.bits.redundant_pic_cnt_present_flag)
		pps->flags |= V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT;


	sps->chroma_format_idc = VAPicture->seq_fields.bits.chroma_format_idc;
	sps->bit_depth_luma_minus8 = VAPicture->bit_depth_luma_minus8;
	sps->bit_depth_chroma_minus8 = VAPicture->bit_depth_chroma_minus8;
	sps->log2_max_frame_num_minus4 =
		VAPicture->seq_fields.bits.log2_max_frame_num_minus4;
	sps->pic_order_cnt_type = VAPicture->seq_fields.bits.pic_order_cnt_type;
	sps->log2_max_pic_order_cnt_lsb_minus4 =
		VAPicture->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4;
	sps->max_num_ref_frames = VAPicture->num_ref_frames;
	sps->pic_width_in_mbs_minus1 = VAPicture->picture_width_in_mbs_minus1;
	sps->pic_height_in_map_units_minus1 =
		VAPicture->picture_height_in_mbs_minus1;

	if (VAPicture->seq_fields.bits.residual_colour_transform_flag)
		sps->flags |= V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE;
	if (VAPicture->seq_fields.bits.gaps_in_frame_num_value_allowed_flag)
		sps->flags |=
			V4L2_H264_SPS_FLAG_GAPS_IN_FRAME_NUM_VALUE_ALLOWED;
	if (VAPicture->seq_fields.bits.frame_mbs_only_flag)
		sps->flags |= V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY;
	if (VAPicture->seq_fields.bits.mb_adaptive_frame_field_flag)
		sps->flags |= V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD;
	if (VAPicture->seq_fields.bits.direct_8x8_inference_flag)
		sps->flags |= V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE;
	if (VAPicture->seq_fields.bits.delta_pic_order_always_zero_flag)
		sps->flags |= V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO;
}

static void h264_va_matrix_to_v4l2(RequestData *driver_data,
				   const Context& context,
				   VAIQMatrixBufferH264 *VAMatrix,
				   struct v4l2_ctrl_h264_scaling_matrix *v4l2_matrix)
{
	memcpy(v4l2_matrix->scaling_list_4x4, &VAMatrix->ScalingList4x4,
	       sizeof(VAMatrix->ScalingList4x4));

	/*
	 * In YUV422, there's only two matrices involved, while YUV444
	 * needs 6. However, in the former case, the two matrices
	 * should be placed at the 0 and 3 offsets.
	 */
	memcpy(v4l2_matrix->scaling_list_8x8[0], &VAMatrix->ScalingList8x8[0],
	       sizeof(v4l2_matrix->scaling_list_8x8[0]));
	memcpy(v4l2_matrix->scaling_list_8x8[3], &VAMatrix->ScalingList8x8[1],
	       sizeof(v4l2_matrix->scaling_list_8x8[3]));
}

static void h264_copy_pred_table(struct v4l2_h264_weight_factors *factors,
				 unsigned int num_refs,
				 int16_t luma_weight[32],
				 int16_t luma_offset[32],
				 int16_t chroma_weight[32][2],
				 int16_t chroma_offset[32][2])
{
	unsigned int i;

	for (i = 0; i < num_refs; i++) {
		unsigned int j;

		factors->luma_weight[i] = luma_weight[i];
		factors->luma_offset[i] = luma_offset[i];

		for (j = 0; j < 2; j++) {
			factors->chroma_weight[i][j] = chroma_weight[i][j];
			factors->chroma_offset[i][j] = chroma_offset[i][j];
		}
	}
}

static void h264_va_slice_to_v4l2(RequestData *driver_data,
				  Context& context,
				  VASliceParameterBufferH264 *VASlice,
				  VAPictureParameterBufferH264 *VAPicture,
				  struct v4l2_ctrl_h264_slice_params *slice)
{
	slice->header_bit_size = VASlice->slice_data_bit_offset;
	slice->first_mb_in_slice = VASlice->first_mb_in_slice;
	slice->slice_type = VASlice->slice_type;
	slice->cabac_init_idc = VASlice->cabac_init_idc;
	slice->slice_qp_delta = VASlice->slice_qp_delta;
	slice->disable_deblocking_filter_idc =
		VASlice->disable_deblocking_filter_idc;
	slice->slice_alpha_c0_offset_div2 = VASlice->slice_alpha_c0_offset_div2;
	slice->slice_beta_offset_div2 = VASlice->slice_beta_offset_div2;

	if (((VASlice->slice_type % 5) == H264_SLICE_P) ||
	    ((VASlice->slice_type % 5) == H264_SLICE_B)) {
		slice->num_ref_idx_l0_active_minus1 =
			VASlice->num_ref_idx_l0_active_minus1;

		for (int i = 0; i < VASlice->num_ref_idx_l0_active_minus1 + 1; i++) {
			VAPictureH264 *pic = &VASlice->RefPicList0[i];
			struct h264_dpb_entry *entry;
			struct v4l2_h264_reference ref;

			entry = dpb_lookup(context, pic, &ref);
			if (!entry)
				continue;

			slice->ref_pic_list0[i] = ref;
		}
	}

	if ((VASlice->slice_type % 5) == H264_SLICE_B) {
		slice->num_ref_idx_l1_active_minus1 =
			VASlice->num_ref_idx_l1_active_minus1;

		for (int i = 0; i < VASlice->num_ref_idx_l1_active_minus1 + 1; i++) {
			VAPictureH264 *pic = &VASlice->RefPicList1[i];
			struct h264_dpb_entry *entry;
			struct v4l2_h264_reference ref;

			entry = dpb_lookup(context, pic, &ref);
			if (!entry)
				continue;

			slice->ref_pic_list1[i] = ref;
		}
	}

	if (VASlice->direct_spatial_mv_pred_flag)
		slice->flags |= V4L2_H264_SLICE_FLAG_DIRECT_SPATIAL_MV_PRED;
}


static void h264_va_slice_to_predicted_weights(
	VASliceParameterBufferH264* VASlice,
	struct v4l2_ctrl_h264_slice_params *slice,
	struct v4l2_ctrl_h264_pred_weights* weights
) {

	weights->chroma_log2_weight_denom = VASlice->chroma_log2_weight_denom;
	weights->luma_log2_weight_denom = VASlice->luma_log2_weight_denom;

	if (((VASlice->slice_type % 5) == H264_SLICE_P) ||
	    ((VASlice->slice_type % 5) == H264_SLICE_B))
		h264_copy_pred_table(&weights->weight_factors[0],
				     slice->num_ref_idx_l0_active_minus1 + 1,
				     VASlice->luma_weight_l0,
				     VASlice->luma_offset_l0,
				     VASlice->chroma_weight_l0,
				     VASlice->chroma_offset_l0);

	if ((VASlice->slice_type % 5) == H264_SLICE_B)
		h264_copy_pred_table(&weights->weight_factors[1],
				     slice->num_ref_idx_l1_active_minus1 + 1,
				     VASlice->luma_weight_l1,
				     VASlice->luma_offset_l1,
				     VASlice->chroma_weight_l1,
				     VASlice->chroma_offset_l1);
}


VAStatus h264_store_buffer(RequestData *driver_data,
				   Surface& surface,
				   const Buffer& buffer)
{
	switch (buffer.type) {
	case VASliceDataBufferType:
		/*
		 * Since there is no guarantee that the allocation
		 * order is the same as the submission order (via
		 * RenderPicture), we can't use a V4L2 buffer directly
		 * and have to copy from a regular buffer.
		 */
		surface.source_size_used += prefix_data(
			surface.source_data.data() + surface.source_size_used
		);

		if (surface.source_size_used + buffer.size * buffer.count > surface.source_data.size()) {
			return VA_STATUS_ERROR_NOT_ENOUGH_BUFFER;
		}
		memcpy(surface.source_data.data() +
			       surface.source_size_used,
		       buffer.data.get(),
		       buffer.size * buffer.count);
		surface.source_size_used +=
			buffer.size * buffer.count;
		break;

	case VAPictureParameterBufferType:
		surface.params.h264.picture = reinterpret_cast<VAPictureParameterBufferH264*>(buffer.data.get());
		break;

	case VASliceParameterBufferType:
		surface.params.h264.slice = reinterpret_cast<VASliceParameterBufferH264*>(buffer.data.get());
		break;

	case VAIQMatrixBufferType:
		surface.params.h264.matrix = reinterpret_cast<VAIQMatrixBufferH264*>(buffer.data.get());
		break;

	default:
		return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
	}

	return VA_STATUS_SUCCESS;
}


int h264_set_controls(RequestData *driver_data,
		      Context& context,
		      Surface& surface)
{
	if (!driver_data->configs.contains(context.config_id)) {
		return VA_STATUS_ERROR_INVALID_CONFIG;
	}
	auto& config = driver_data->configs.at(context.config_id);

	struct v4l2_ctrl_h264_scaling_matrix matrix = {};
	struct v4l2_ctrl_h264_decode_params decode = {};
	struct v4l2_ctrl_h264_slice_params slice = {};
	struct v4l2_ctrl_h264_pps pps = {};
	struct v4l2_ctrl_h264_sps sps = {};
	struct h264_dpb_entry *output;
	struct v4l2_ext_control controls[8] = {};
	int i = 0;

	output = dpb_lookup(context, &surface.params.h264.picture->CurrPic,
			    NULL);
	if (!output)
		output = dpb_find_entry(context);

	dpb_clear_entry(output, true);

	dpb_update(context, surface.params.h264.picture);

	h264_va_picture_to_v4l2(driver_data, context,
				surface.params.h264.picture,
				&decode, &pps, &sps);
	h264_va_matrix_to_v4l2(driver_data, context,
			       surface.params.h264.matrix, &matrix);
	h264_va_slice_to_v4l2(driver_data, context,
			      surface.params.h264.slice,
			      surface.params.h264.picture, &slice);

	sps.profile_idc = va_profile_to_profile_idc(config.profile);
	switch (surface.params.h264.slice->slice_type % 5) {
		case H264_SLICE_P:
			decode.flags |= V4L2_H264_DECODE_PARAM_FLAG_PFRAME;
			break;
		case H264_SLICE_B:
			decode.flags |= V4L2_H264_DECODE_PARAM_FLAG_BFRAME;
			break;
	}

	if (V4L2_H264_CTRL_PRED_WEIGHTS_REQUIRED(&pps, &slice)) {
		struct v4l2_ctrl_h264_pred_weights weights = {};
		h264_va_slice_to_predicted_weights(surface.params.h264.slice, &slice, &weights);

		controls[i++] = (struct v4l2_ext_control){
			.id = V4L2_CID_STATELESS_H264_PRED_WEIGHTS,
			.size = sizeof(weights),
			.ptr = &weights,
		};
	}

	controls[i++] = (struct v4l2_ext_control) {
		.id = V4L2_CID_STATELESS_H264_DECODE_PARAMS,
		.size = sizeof(decode),
		.ptr = &decode,
	};

	controls[i++] = (struct v4l2_ext_control){
		.id = V4L2_CID_STATELESS_H264_PPS,
		.size = sizeof(pps),
		.ptr = &pps,
	};

	controls[i++] = (struct v4l2_ext_control){
		.id = V4L2_CID_STATELESS_H264_SPS,
		.size = sizeof(sps),
		.ptr = &sps,
	};

	controls[i++] = (struct v4l2_ext_control){
		.id = V4L2_CID_STATELESS_H264_SCALING_MATRIX,
		.size = sizeof(matrix),
		.ptr = &matrix,
	};

	try {
		driver_data->device.set_controls(surface.request_fd, std::span(controls, i));
	} catch(std::runtime_error& e) {
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	dpb_insert(context, &surface.params.h264.picture->CurrPic, output);

	return VA_STATUS_SUCCESS;
}
