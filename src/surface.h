/*
 * Copyright (C) 2007 Intel Corporation
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
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

#pragma once

#include <span>
#include <vector>

extern "C" {
#include <linux/videodev2.h>

#include <va/va_backend.h>
}

#include "video.h"

struct RequestData;

struct Surface {
    VASurfaceStatus status;
    unsigned width;
    unsigned height;

    unsigned int source_index;
    unsigned int source_size_used;

    unsigned int destination_index;
    BufferLayout logical_destination_layout;

    timeval timestamp;

    union {
        struct {
            VAPictureParameterBufferMPEG2* picture;
            VASliceParameterBufferMPEG2* slice;
            VAIQMatrixBufferMPEG2* iqmatrix;
        } mpeg2;
        struct {
            VAIQMatrixBufferH264* matrix;
            VAPictureParameterBufferH264* picture;
            VASliceParameterBufferH264* slice;
        } h264;
        struct {
            VAPictureParameterBufferVP8* picture;
            VASliceParameterBufferVP8* slice;
            VAProbabilityDataBufferVP8* probabilities;
            VAIQMatrixBufferVP8* iqmatrix;
        } vp8;
        struct {
            VADecPictureParameterBufferVP9* picture;
            VASliceParameterBufferVP9* slice;
        } vp9;
    } params;

    int request_fd;
};

void RequestCreateSurfacesDeferred(RequestData* driver_data, std::span<VASurfaceID> surface_ids);
VAStatus RequestCreateSurfaces2(VADriverContextP context, unsigned int format, unsigned int width, unsigned int height,
    VASurfaceID* surfaces_ids, unsigned int surfaces_count, VASurfaceAttrib* attributes, unsigned int attributes_count);
VAStatus RequestCreateSurfaces(
    VADriverContextP context, int width, int height, int format, int surfaces_count, VASurfaceID* surfaces_ids);
VAStatus RequestDestroySurfaces(VADriverContextP context, VASurfaceID* surfaces_ids, int surfaces_count);
VAStatus RequestSyncSurface(VADriverContextP context, VASurfaceID surface_id);
VAStatus RequestQuerySurfaceAttributes(
    VADriverContextP context, VAConfigID config, VASurfaceAttrib* attributes, unsigned int* attributes_count);
VAStatus RequestQuerySurfaceStatus(VADriverContextP context, VASurfaceID surface_id, VASurfaceStatus* status);
VAStatus RequestPutSurface(VADriverContextP context, VASurfaceID surface_id, void* draw, short src_x, short src_y,
    unsigned short src_width, unsigned short src_height, short dst_x, short dst_y, unsigned short dst_width,
    unsigned short dst_height, VARectangle* cliprects, unsigned int cliprects_count, unsigned int flags);
VAStatus RequestLockSurface(VADriverContextP context, VASurfaceID surface_id, unsigned int* fourcc,
    unsigned int* luma_stride, unsigned int* chroma_u_stride, unsigned int* chroma_v_stride, unsigned int* luma_offset,
    unsigned int* chroma_u_offset, unsigned int* chroma_v_offset, unsigned int* buffer_name, void** buffer);
VAStatus RequestUnlockSurface(VADriverContextP context, VASurfaceID surface_id);
VAStatus RequestExportSurfaceHandle(
    VADriverContextP context, VASurfaceID surface_id, uint32_t mem_type, uint32_t flags, void* descriptor);
