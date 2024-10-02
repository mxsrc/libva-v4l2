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

extern "C" {
#include <va/va_backend.h>
}

VAStatus createImage(VADriverContextP context, VAImageFormat* format, int width, int height, VAImage* image);
VAStatus destroyImage(VADriverContextP context, VAImageID image_id);
VAStatus deriveImage(VADriverContextP context, VASurfaceID surface_id, VAImage* image);
VAStatus queryImageFormats(VADriverContextP context, VAImageFormat* formats, int* formats_count);
VAStatus setImagePalette(VADriverContextP context, VAImageID image_id, unsigned char* palette);
VAStatus getImage(VADriverContextP context, VASurfaceID surface_id, int x, int y, unsigned int width,
    unsigned int height, VAImageID image_id);
VAStatus putImage(VADriverContextP context, VASurfaceID surface_id, VAImageID image, int src_x, int src_y,
    unsigned int src_width, unsigned int src_height, int dst_x, int dst_y, unsigned int dst_width,
    unsigned int dst_height);
