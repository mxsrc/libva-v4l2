/*
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

#include "video.h"

#include <cstdlib>
#include <cstring>

extern "C" {
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <libdrm/drm_fourcc.h>
}

namespace {

BufferLayout nv12_derive_layout(unsigned width, unsigned height)
{
    auto base_size = width * height;
    return BufferLayout {
        { 0, base_size, width, 0 },
        { 0, base_size / 2, width, base_size },
    };
}

const video_format formats[] = {
    {
        .description = "NV12 YUV",
        .v4l2_format = V4L2_PIX_FMT_NV12,
        .drm_format = DRM_FORMAT_NV12,
        .drm_modifier = DRM_FORMAT_MOD_NONE,
        .derive_layout = &nv12_derive_layout,
    },
    {
        .description = "NV12 YUV non contiguous",
        .v4l2_format = V4L2_PIX_FMT_NV12M,
        .drm_format = DRM_FORMAT_NV12,
        .drm_modifier = DRM_FORMAT_MOD_NONE,
    },
};

const unsigned int formats_count = sizeof(formats) / sizeof(formats[0]);

} // namespace

const video_format* video_format_find(unsigned int pixelformat)
{
    unsigned int i;

    for (i = 0; i < formats_count; i++)
        if (formats[i].v4l2_format == pixelformat)
            return &formats[i];

    return NULL;
}
