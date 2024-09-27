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

#pragma once

#include <vector>

extern "C" {
#include <va/va.h>
}

#include "surface.h"

struct Buffer;
struct Context;
struct RequestData;
class V4L2M2MDevice;

#define H264_DPB_SIZE 16

struct h264_dpb_entry {
	VAPictureH264 pic;
	unsigned int age;
	bool used;
	bool valid;
	bool reserved;
};

struct h264_dpb {
	h264_dpb_entry entries[H264_DPB_SIZE];
	unsigned int age;
};

VAStatus h264_store_buffer(RequestData *driver_data,
				   Surface& surface,
				   const Buffer& buffer);
int h264_set_controls(RequestData *data,
		      Context& context,
		      Surface& surface);
std::vector<VAProfile> h264_supported_profiles(const V4L2M2MDevice& device);
