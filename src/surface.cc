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

#include "surface.h"


#include <cassert>
#include <cstdlib>
#include <cstring>
#include <system_error>

extern "C" {
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <libdrm/drm_fourcc.h>
#include <va/va.h>
#include <va/va_drmcommon.h>
}

#include "media.h"
#include "request.h"
#include "v4l2.h"
#include "video.h"
#include "utils.h"

VAStatus RequestCreateSurfaces2(VADriverContextP context, unsigned int format,
				unsigned int width, unsigned int height,
				VASurfaceID *surfaces_ids,
				unsigned int surfaces_count,
				VASurfaceAttrib *attributes,
				unsigned int attributes_count)
{
	// TODO inspect attributes
	// TODO ensure dimensions match previous surfaces

	auto driver_data = static_cast<RequestData*>(context->pDriverData);

	if (format != VA_RT_FORMAT_YUV420)
		return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;

        if (!driver_data->video_format) {
		if (driver_data->device.format_supported(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_PIX_FMT_NV12)) {
			driver_data->video_format = video_format_find(V4L2_PIX_FMT_NV12);
		} else if (driver_data->device.format_supported(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_PIX_FMT_NV12M)) {
			driver_data->video_format = video_format_find(V4L2_PIX_FMT_NV12M);
		} else {
			return VA_STATUS_ERROR_OPERATION_FAILED;
		}
        }

	std::lock_guard<std::mutex> guard(driver_data->mutex);
	for (unsigned i = 0; i < surfaces_count; i++) {
		surfaces_ids[i] = smallest_free_key(driver_data->surfaces);
		auto [config, inserted] = driver_data->surfaces.emplace(std::make_pair(surfaces_ids[i], Surface{
			.status = VASurfaceReady,
			.width = width,
			.height = height,
			.request_fd = -1,
		}));
		if (!inserted) {
			return VA_STATUS_ERROR_ALLOCATION_FAILED;
		}
	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestCreateSurfacesReally(VADriverContextP context, VASurfaceID *surfaces_ids,
				unsigned surfaces_count) {
	auto driver_data = static_cast<RequestData*>(context->pDriverData);

	if (surfaces_count < 1) {
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	if (!driver_data->surfaces.contains(surfaces_ids[0])) {
		return VA_STATUS_ERROR_INVALID_SURFACE;
	}
	const auto& surface = driver_data->surfaces.at(surfaces_ids[0]);

	try {
		driver_data->device.set_format(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
			    driver_data->video_format->v4l2_format, surface.width, surface.height);
	} catch(std::system_error& e) {
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	struct v4l2_pix_format_mplane* driver_format = &driver_data->device.capture_format.fmt.pix_mp;

	try {
		driver_data->device.request_buffers(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, surfaces_count);
	} catch(std::system_error& e) {
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
	}

	for (unsigned i = 0; i < surfaces_count; i++) {
		if (!driver_data->surfaces.contains(surfaces_ids[i])) {
			return VA_STATUS_ERROR_INVALID_SURFACE;
		}
		auto& surface = driver_data->surfaces.at(surfaces_ids[i]);

		if (surface.destination_plane_data[0]) {  // Already initialized
			continue;
		}

		unsigned map_offsets[VIDEO_MAX_PLANES];
		if (v4l2_query_buffer(driver_data->device.video_fd,
				      V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
				      i,
				      surface.destination_plane_size,
				      map_offsets,
				      NULL) < 0)
			return VA_STATUS_ERROR_ALLOCATION_FAILED;

		for (int j = 0; j < driver_format->num_planes; j++) {
			surface.destination_plane_data[j] =
				static_cast<uint8_t*>(mmap(NULL,
				     surface.destination_plane_size[j],
				     PROT_READ | PROT_WRITE, MAP_SHARED,
				     driver_data->device.video_fd,
				     map_offsets[j]));

			if (surface.destination_plane_data[j] == MAP_FAILED)
				return VA_STATUS_ERROR_ALLOCATION_FAILED;
		}

		if (driver_data->video_format->derive_layout) {  // (logical) single plane
			driver_data->video_format->derive_layout(
					driver_format->width, driver_format->height,
					surface.destination_logical_plane_size, surface.destination_logical_plane_pitch,
					&surface.destination_logical_planes_count);

			for (unsigned j = 0; j < surface.destination_logical_planes_count; j += 1) {
				surface.destination_logical_plane_index[j] = 0;
				surface.destination_logical_plane_offset[j] = (j > 0) ?
					(surface.destination_logical_plane_offset[j - 1] + surface.destination_logical_plane_size[j - 1]) : 0;
			}
		} else {
			surface.destination_logical_planes_count = driver_format->num_planes;
			for (unsigned j = 0; j < surface.destination_logical_planes_count; j += 1) {
				surface.destination_logical_plane_index[j] = j;
				surface.destination_logical_plane_size[j] = driver_format->plane_fmt[j].sizeimage;
				surface.destination_logical_plane_pitch[j] = driver_format->plane_fmt[j].bytesperline;
				surface.destination_logical_plane_offset[j] = (j > 0) ?
					(surface.destination_logical_plane_offset[j - 1] + surface.destination_logical_plane_size[j - 1]) : 0;
			}
		}

		surface.destination_index = i;
		surface.destination_planes_count = driver_format->num_planes;
	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestCreateSurfaces(VADriverContextP context, int width, int height,
			       int format, int surfaces_count,
			       VASurfaceID *surfaces_ids)
{
	return RequestCreateSurfaces2(context, format, width, height,
				      surfaces_ids, surfaces_count, NULL, 0);
}

VAStatus RequestDestroySurfaces(VADriverContextP context,
				VASurfaceID *surfaces_ids, int surfaces_count)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	unsigned int j;

	std::lock_guard<std::mutex> guard(driver_data->mutex);
	for (int i = 0; i < surfaces_count; i++) {
		if (!driver_data->surfaces.contains(surfaces_ids[i])) {
			return VA_STATUS_ERROR_INVALID_SURFACE;
		}
		auto& surface = driver_data->surfaces.at(surfaces_ids[i]);

		if (surface.source_data != NULL &&
		    surface.source_size > 0)
			munmap(surface.source_data,
			       surface.source_size);

		for (j = 0; j < surface.destination_planes_count; j++)
			if (surface.destination_plane_data[j] != NULL &&
			    surface.destination_plane_size[j] > 0)
				munmap(surface.destination_plane_data[j],
				       surface.destination_plane_size[j]);

		if (surface.request_fd > 0)
			close(surface.request_fd);

		driver_data->surfaces.erase(surfaces_ids[i]);

	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestSyncSurface(VADriverContextP context, VASurfaceID surface_id)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	VAStatus status;
	int rc;

	if (!driver_data->video_format) {
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	if (!driver_data->surfaces.contains(surface_id)) {
		return VA_STATUS_ERROR_INVALID_SURFACE;
	}
	auto& surface = driver_data->surfaces.at(surface_id);

	if (surface.status != VASurfaceRendering) {
		status = VA_STATUS_SUCCESS;
		goto complete;
	}

	if (surface.request_fd >= 0) {
		rc = media_request_queue(surface.request_fd);
		if (rc < 0) {
			status = VA_STATUS_ERROR_OPERATION_FAILED;
			goto error;
		}

		rc = media_request_wait_completion(surface.request_fd);
		if (rc < 0) {
			status = VA_STATUS_ERROR_OPERATION_FAILED;
			goto error;
		}

		rc = media_request_reinit(surface.request_fd);
		if (rc < 0) {
			status = VA_STATUS_ERROR_OPERATION_FAILED;
			goto error;
		}
	}

	rc = v4l2_dequeue_buffer(driver_data->device.video_fd, -1, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
				 surface.source_index, 1);
	if (rc < 0) {
		status = VA_STATUS_ERROR_OPERATION_FAILED;
		goto error;
	}

	rc = v4l2_dequeue_buffer(driver_data->device.video_fd, -1, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
				 surface.destination_index,
				 surface.destination_planes_count);
	if (rc < 0) {
		status = VA_STATUS_ERROR_OPERATION_FAILED;
		goto error;
	}

	surface.status = VASurfaceDisplaying;

	status = VA_STATUS_SUCCESS;
	goto complete;

error:
	if (surface.request_fd >= 0) {
		close(surface.request_fd);
		surface.request_fd = -1;
	}

complete:
	return status;
}

VAStatus RequestQuerySurfaceAttributes(VADriverContextP context,
				       VAConfigID config,
				       VASurfaceAttrib *attributes,
				       unsigned int *attributes_count)
{
	VASurfaceAttrib *attributes_list;
	unsigned int attributes_list_size = Config::max_attributes * sizeof(*attributes);
	int memory_types;
	unsigned int i = 0;

	attributes_list = static_cast<VASurfaceAttrib*>(malloc(attributes_list_size));
	memset(attributes_list, 0, attributes_list_size);

	attributes_list[i].type = VASurfaceAttribPixelFormat;
	attributes_list[i].flags = VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE;
	attributes_list[i].value.type = VAGenericValueTypeInteger;
	attributes_list[i].value.value.i = VA_FOURCC_NV12;
	i++;

	attributes_list[i].type = VASurfaceAttribMinWidth;
	attributes_list[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
	attributes_list[i].value.type = VAGenericValueTypeInteger;
	attributes_list[i].value.value.i = 32;
	i++;

	attributes_list[i].type = VASurfaceAttribMaxWidth;
	attributes_list[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
	attributes_list[i].value.type = VAGenericValueTypeInteger;
	attributes_list[i].value.value.i = 2048;
	i++;

	attributes_list[i].type = VASurfaceAttribMinHeight;
	attributes_list[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
	attributes_list[i].value.type = VAGenericValueTypeInteger;
	attributes_list[i].value.value.i = 32;
	i++;

	attributes_list[i].type = VASurfaceAttribMaxHeight;
	attributes_list[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
	attributes_list[i].value.type = VAGenericValueTypeInteger;
	attributes_list[i].value.value.i = 2048;
	i++;

	attributes_list[i].type = VASurfaceAttribMemoryType;
	attributes_list[i].flags = VA_SURFACE_ATTRIB_GETTABLE |
				   VA_SURFACE_ATTRIB_SETTABLE;
	attributes_list[i].value.type = VAGenericValueTypeInteger;

	memory_types = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;

	attributes_list[i].value.value.i = memory_types;
	i++;

	attributes_list_size = i * sizeof(*attributes);

	if (attributes != NULL)
		memcpy(attributes, attributes_list, attributes_list_size);

	free(attributes_list);

	*attributes_count = i;

	return VA_STATUS_SUCCESS;
}

VAStatus RequestQuerySurfaceStatus(VADriverContextP context,
				   VASurfaceID surface_id,
				   VASurfaceStatus *status)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);

	if (!driver_data->surfaces.contains(surface_id)) {
		return VA_STATUS_ERROR_INVALID_SURFACE;
	}
	*status = driver_data->surfaces.at(surface_id).status;

	return VA_STATUS_SUCCESS;
}

VAStatus RequestPutSurface(VADriverContextP context, VASurfaceID surface_id,
			   void *draw, short src_x, short src_y,
			   unsigned short src_width, unsigned short src_height,
			   short dst_x, short dst_y, unsigned short dst_width,
			   unsigned short dst_height, VARectangle *cliprects,
			   unsigned int cliprects_count, unsigned int flags)
{
	return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus RequestLockSurface(VADriverContextP context, VASurfaceID surface_id,
			    unsigned int *fourcc, unsigned int *luma_stride,
			    unsigned int *chroma_u_stride,
			    unsigned int *chroma_v_stride,
			    unsigned int *luma_offset,
			    unsigned int *chroma_u_offset,
			    unsigned int *chroma_v_offset,
			    unsigned int *buffer_name, void **buffer)
{
	return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus RequestUnlockSurface(VADriverContextP context, VASurfaceID surface_id)
{
	return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus RequestExportSurfaceHandle(VADriverContextP context,
				    VASurfaceID surface_id, uint32_t mem_type,
				    uint32_t flags, void *descriptor)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	auto surface_descriptor = static_cast<VADRMPRIMESurfaceDescriptor*>(descriptor);
	int *export_fds = NULL;
	unsigned int export_fds_count;
	unsigned int planes_count;
	unsigned int size;
	unsigned int i;
	VAStatus status;
	int rc;

	if (!driver_data->video_format)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	if (mem_type != VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2)
		return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;

	if (!driver_data->surfaces.contains(surface_id)) {
		return VA_STATUS_ERROR_INVALID_SURFACE;
	}
	const auto& surface = driver_data->surfaces.at(surface_id);

	export_fds_count = surface.destination_planes_count;
	export_fds = static_cast<int*>(malloc(export_fds_count * sizeof(*export_fds)));

	rc = v4l2_export_buffer(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
				surface.destination_index, O_RDONLY,
				export_fds, export_fds_count);
	if (rc < 0) {
		status = VA_STATUS_ERROR_OPERATION_FAILED;
		goto error;
	}

	planes_count = surface.destination_logical_planes_count;

	surface_descriptor->fourcc = VA_FOURCC_NV12;
	surface_descriptor->width = surface.width;
	surface_descriptor->height = surface.height;
	surface_descriptor->num_objects = export_fds_count;

	size = 0;

	if (export_fds_count == 1)
		for (i = 0; i < planes_count; i++)
			size += surface.destination_logical_plane_size[i];

	for (i = 0; i < export_fds_count; i++) {
		surface_descriptor->objects[i].drm_format_modifier =
			driver_data->video_format->drm_modifier;
		surface_descriptor->objects[i].fd = export_fds[i];
		surface_descriptor->objects[i].size = export_fds_count == 1 ?
						      size :
						      surface.destination_logical_plane_size[i];
	}

	surface_descriptor->num_layers = 1;

	surface_descriptor->layers[0].drm_format = driver_data->video_format->drm_format;
	surface_descriptor->layers[0].num_planes = planes_count;

	for (i = 0; i < planes_count; i++) {
		surface_descriptor->layers[0].object_index[i] = export_fds_count == 1 ? 0 : i;
		surface_descriptor->layers[0].offset[i] = (i > 0 ) ? (surface_descriptor->layers[0].offset[i] + surface.destination_logical_plane_size[i]) : 0;
		surface_descriptor->layers[0].pitch[i] = surface.destination_logical_plane_pitch[i];
	}

	status = VA_STATUS_SUCCESS;
	goto complete;

error:
	for (i = 0; i < export_fds_count; i++)
		if (export_fds[i] >= 0)
			close(export_fds[i]);

complete:
	if (export_fds != NULL)
		free(export_fds);

	return status;
}
