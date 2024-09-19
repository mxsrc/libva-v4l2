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

#include "picture.h"

#include <cassert>
#include <cstring>

extern "C" {
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <va/va.h>
}

#include "buffer.h"
#include "config.h"
#include "context.h"
#include "h264.h"
#include "media.h"
#include "mpeg2.h"
#include "request.h"
#include "surface.h"
#include "v4l2.h"
#include "vp8.h"
#include "vp9.h"

static VAStatus codec_store_buffer(RequestData *driver_data,
				   VAProfile profile,
				   struct object_surface *surface_object,
				   struct object_buffer *buffer_object)
{
	switch (profile) {
	case VAProfileMPEG2Simple:
	case VAProfileMPEG2Main:
		return mpeg2_store_buffer(driver_data, surface_object, buffer_object);

	case VAProfileH264Main:
	case VAProfileH264High:
	case VAProfileH264ConstrainedBaseline:
	case VAProfileH264MultiviewHigh:
	case VAProfileH264StereoHigh:
		return h264_store_buffer(driver_data, surface_object, buffer_object);

	case VAProfileVP8Version0_3:
		return vp8_store_buffer(driver_data, surface_object, buffer_object);

	case VAProfileVP9Profile0:
	case VAProfileVP9Profile1:
	case VAProfileVP9Profile2:
	case VAProfileVP9Profile3:
		return vp9_store_buffer(driver_data, surface_object, buffer_object);

	default:
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
	}
}

static VAStatus codec_set_controls(RequestData *driver_data,
				   struct object_context *context,
				   VAProfile profile,
				   struct object_surface *surface_object)
{
	switch (profile) {
	case VAProfileMPEG2Simple:
	case VAProfileMPEG2Main:
		return mpeg2_set_controls(driver_data, context, surface_object);

	case VAProfileH264Main:
	case VAProfileH264High:
	case VAProfileH264ConstrainedBaseline:
	case VAProfileH264MultiviewHigh:
	case VAProfileH264StereoHigh:
		return h264_set_controls(driver_data, context, surface_object);

	case VAProfileVP8Version0_3:
		return vp8_set_controls(driver_data, context, surface_object);

	case VAProfileVP9Profile0:
	case VAProfileVP9Profile1:
	case VAProfileVP9Profile2:
	case VAProfileVP9Profile3:
		return vp9_set_controls(driver_data, context, surface_object);

	default:
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
	}
}

VAStatus RequestBeginPicture(VADriverContextP context, VAContextID context_id,
			     VASurfaceID surface_id)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	struct object_context *context_object;
	struct object_surface *surface_object;

	context_object = CONTEXT(driver_data, context_id);
	if (context_object == NULL)
		return VA_STATUS_ERROR_INVALID_CONTEXT;

	surface_object = SURFACE(driver_data, surface_id);
	if (surface_object == NULL)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	if (surface_object->status == VASurfaceRendering)
		RequestSyncSurface(context, surface_id);

	surface_object->status = VASurfaceRendering;
	context_object->render_surface_id = surface_id;

	return VA_STATUS_SUCCESS;
}

VAStatus RequestRenderPicture(VADriverContextP context, VAContextID context_id,
			      VABufferID *buffers_ids, int buffers_count)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	struct object_context *context_object;
	struct object_config *config_object;
	struct object_surface *surface_object;
	struct object_buffer *buffer_object;
	int rc;
	int i;

	context_object = CONTEXT(driver_data, context_id);
	if (context_object == NULL)
		return VA_STATUS_ERROR_INVALID_CONTEXT;

	config_object = CONFIG(driver_data, context_object->config_id);
	if (config_object == NULL)
		return VA_STATUS_ERROR_INVALID_CONFIG;

	surface_object =
		SURFACE(driver_data, context_object->render_surface_id);
	if (surface_object == NULL)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	for (i = 0; i < buffers_count; i++) {
		buffer_object = BUFFER(driver_data, buffers_ids[i]);
		if (buffer_object == NULL)
			return VA_STATUS_ERROR_INVALID_BUFFER;

		rc = codec_store_buffer(driver_data, config_object->profile,
					surface_object, buffer_object);
		if (rc != VA_STATUS_SUCCESS)
			return rc;
	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestEndPicture(VADriverContextP context, VAContextID context_id)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	struct object_context *context_object;
	struct object_config *config_object;
	struct object_surface *surface_object;
	int request_fd;
	VAStatus status;
	int rc;

	if (!driver_data->video_format)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	context_object = CONTEXT(driver_data, context_id);
	if (context_object == NULL)
		return VA_STATUS_ERROR_INVALID_CONTEXT;

	config_object = CONFIG(driver_data, context_object->config_id);
	if (config_object == NULL)
		return VA_STATUS_ERROR_INVALID_CONFIG;

	surface_object =
		SURFACE(driver_data, context_object->render_surface_id);
	if (surface_object == NULL)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	gettimeofday(&surface_object->timestamp, NULL);

	request_fd = surface_object->request_fd;
	if (driver_data->device.media_fd >= 0) {
		if (request_fd < 0) {
			request_fd = media_request_alloc(driver_data->device.media_fd);
			if (request_fd < 0)
				return VA_STATUS_ERROR_OPERATION_FAILED;

			surface_object->request_fd = request_fd;
		}

		rc = codec_set_controls(driver_data, context_object,
					config_object->profile, surface_object);
		if (rc != VA_STATUS_SUCCESS)
			return rc;
	}

	rc = v4l2_queue_buffer(driver_data->device.video_fd, -1, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, NULL,
			       surface_object->destination_index, 0,
			       surface_object->destination_planes_count);
	if (rc < 0)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	rc = v4l2_queue_buffer(driver_data->device.video_fd, request_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
			       &surface_object->timestamp,
			       surface_object->source_index,
			       surface_object->slices_size, 1);
	if (rc < 0)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	surface_object->slices_size = 0;

	status = RequestSyncSurface(context, context_object->render_surface_id);
	if (status != VA_STATUS_SUCCESS)
		return status;

	context_object->render_surface_id = VA_INVALID_ID;

	return VA_STATUS_SUCCESS;
}
