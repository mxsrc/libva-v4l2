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

#pragma once

#include <span>
#include <vector>

extern "C" {
#include <linux/videodev2.h>

#include <va/va_backend.h>
}

struct Surface {
	VASurfaceStatus status;
	unsigned width;
	unsigned height;

	unsigned int source_index;
	std::span<uint8_t> source_data;
	unsigned int source_size_used;

	unsigned int destination_index;
	std::vector<std::span<uint8_t>> destination_plane_data;

	unsigned int destination_logical_plane_index[VIDEO_MAX_PLANES];
	unsigned int destination_logical_plane_size[VIDEO_MAX_PLANES];
	unsigned int destination_logical_plane_pitch[VIDEO_MAX_PLANES];
	unsigned int destination_logical_plane_offset[VIDEO_MAX_PLANES];
	unsigned int destination_logical_planes_count;

	struct timeval timestamp;

	union {
		struct {
			VAPictureParameterBufferMPEG2 picture;
			VASliceParameterBufferMPEG2 slice;
			VAIQMatrixBufferMPEG2 iqmatrix;
			bool iqmatrix_set;
		} mpeg2;
		struct {
			VAIQMatrixBufferH264 matrix;
			VAPictureParameterBufferH264 picture;
			VASliceParameterBufferH264 slice;
		} h264;
		struct {
			VAPictureParameterBufferVP8 picture;
			VASliceParameterBufferVP8 slice;
			VAProbabilityDataBufferVP8 probabilities;
			VAIQMatrixBufferVP8 iqmatrix;
		} vp8;
		struct {
			VADecPictureParameterBufferVP9 picture;
			VASliceParameterBufferVP9 slice;
		} vp9;
	} params;

	int request_fd;
};

VAStatus RequestCreateSurfacesReally(VADriverContextP context, VASurfaceID *surfaces_ids, unsigned int surfaces_count);
VAStatus RequestCreateSurfaces2(VADriverContextP context, unsigned int format,
				unsigned int width, unsigned int height,
				VASurfaceID *surfaces_ids,
				unsigned int surfaces_count,
				VASurfaceAttrib *attributes,
				unsigned int attributes_count);
VAStatus RequestCreateSurfaces(VADriverContextP context, int width, int height,
			       int format, int surfaces_count,
			       VASurfaceID *surfaces_ids);
VAStatus RequestDestroySurfaces(VADriverContextP context,
				VASurfaceID *surfaces_ids, int surfaces_count);
VAStatus RequestSyncSurface(VADriverContextP context, VASurfaceID surface_id);
VAStatus RequestQuerySurfaceAttributes(VADriverContextP context,
				       VAConfigID config,
				       VASurfaceAttrib *attributes,
				       unsigned int *attributes_count);
VAStatus RequestQuerySurfaceStatus(VADriverContextP context,
				   VASurfaceID surface_id,
				   VASurfaceStatus *status);
VAStatus RequestPutSurface(VADriverContextP context, VASurfaceID surface_id,
			   void *draw, short src_x, short src_y,
			   unsigned short src_width, unsigned short src_height,
			   short dst_x, short dst_y, unsigned short dst_width,
			   unsigned short dst_height, VARectangle *cliprects,
			   unsigned int cliprects_count, unsigned int flags);
VAStatus RequestLockSurface(VADriverContextP context, VASurfaceID surface_id,
			    unsigned int *fourcc, unsigned int *luma_stride,
			    unsigned int *chroma_u_stride,
			    unsigned int *chroma_v_stride,
			    unsigned int *luma_offset,
			    unsigned int *chroma_u_offset,
			    unsigned int *chroma_v_offset,
			    unsigned int *buffer_name, void **buffer);
VAStatus RequestUnlockSurface(VADriverContextP context, VASurfaceID surface_id);
VAStatus RequestExportSurfaceHandle(VADriverContextP context,
				    VASurfaceID surface_id, uint32_t mem_type,
				    uint32_t flags, void *descriptor);
