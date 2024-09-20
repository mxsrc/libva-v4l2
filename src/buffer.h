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

#include <cstdint>
#include <memory>

extern "C" {
#include <va/va.h>
#include <va/va_backend.h>
}

struct Buffer {
	Buffer(VABufferType type, unsigned count, unsigned size, VASurfaceID derived_surface_id);

	VABufferType type;
	unsigned initial_count;
	unsigned count;
	std::unique_ptr<uint8_t> data;
	unsigned int size;
	VASurfaceID derived_surface_id;
	VABufferInfo info;
};

VAStatus RequestCreateBuffer(VADriverContextP context, VAContextID context_id,
			     VABufferType type, unsigned int size,
			     unsigned int count, void *data,
			     VABufferID *buffer_id);
VAStatus RequestDestroyBuffer(VADriverContextP context, VABufferID buffer_id);
VAStatus RequestMapBuffer(VADriverContextP context, VABufferID buffer_id,
			  void **data_map);
VAStatus RequestUnmapBuffer(VADriverContextP context, VABufferID buffer_id);
VAStatus RequestBufferSetNumElements(VADriverContextP context,
				     VABufferID buffer_id, unsigned int count);
VAStatus RequestBufferInfo(VADriverContextP context, VABufferID buffer_id,
			   VABufferType *type, unsigned int *size,
			   unsigned int *count);
VAStatus RequestAcquireBufferHandle(VADriverContextP context,
				    VABufferID buffer_id,
				    VABufferInfo *buffer_info);
VAStatus RequestReleaseBufferHandle(VADriverContextP context,
	VABufferID buffer_id);
