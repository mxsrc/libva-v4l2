/*
 * Copyright (C) 2007 Intel Corporation
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
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

extern "C" {
#include <va/va_backend.h>
}

VAStatus createSubpicture(VADriverContextP context, VAImageID image_id, VASubpictureID* subpicture_id);
VAStatus destroySubpicture(VADriverContextP context, VASubpictureID subpicture_id);
VAStatus querySubpictureFormats(
    VADriverContextP context, VAImageFormat* formats, unsigned int* flags, unsigned int* formats_count);
VAStatus setSubpictureImage(VADriverContextP context, VASubpictureID subpicture_id, VAImageID image_id);
VAStatus setSubpicturePalette(VADriverContextP context, VASubpictureID subpicture_id, unsigned char* palette);
VAStatus setSubpictureChromakey(VADriverContextP context, VASubpictureID subpicture_id, unsigned int chromakey_min,
    unsigned int chromakey_max, unsigned int chromakey_mask);
VAStatus setSubpictureGlobalAlpha(VADriverContextP context, VASubpictureID subpicture_id, float global_alpha);
VAStatus associateSubpicture(VADriverContextP context, VASubpictureID subpicture_id, VASurfaceID* surfaces_ids,
    int surfaces_count, short src_x, short src_y, unsigned short src_width, unsigned short src_height, short dst_x,
    short dst_y, unsigned short dst_width, unsigned short dst_height, unsigned int flags);
VAStatus deassociateSubpicture(
    VADriverContextP context, VASubpictureID subpicture_id, VASurfaceID* surfaces_ids, int surfaces_count);
