/*
 * Copyright (C) 2007 Intel Corporation
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
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

#include "mpeg2.h"
#include "context.h"
#include "request.h"
#include "surface.h"

#include <assert.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/videodev2.h>

#include "v4l2.h"

#define H265_NAL_UNIT_TYPE_SHIFT		1
#define H265_NAL_UNIT_TYPE_MASK			((1 << 6) - 1)
#define H265_NUH_TEMPORAL_ID_PLUS1_SHIFT	0
#define H265_NUH_TEMPORAL_ID_PLUS1_MASK		((1 << 3) - 1)

static void h265_fill_pps(VAPictureParameterBufferHEVC *picture,
			  VASliceParameterBufferHEVC *slice,
			  struct v4l2_ctrl_hevc_pps *pps)
{
	memset(pps, 0, sizeof(*pps));

	pps->num_extra_slice_header_bits =
		picture->num_extra_slice_header_bits;
	pps->init_qp_minus26 = picture->init_qp_minus26;
	pps->diff_cu_qp_delta_depth = picture->diff_cu_qp_delta_depth;
	pps->pps_cb_qp_offset = picture->pps_cb_qp_offset;
	pps->pps_cr_qp_offset = picture->pps_cr_qp_offset;
	pps->num_tile_columns_minus1 = picture->num_tile_columns_minus1;
	pps->num_tile_rows_minus1 = picture->num_tile_rows_minus1;
	pps->pps_beta_offset_div2 = picture->pps_beta_offset_div2;
	pps->pps_tc_offset_div2 = picture->pps_tc_offset_div2;
	pps->log2_parallel_merge_level_minus2 =
		picture->log2_parallel_merge_level_minus2;

	// TODO:
	// - does the `slice->LongSliceFlags.fields.slice_deblocking_filter_disabled_flag` corrsepond to `V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT`?
	// 	- the relevant linux commit also introduces the the `num_ref_idx_l{0,1}_active_minus1` fields.
	// - VA comment mentions `uniform_spacing_flag`, but it's not present. Might correspond to `V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING`?
	pps->flags = (
		(slice->LongSliceFlags.fields.dependent_slice_segment_flag ? V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED : 0) |
		(picture->slice_parsing_fields.bits.output_flag_present_flag ? V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT : 0) |
		(picture->pic_fields.bits.sign_data_hiding_enabled_flag ? V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED : 0) |
		(picture->slice_parsing_fields.bits.cabac_init_present_flag ? V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT : 0) |
		(picture->pic_fields.bits.constrained_intra_pred_flag ? V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED : 0) |
		(picture->pic_fields.bits.transform_skip_enabled_flag ? V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED : 0) |
		(picture->pic_fields.bits.cu_qp_delta_enabled_flag ? V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED : 0) |
		(picture->slice_parsing_fields.bits.pps_slice_chroma_qp_offsets_present_flag ? V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT : 0) |
		(picture->pic_fields.bits.weighted_pred_flag ? V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED : 0) |
		(picture->pic_fields.bits.weighted_bipred_flag ? V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED : 0) |
		(picture->pic_fields.bits.transquant_bypass_enabled_flag ? V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED : 0) |
		(picture->pic_fields.bits.tiles_enabled_flag ? V4L2_HEVC_PPS_FLAG_TILES_ENABLED : 0) |
		(picture->pic_fields.bits.entropy_coding_sync_enabled_flag ? V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED : 0) |
		(picture->pic_fields.bits.loop_filter_across_tiles_enabled_flag ? V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED : 0) |
		(picture->pic_fields.bits.pps_loop_filter_across_slices_enabled_flag ? V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED : 0) |
		(picture->slice_parsing_fields.bits.deblocking_filter_override_enabled_flag ? V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED : 0) |
		(picture->slice_parsing_fields.bits.pps_disable_deblocking_filter_flag ? V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER : 0) |
		(picture->slice_parsing_fields.bits.lists_modification_present_flag ? V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT : 0) |
		(picture->slice_parsing_fields.bits.slice_segment_header_extension_present_flag ? V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT : 0)
	);
}

static void h265_fill_sps(VAPictureParameterBufferHEVC *picture,
			  struct v4l2_ctrl_hevc_sps *sps)
{
	memset(sps, 0, sizeof(*sps));

	sps->chroma_format_idc = picture->pic_fields.bits.chroma_format_idc;
	sps->pic_width_in_luma_samples = picture->pic_width_in_luma_samples;
	sps->pic_height_in_luma_samples = picture->pic_height_in_luma_samples;
	sps->bit_depth_luma_minus8 = picture->bit_depth_luma_minus8;
	sps->bit_depth_chroma_minus8 = picture->bit_depth_chroma_minus8;
	sps->log2_max_pic_order_cnt_lsb_minus4 =
		picture->log2_max_pic_order_cnt_lsb_minus4;
	sps->sps_max_dec_pic_buffering_minus1 =
		picture->sps_max_dec_pic_buffering_minus1;
	sps->sps_max_num_reorder_pics = 0;
	sps->sps_max_latency_increase_plus1 = 0;
	sps->log2_min_luma_coding_block_size_minus3 =
		picture->log2_min_luma_coding_block_size_minus3;
	sps->log2_diff_max_min_luma_coding_block_size =
		picture->log2_diff_max_min_luma_coding_block_size;
	sps->log2_min_luma_transform_block_size_minus2 =
		picture->log2_min_transform_block_size_minus2;
	sps->log2_diff_max_min_luma_transform_block_size =
		picture->log2_diff_max_min_transform_block_size;
	sps->max_transform_hierarchy_depth_inter =
		picture->max_transform_hierarchy_depth_inter;
	sps->max_transform_hierarchy_depth_intra =
		picture->max_transform_hierarchy_depth_intra;
	sps->pcm_sample_bit_depth_luma_minus1 =
		picture->pcm_sample_bit_depth_luma_minus1;
	sps->pcm_sample_bit_depth_chroma_minus1 =
		picture->pcm_sample_bit_depth_chroma_minus1;
	sps->log2_min_pcm_luma_coding_block_size_minus3 =
		picture->log2_min_pcm_luma_coding_block_size_minus3;
	sps->log2_diff_max_min_pcm_luma_coding_block_size =
		picture->log2_diff_max_min_pcm_luma_coding_block_size;
	sps->num_short_term_ref_pic_sets = picture->num_short_term_ref_pic_sets;
	sps->num_long_term_ref_pics_sps = picture->num_long_term_ref_pic_sps;

	sps->flags = (
		(picture->pic_fields.bits.separate_colour_plane_flag ? V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE : 0) |
		(picture->pic_fields.bits.scaling_list_enabled_flag ? V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED : 0) |
		(picture->pic_fields.bits.amp_enabled_flag ? V4L2_HEVC_SPS_FLAG_AMP_ENABLED : 0) |
		(picture->slice_parsing_fields.bits.sample_adaptive_offset_enabled_flag ? V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET : 0) |
		(picture->pic_fields.bits.pcm_enabled_flag ? V4L2_HEVC_SPS_FLAG_PCM_ENABLED : 0) |
		(picture->pic_fields.bits.pcm_loop_filter_disabled_flag ? V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED : 0) |
		(picture->slice_parsing_fields.bits.long_term_ref_pics_present_flag ? V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT : 0) |
		(picture->slice_parsing_fields.bits.sps_temporal_mvp_enabled_flag ? V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED : 0) |
		(picture->pic_fields.bits.strong_intra_smoothing_enabled_flag ? V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED : 0)
	);
}

static void h265_fill_slice_params(VAPictureParameterBufferHEVC *picture,
				   VASliceParameterBufferHEVC *slice,
				   struct object_heap *surface_heap,
				   void *source_data,
				   struct v4l2_ctrl_hevc_slice_params *slice_params,
				   struct v4l2_ctrl_hevc_decode_params *decode_params)
{
	struct object_surface *surface_object;
	VAPictureHEVC *hevc_picture;
	uint8_t nal_unit_type;
	uint8_t nuh_temporal_id_plus1;
	uint8_t pic_struct;
	uint8_t field_pic;
	uint8_t slice_type;
	unsigned int num_active_dpb_entries;
	unsigned int num_rps_poc_st_curr_before;
	unsigned int num_rps_poc_st_curr_after;
	unsigned int num_rps_poc_lt_curr;
	uint8_t *b;
	unsigned int count;
	unsigned int i, j;

	/* Extract the missing NAL header information. */

	b = source_data + slice->slice_data_offset;

	nal_unit_type = (b[0] >> H265_NAL_UNIT_TYPE_SHIFT) &
			H265_NAL_UNIT_TYPE_MASK;
	nuh_temporal_id_plus1 = (b[1] >> H265_NUH_TEMPORAL_ID_PLUS1_SHIFT) &
				H265_NUH_TEMPORAL_ID_PLUS1_MASK;

	memset(slice_params, 0, sizeof(*slice_params));

	slice_params->bit_size = slice->slice_data_size * 8;
	slice_params->data_byte_offset = slice->slice_data_offset + slice->slice_data_byte_offset;
	slice_params->nal_unit_type = nal_unit_type;
	slice_params->nuh_temporal_id_plus1 = nuh_temporal_id_plus1;

	slice_type = slice->LongSliceFlags.fields.slice_type;

	slice_params->slice_type = slice_type,
	slice_params->colour_plane_id =
		slice->LongSliceFlags.fields.color_plane_id;
	slice_params->slice_pic_order_cnt =
		picture->CurrPic.pic_order_cnt;
	slice_params->num_ref_idx_l0_active_minus1 =
		slice->num_ref_idx_l0_active_minus1;
	slice_params->num_ref_idx_l1_active_minus1 =
		slice->num_ref_idx_l1_active_minus1;
	slice_params->collocated_ref_idx = slice->collocated_ref_idx;
	slice_params->five_minus_max_num_merge_cand =
		slice->five_minus_max_num_merge_cand;
	slice_params->slice_qp_delta = slice->slice_qp_delta;
	slice_params->slice_cb_qp_offset = slice->slice_cb_qp_offset;
	slice_params->slice_cr_qp_offset = slice->slice_cr_qp_offset;
	slice_params->slice_act_y_qp_offset = 0;
	slice_params->slice_act_cb_qp_offset = 0;
	slice_params->slice_act_cr_qp_offset = 0;
	slice_params->slice_beta_offset_div2 = slice->slice_beta_offset_div2;
	slice_params->slice_tc_offset_div2 = slice->slice_tc_offset_div2;

	slice_params->flags = (
		(slice->LongSliceFlags.fields.slice_sao_luma_flag ? V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_LUMA : 0) |
		(slice->LongSliceFlags.fields.slice_sao_chroma_flag ? V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_CHROMA : 0) |
		(slice->LongSliceFlags.fields.slice_temporal_mvp_enabled_flag ? V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED : 0) |
		(slice->LongSliceFlags.fields.mvd_l1_zero_flag ? V4L2_HEVC_SLICE_PARAMS_FLAG_MVD_L1_ZERO : 0) |
		(slice->LongSliceFlags.fields.cabac_init_flag ? V4L2_HEVC_SLICE_PARAMS_FLAG_CABAC_INIT : 0) |
		(slice->LongSliceFlags.fields.collocated_from_l0_flag ? V4L2_HEVC_SLICE_PARAMS_FLAG_COLLOCATED_FROM_L0 : 0) |
		(slice->LongSliceFlags.fields.dependent_slice_segment_flag ? V4L2_HEVC_SLICE_PARAMS_FLAG_DEPENDENT_SLICE_SEGMENT : 0) |
		(false ? V4L2_HEVC_SLICE_PARAMS_FLAG_USE_INTEGER_MV : 0) |  // dead code for consistency and documentation. Present in `VASliceParameterBufferHEVCRext`
		(slice->LongSliceFlags.fields.slice_deblocking_filter_disabled_flag ? V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_DEBLOCKING_FILTER_DISABLED : 0) |
		(slice->LongSliceFlags.fields.slice_loop_filter_across_slices_enabled_flag ? V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED : 0)
	);

	if (picture->CurrPic.flags & VA_PICTURE_HEVC_FIELD_PIC) {
		if (picture->CurrPic.flags & VA_PICTURE_HEVC_BOTTOM_FIELD)
			pic_struct = 2;
		else
			pic_struct = 1;
	} else {
		pic_struct = 0;
	}

	slice_params->pic_struct = pic_struct;

	num_active_dpb_entries = 0;
	num_rps_poc_st_curr_before = 0;
	num_rps_poc_st_curr_after = 0;
	num_rps_poc_lt_curr = 0;

	for (i = 0; i < 15 && slice_type != V4L2_HEVC_SLICE_TYPE_I ; i++) {
		uint64_t timestamp;

		hevc_picture = &picture->ReferenceFrames[i];

		if (hevc_picture->picture_id == VA_INVALID_SURFACE ||
		    (hevc_picture->flags & VA_PICTURE_HEVC_INVALID) != 0)
			break;

		surface_object = (struct object_surface *)
			object_heap_lookup(surface_heap,
					   hevc_picture->picture_id);
		if (surface_object == NULL)
			break;

		timestamp = v4l2_timeval_to_ns(&surface_object->timestamp);
		decode_params->dpb[i].timestamp = timestamp;

		if ((hevc_picture->flags & VA_PICTURE_HEVC_RPS_ST_CURR_BEFORE) != 0) {
			decode_params->poc_st_curr_before[i] = true;
			num_rps_poc_st_curr_before++;
		} else if ((hevc_picture->flags & VA_PICTURE_HEVC_RPS_ST_CURR_AFTER) != 0) {
			decode_params->poc_st_curr_after[i] = true;
			num_rps_poc_st_curr_after++;
		} else if ((hevc_picture->flags & VA_PICTURE_HEVC_RPS_LT_CURR) != 0) {
			decode_params->poc_lt_curr[i] = true;
			num_rps_poc_lt_curr++;
		}

		field_pic = !!(hevc_picture->flags & VA_PICTURE_HEVC_FIELD_PIC);

		decode_params->dpb[i].field_pic = field_pic;

		/* TODO: Interleaved: Get the POC for each field. */
		decode_params->dpb[i].pic_order_cnt_val = hevc_picture->pic_order_cnt;

		num_active_dpb_entries++;
	}

	decode_params->num_active_dpb_entries = num_active_dpb_entries;

	count = slice_params->num_ref_idx_l0_active_minus1 + 1;

	for (i = 0; i < count && slice_type != V4L2_HEVC_SLICE_TYPE_I; i++)
		slice_params->ref_idx_l0[i] = slice->RefPicList[0][i];

	count = slice_params->num_ref_idx_l1_active_minus1 + 1;

	for (i = 0; i < count && slice_type == V4L2_HEVC_SLICE_TYPE_B ; i++)
		slice_params->ref_idx_l1[i] = slice->RefPicList[1][i];

	decode_params->num_poc_st_curr_before = num_rps_poc_st_curr_before;
	decode_params->num_poc_st_curr_after = num_rps_poc_st_curr_after;
	decode_params->num_poc_lt_curr = num_rps_poc_lt_curr;

	slice_params->pred_weight_table.luma_log2_weight_denom =
		slice->luma_log2_weight_denom;
	slice_params->pred_weight_table.delta_chroma_log2_weight_denom =
		slice->delta_chroma_log2_weight_denom;

	for (i = 0; i < 15 && slice_type != V4L2_HEVC_SLICE_TYPE_I; i++) {
		slice_params->pred_weight_table.delta_luma_weight_l0[i] =
			slice->delta_luma_weight_l0[i];
		slice_params->pred_weight_table.luma_offset_l0[i] =
			slice->luma_offset_l0[i];

		for (j = 0; j < 2; j++) {
			slice_params->pred_weight_table.delta_chroma_weight_l0[i][j] =
				slice->delta_chroma_weight_l0[i][j];
			slice_params->pred_weight_table.chroma_offset_l0[i][j] =
				slice->ChromaOffsetL0[i][j];
		}
	}

	for (i = 0; i < 15 && slice_type == V4L2_HEVC_SLICE_TYPE_B; i++) {
		slice_params->pred_weight_table.delta_luma_weight_l1[i] =
			slice->delta_luma_weight_l1[i];
		slice_params->pred_weight_table.luma_offset_l1[i] =
			slice->luma_offset_l1[i];

		for (j = 0; j < 2; j++) {
			slice_params->pred_weight_table.delta_chroma_weight_l1[i][j] =
				slice->delta_chroma_weight_l1[i][j];
			slice_params->pred_weight_table.chroma_offset_l1[i][j] =
				slice->ChromaOffsetL1[i][j];
		}
	}
}

int h265_set_controls(struct request_data *driver_data,
		      struct object_context *context_object,
		      struct object_surface *surface_object)
{
	VAPictureParameterBufferHEVC *picture =
		&surface_object->params.h265.picture;
	VASliceParameterBufferHEVC *slice =
		&surface_object->params.h265.slice;
	struct v4l2_ctrl_hevc_pps pps;
	struct v4l2_ctrl_hevc_sps sps;
	struct v4l2_ctrl_hevc_slice_params slice_params;
	struct v4l2_ctrl_hevc_decode_params decode_params = { 0 };
	int rc;

	h265_fill_pps(picture, slice, &pps);

	rc = v4l2_set_control(driver_data->video_fd, surface_object->request_fd,
			      V4L2_CID_STATELESS_HEVC_PPS, &pps, sizeof(pps));
	if (rc < 0)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	h265_fill_sps(picture, &sps);

	rc = v4l2_set_control(driver_data->video_fd, surface_object->request_fd,
			      V4L2_CID_STATELESS_HEVC_SPS, &sps, sizeof(sps));
	if (rc < 0)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	h265_fill_slice_params(picture, slice, &driver_data->surface_heap,
			       surface_object->source_data, &slice_params, &decode_params);

	rc = v4l2_set_control(driver_data->video_fd, surface_object->request_fd,
			      V4L2_CID_STATELESS_HEVC_SLICE_PARAMS,
			      &slice_params, sizeof(slice_params));
	if (rc < 0)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	return 0;
}
