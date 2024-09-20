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

#include "context.h"

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <va/va.h>
}

#include "config.h"
#include "request.h"
#include "surface.h"
#include "utils.h"
#include "v4l2.h"

VAStatus RequestCreateContext(VADriverContextP va_context, VAConfigID config_id,
			      int picture_width, int picture_height, int flags,
			      VASurfaceID *surfaces_ids, int surfaces_count,
			      VAContextID *context_id)
{
	auto driver_data = static_cast<RequestData*>(va_context->pDriverData);
	decltype(driver_data->surfaces)::iterator surface;
	unsigned int length;
	unsigned int offset;
	unsigned usurfaces_count;
	unsigned buffer_count = 0;
	uint8_t *source_data = static_cast<uint8_t*>(MAP_FAILED);
	VASurfaceID *ids = NULL;
	VAStatus status;
	unsigned int pixelformat;
	int rc;

	if (!driver_data->video_format)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	if (!driver_data->configs.contains(config_id)) {
		return VA_STATUS_ERROR_INVALID_CONFIG;
	}
	const auto& config = driver_data->configs.at(config_id);

	std::lock_guard<std::mutex> guard(driver_data->mutex);
	*context_id = smallest_free_key(driver_data->contexts);
	auto [context, inserted] = driver_data->contexts.emplace(std::make_pair(*context_id, Context{
		.config_id = config_id,
		.render_surface_id = VA_INVALID_ID,
		.surfaces_count = surfaces_count,
		.picture_width = picture_width,
		.picture_height = picture_height,
		.flags = flags,
	}));
	if (!inserted) {
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
	}


	switch (config.profile) {
	case VAProfileMPEG2Simple:
	case VAProfileMPEG2Main:
		pixelformat = V4L2_PIX_FMT_MPEG2_SLICE;
		break;

	case VAProfileH264Main:
	case VAProfileH264High:
	case VAProfileH264ConstrainedBaseline:
	case VAProfileH264MultiviewHigh:
	case VAProfileH264StereoHigh:
		if (v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_H264)) {
			pixelformat = V4L2_PIX_FMT_H264;
		} else if (v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_H264_SLICE)) {
			pixelformat = V4L2_PIX_FMT_H264_SLICE;
		} else {
			status = VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
			goto error;
		}
		break;

	case VAProfileVP8Version0_3:
		pixelformat = V4L2_PIX_FMT_VP8_FRAME;
		break;

	case VAProfileVP9Profile0:
	case VAProfileVP9Profile1:
	case VAProfileVP9Profile2:
	case VAProfileVP9Profile3:
		if (v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_VP9)) {
			pixelformat = V4L2_PIX_FMT_VP9;
		} else if (v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_VP9_FRAME)) {
			pixelformat = V4L2_PIX_FMT_VP9_FRAME;
		} else {
			status = VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
			goto error;
		}
		break;

	default:
		status = VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
		goto error;
	}

	rc = v4l2_m2m_device_set_format(&driver_data->device, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, pixelformat,
			     picture_width, picture_height);
	if (rc < 0) {
		status = VA_STATUS_ERROR_OPERATION_FAILED;
		goto error;
	}

	// Now that the output format is set, we can set the capture format and allocate the surfaces.
	status = RequestCreateSurfacesReally(va_context, surfaces_ids, surfaces_count);
	if (status != VA_STATUS_SUCCESS) {
		goto error;
	}

	usurfaces_count = surfaces_count;
	rc = v4l2_m2m_device_request_buffers(&driver_data->device, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, &usurfaces_count);
	if (rc < 0) {
		status = VA_STATUS_ERROR_ALLOCATION_FAILED;
		goto error;
	}

	/*
	 * The surface_ids array has been allocated by the caller and
	 * we don't have any indication wrt its life time. Let's make sure
	 * its life span is under our control.
	 */
	ids = static_cast<VASurfaceID*>(malloc(surfaces_count * sizeof(VASurfaceID)));
	if (ids == NULL) {
		status = VA_STATUS_ERROR_ALLOCATION_FAILED;
		goto error;
	}

	memcpy(ids, surfaces_ids, surfaces_count * sizeof(VASurfaceID));
	context->second.surfaces_ids = ids;

	for (int i = 0; i < surfaces_count; i++) {
		surface = driver_data->surfaces.find(surfaces_ids[i]);
		if (surface == driver_data->surfaces.end()) {
			status = VA_STATUS_ERROR_INVALID_SURFACE;
			goto error;
		}

		rc = v4l2_query_buffer(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
				       i, &length, &offset, &buffer_count);
		if (rc < 0) {
			status = VA_STATUS_ERROR_ALLOCATION_FAILED;
			goto error;
		}

		source_data = static_cast<uint8_t*>(mmap(NULL, length, PROT_READ | PROT_WRITE,
				   MAP_SHARED, driver_data->device.video_fd, offset));
		if (source_data == MAP_FAILED) {
			status = VA_STATUS_ERROR_ALLOCATION_FAILED;
			goto error;
		}

		surface->second.source_index = i;
		surface->second.source_data = source_data;
		surface->second.source_size = length;
	}

	rc = v4l2_set_stream(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, true);
	if (rc < 0) {
		status = VA_STATUS_ERROR_OPERATION_FAILED;
		goto error;
	}

	rc = v4l2_set_stream(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, true);
	if (rc < 0) {
		status = VA_STATUS_ERROR_OPERATION_FAILED;
		goto error;
	}

	status = VA_STATUS_SUCCESS;
	goto complete;

error:
	if (source_data != MAP_FAILED)
		munmap(source_data, length);

	if (ids != NULL)
		free(ids);

	if (inserted) {
		driver_data->contexts.erase(*context_id);
	}

complete:
	return status;
}

VAStatus RequestDestroyContext(VADriverContextP va_context, VAContextID context_id)
{
	auto driver_data = static_cast<RequestData*>(va_context->pDriverData);
	int rc;

	if (!driver_data->video_format) {
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	if (!driver_data->contexts.contains(context_id)) {
		return VA_STATUS_ERROR_INVALID_CONTEXT;
	}
	const auto& context = driver_data->contexts.at(context_id);

	rc = v4l2_set_stream(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, false);
	if (rc < 0)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	rc = v4l2_set_stream(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, false);
	if (rc < 0)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	free(context.surfaces_ids);

	struct v4l2_requestbuffers reqbuf = {
		.count = 0,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.memory = V4L2_MEMORY_MMAP,
	};

	rc = ioctl(driver_data->device.video_fd, VIDIOC_REQBUFS, &reqbuf);
	if (rc < 0) {
		error_log(va_context, "Unable to free buffers: %s\n", strerror(errno));
		return -1;
	}

	std::lock_guard<std::mutex> guard(driver_data->mutex);
	driver_data->contexts.erase(context_id);

	return VA_STATUS_SUCCESS;
}
