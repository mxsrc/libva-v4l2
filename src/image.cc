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

#include "image.h"

#include <cassert>
#include <cstring>

extern "C" {
#include <linux/videodev2.h>
}

#include "buffer.h"
#include "request.h"
#include "surface.h"
#include "v4l2.h"
#include "video.h"

VAStatus RequestCreateImage(VADriverContextP context, VAImageFormat *format,
			    int width, int height, VAImage *image)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	unsigned int destination_sizes[VIDEO_MAX_PLANES];
	struct object_image *image_object;
	VABufferID buffer_id;
	VAImageID id;
	VAStatus status;
	unsigned int i;

	if (!driver_data->video_format)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	memset(image, 0, sizeof(*image));
	image->format = *format;
	image->width = width;
	image->height = height;

	struct v4l2_pix_format_mplane* driver_format = &driver_data->device.capture_format.fmt.pix_mp;
	if (driver_data->video_format->derive_layout) {
		driver_data->video_format->derive_layout(
				driver_format->width, driver_format->height,  // Have to use the driver format to get the actual height, which may differ due to block alignment.
				destination_sizes, image->pitches, &image->num_planes);
	} else {
		image->num_planes = driver_format->num_planes;
		for (unsigned i = 0; i < image->num_planes; i += 1) {
			destination_sizes[i] = driver_format->plane_fmt[i].sizeimage;
			image->pitches[i] = driver_format->plane_fmt[i].bytesperline;
		}
	}

	for (i = 0; i < image->num_planes; i++) {
		image->data_size += destination_sizes[i];  // The size returned by V4L2 covers buffers, not logical planes.
		image->offsets[i] = i > 0 ? destination_sizes[i - 1] : 0;
	}

	id = object_heap_allocate(&driver_data->image_heap);
	image_object = IMAGE(driver_data, id);
	if (image_object == NULL)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	status = RequestCreateBuffer(context, 0, VAImageBufferType, image->data_size, 1,
				     NULL, &buffer_id);
	if (status != VA_STATUS_SUCCESS) {
		object_heap_free(&driver_data->image_heap,
				 (struct object_base *)image_object);
		return status;
	}

	image->buf = buffer_id;
	image->image_id = id;
	image_object->image = *image;

	return VA_STATUS_SUCCESS;
}

VAStatus RequestDestroyImage(VADriverContextP context, VAImageID image_id)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	struct object_image *image_object;
	VAStatus status;

	image_object = IMAGE(driver_data, image_id);
	if (image_object == NULL)
		return VA_STATUS_ERROR_INVALID_IMAGE;

	status = RequestDestroyBuffer(context, image_object->image.buf);
	if (status != VA_STATUS_SUCCESS)
		return status;

	object_heap_free(&driver_data->image_heap,
			 (struct object_base *)image_object);

	return VA_STATUS_SUCCESS;
}

static VAStatus copy_surface_to_image (RequestData *driver_data,
				       struct object_surface *surface_object,
				       VAImage *image)
{
	struct object_buffer *buffer_object;
	unsigned int i;

	buffer_object = BUFFER(driver_data, image->buf);
	if (buffer_object == NULL)
		return VA_STATUS_ERROR_INVALID_BUFFER;

	for (i = 0; i < surface_object->destination_logical_planes_count; i++) {
		memcpy(buffer_object->data + image->offsets[i],
		       surface_object->destination_plane_data[surface_object->destination_logical_plane_index[i]] + surface_object->destination_logical_plane_offset[i],
		       surface_object->destination_logical_plane_size[i]);
	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestDeriveImage(VADriverContextP context, VASurfaceID surface_id,
			    VAImage *image)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	struct object_surface *surface_object;
	struct object_buffer *buffer_object;
	VAImageFormat format;
	VAStatus status;

	surface_object = SURFACE(driver_data, surface_id);
	if (surface_object == NULL)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	if (surface_object->status == VASurfaceRendering) {
		status = RequestSyncSurface(context, surface_id);
		if (status != VA_STATUS_SUCCESS)
			return status;
	}

	format.fourcc = VA_FOURCC_NV12;

	status = RequestCreateImage(context, &format, surface_object->width,
				    surface_object->height, image);
	if (status != VA_STATUS_SUCCESS)
		return status;

	status = copy_surface_to_image (driver_data, surface_object, image);
	if (status != VA_STATUS_SUCCESS)
		return status;

	surface_object->status = VASurfaceReady;

	buffer_object = BUFFER(driver_data, image->buf);
	buffer_object->derived_surface_id = surface_id;

	return VA_STATUS_SUCCESS;
}

VAStatus RequestQueryImageFormats(VADriverContextP context,
				  VAImageFormat *formats, int *formats_count)
{
	formats[0].fourcc = VA_FOURCC_NV12;
	*formats_count = 1;

	return VA_STATUS_SUCCESS;
}

VAStatus RequestSetImagePalette(VADriverContextP context, VAImageID image_id,
				unsigned char *palette)
{
	return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus RequestGetImage(VADriverContextP context, VASurfaceID surface_id,
			 int x, int y, unsigned int width, unsigned int height,
			 VAImageID image_id)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	struct object_surface *surface_object;
	struct object_image *image_object;
	VAImage *image;

	surface_object = SURFACE(driver_data, surface_id);
	if (surface_object == NULL)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	image_object = IMAGE(driver_data, image_id);
	if (image_object == NULL)
		return VA_STATUS_ERROR_INVALID_IMAGE;

	image = &image_object->image;
	if (x != 0 || y != 0 || width != image->width || height != image->height)
		return VA_STATUS_ERROR_UNIMPLEMENTED;

	return copy_surface_to_image (driver_data, surface_object, image);
}

VAStatus RequestPutImage(VADriverContextP context, VASurfaceID surface_id,
			 VAImageID image, int src_x, int src_y,
			 unsigned int src_width, unsigned int src_height,
			 int dst_x, int dst_y, unsigned int dst_width,
			 unsigned int dst_height)
{
	return VA_STATUS_ERROR_UNIMPLEMENTED;
}
