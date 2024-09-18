/*
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

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <libdrm/drm_fourcc.h>
#include <linux/videodev2.h>

#include "utils.h"
#include "video.h"

static void nv12_derive_layout(unsigned width, unsigned height, unsigned size[VIDEO_MAX_PLANES], unsigned pitch[VIDEO_MAX_PLANES], unsigned* planes_count) {
	size[0] = width * height;
	pitch[0] = width;

	size[1] = size[0] / 2;
	pitch[1] = width;  // Half the resolution but corresponds to two rows of pixels.

	*planes_count = 2;
}

static const struct video_format formats[] = {
	{
		.description		= "NV12 YUV",
		.v4l2_format		= V4L2_PIX_FMT_NV12,
		.drm_format		= DRM_FORMAT_NV12,
		.drm_modifier		= DRM_FORMAT_MOD_NONE,
		.derive_layout		= &nv12_derive_layout,
	},
	{
		.description		= "NV12 YUV non contiguous",
		.v4l2_format		= V4L2_PIX_FMT_NV12M,
		.drm_format		= DRM_FORMAT_NV12,
		.drm_modifier		= DRM_FORMAT_MOD_NONE,
	},
};

static const unsigned int formats_count = sizeof(formats) / sizeof(formats[0]);

const struct video_format *video_format_find(unsigned int pixelformat)
{
	unsigned int i;

	for (i = 0; i < formats_count; i++)
		if (formats[i].v4l2_format == pixelformat)
			return &formats[i];

	return NULL;
}
