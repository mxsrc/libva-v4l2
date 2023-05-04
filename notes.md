# H264
## Trivial:
- `V4L2_PIX_FMT_H264_SLICE_RAW` -> `V4L2_PIX_FMT_H264_SLICE`

## Non-trivial:
### symptomatic:
- `struct v4l2_ctrl_h264_slice_params`:
  - dropped: `size`, `pic_parameter_set_id`, `frame_num`, `idr_pic_id`, `pic_order_cnt_lsb`, `delta_pic_order_cnt_bottom`, `delta_pic_order_cnt0`, `delta_pic_order_cnt1`, `pred_weight_table`, `dec_ref_pic_marking_bit_size`, `pic_order_cnt_bit_size`, `slice_group_change_cycle`
  `- first_mb_in_slice`: u32 -> u16
  - `ref_pic_list{0,1}`: u8 array to `v4l2_h264_reference` array. Previous comment: "indices into `v4l2_ctrl_h264_decode_params.dpb`"
- `struct v4l2_ctrl_h264_decode_params`
  - dpb size: 16 -> `V4L2_H264_NUM_DPB_ENTRIES`
  - dropped: `num_slices`, `ref_pic_list_p0`, `ref_pic_list_b0`, `ref_pic_list_b1`
  - added: `frame_num`, `idr_pic_id`, `pic_order_cnt_lsb`, `delta_pic_order_cnt_bottom`, `delta_pic_order_cnt0`, `delta_pic_order_cnt1`, `dec_ref_pic_marking_bit_size`, `pic_order_cnt_bit_size`, `slice_group_change_cycle`
- `V4L2_CID_MPEG_VIDEO_H264_*` -> `V4L2_CID_STATELESS_H264_*`

### asymptomatic(?):
- `struct v4l2_h264_dpb_entry`:
  - `pic_num`: u16 -> u32
  - fields added. Previous comment "fields indicated by `v4l2_buffer.field`"

## Unrelated:
- `V4L2_H264_PPS_FLAG_PIC_SCALING_MATRIX_PRESENT` -> `V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT`
- `v4l2_h264_pred_weight_table` -> `v4l2_h264_pred_weights`
- `V4L2_CTRL_TYPE_H264_*`:
  - define -> `enum v4l2_ctrl_type`
  - values changes
- scope of constants seemingly changed?:
  - moved to different bitmask
    - `V4L2_H264_SLICE_FLAG_FIELD_PIC` -> `V4L2_264_DECODE_FLAG_FIELD_PIC`
    - `V4L2_H264_SLICE_FLAG_BOTTOM_FIELD` -> `V4L2_264_DECODE_FLAG_BOTTOM_FIELD`
  - values changed (due to previous bullet point?):
    - `V4L2_H264_SLICE_FLAG_DIRECT_SPATIAL_MV_PRED`
    - `V4L2_H264_SLICE_FLAG_SP_FOR_SWITCH`

# HEVC
- `struct v4l2_ctrl_hevc_sps`
    - dropped: `separate_color_plane_idc`, `scaling_list_enabled_flag`, `amp_enabled_flag`, `sample_adaptive_offset_enabled_flag`, `pcm_enabled_flag`, `pcm_loop_filter_disabled_flag`, `long_term_ref_pics_present_flag`, `sps_temporal_mvp_enabled_flag`, `strong_intra_smoothing_enabled_flag`
    - added: `video_parameter_set_id`, `seq_parameter_set_id`, `chroma_format_idc`, `sps_max_sub_layers_minus1`, `flags`
- `struct v4l2_ctrl_hevc_pps`
    - individual fields aggregated as flags?
    - dropped: `dependent_slice_segment_flag`, `output_flag_present_flag`, `sign_data_hiding_enabled_flag`, `cabac_init_present_flag`, `constrained_intra_pred_flag`, `transform_skip_enabled_flag`, `cu_qp_delta_enabled_flag`, `pps_slice_chroma_qp_offsets_present_flag`, `weighted_pred_flag`, `weighted_bipred_flag`, `transquant_bypass_enabled_flag`, `tiles_enabled_flag`, `entropy_coding_sync_enabled_flag`, `loop_filter_across_tiles_enabled_flag`, `pps_loop_filter_across_slices_enabled_flag`, `deblocking_filter_override_enabled_flag`, `pps_disable_deblocking_filter_flag`, `lists_modification_present_flag`, `slice_segment_header_extension_present_flag`, `slice_segment_header_extension_present_flag`
    - added: `slice_segment_header_extension_present_flag`, `num_ref_idx_l0_default_active_minus1`, `num_ref_idx_l1_default_active_minus1`, `flags`
- `struct v4l2_hevc_dpb_entry`:
  - dropped: `rps`
  - added: `flags`
  - changed: `pic_order_cnt[2]` -> `pic_order_cnt_val`
- `struct v4l2_ctrl_hevc_slice_params`:
  - flags aggregated (`slice_sao_luma_flag`, `slice_sao_chroma_flag`, `slice_temporall_mvp_enabled_flag`, `mvd_l1_zero_flag`, `cabac_init_flag`, `collocated_from_l0_flag`, `use_integer_mv_flag`, `slice_deblocking_filter_disabled_flag`, `slice_loop_filter_across_slices_enabled_flag` -> `flags`)
  - `data_bit_offset` -> `data_byte_offset`
  - `num_rps_poc_st_curr_before` & `num_rps_poc_st_curr_after` -> `short_term_ref_pic_set_size`?
  - `num_rps_poc_lt_curr` -> `long_term_ref_pic_set_size`
  - dropped: `dpb`, `num_active_dpb_entries`, , 
  - added: `num_entry_point_offsets`, `slice_segment_addr`
  - changed: `slice_pic_order_cnt` u16 -> s32
- `V4L2_CTRL_TYPE_HEVC_*` values changed
- `V4L2_CID_MPEG_VIDEO_HEVC_*` -> `V4L2_CID_STATELESS_HEVC_*`
- missing
  - `V4L2_HEVC_DPB_ENTRY_RPS_ST_CURR_BEFORE`, `V4L2_HEVC_DPB_ENTRY_RPS_ST_CURR_AFTER`, `V4L2_HEVC_DPB_ENTRY_RPS_LT_CURR`
    - c.f. `struct v4l2_ctrl_hevc_slice_params` changes


# MPEG
- `struct v4l2_mpeg2_sequence`
  - dropped: `progressive_sequence`
  - added: `flags`
- `struct v4l2_ctrl_mpeg2_picture`
  - changed completely?
  -
```
struct v4l2_mpeg2_picture {
	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Picture header */
	__u8	picture_coding_type;

	/* ISO/IEC 13818-2, ITU-T Rec. H.262: Picture coding extension */
	__u8	f_code[2][2];
	__u8	intra_dc_precision;
	__u8	picture_structure;
	__u8	top_field_first;
	__u8	frame_pred_frame_dct;
	__u8	concealment_motion_vectors;
	__u8	q_scale_type;
	__u8	intra_vlc_format;
	__u8	alternate_scan;
	__u8	repeat_first_field;
	__u16	progressive_frame;
};
```
to
```
struct v4l2_ctrl_mpeg2_picture {
	__u64	backward_ref_ts;
	__u64	forward_ref_ts;
	__u32	flags;
	__u8	f_code[2][2];
	__u8	picture_coding_type;
	__u8	picture_structure;
	__u8	intra_dc_precision;
	__u8	reserved[5];
};
```

- `V4L2_MPEG2_PICTURE_CODING_TYPE_*` -> `V4L2_MPEG2_PIC_CODING_TYPE_P`
- `v4l2_ctrl_mpeg2_quantization` -> `v4l2_ctrl_mpeg2_quantisation`
  - added load flags for each matrix
- slice params gone?
  - no `v4l2_ctrl_mpeg2_slice_params` struct, not constants
- `V4L2_CID_MPEG_VIDEO_MPEG2_QUANTIZATION` -> `V4L2_CID_STATELESS_MPEG2_QUANTISATION`
- `V4L2_CTRL_TYPE_MPEG2_*` gone

# DRM
- `DRM_FORMAT_MOD_NONE`
- `DRM_FORMAT_MOD_NVIDIA` changed?
- `DRM_FORMAT_MOD_ARM_AFBC` implementation changed?
