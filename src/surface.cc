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

#include "surface.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ranges>
#include <stdexcept>
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
#include "utils.h"
#include "v4l2.h"
#include "video.h"

VAStatus RequestCreateSurfaces2(VADriverContextP context, unsigned int format, unsigned int width, unsigned int height,
    VASurfaceID* surfaces_ids, unsigned int surfaces_count, VASurfaceAttrib* attributes, unsigned int attributes_count)
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
        auto [config, inserted] = driver_data->surfaces.emplace(std::make_pair(
            surfaces_ids[i], Surface { .status = VASurfaceReady, .width = width, .height = height, .request_fd = -1 }));
        if (!inserted) {
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
        }
    }

    return VA_STATUS_SUCCESS;
}

void RequestCreateSurfacesDeferred(RequestData* driver_data, std::span<VASurfaceID> surface_ids)
{
    if (surface_ids.size() < 1) {
        throw std::invalid_argument("No surfaces to be created");
    }
    const auto& surface = driver_data->surfaces.at(surface_ids[0]);

    driver_data->device.set_format(
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, driver_data->video_format->v4l2_format, surface.width, surface.height);

    v4l2_pix_format_mplane* driver_format = &driver_data->device.capture_format.fmt.pix_mp;

    driver_data->device.request_buffers(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, surface_ids.size());

    for (unsigned i = 0; i < surface_ids.size(); i++) {
        auto& surface = driver_data->surfaces.at(surface_ids[i]);
        if (driver_data->video_format->derive_layout) { // (logical) single plane
            surface.logical_destination_layout
                = driver_data->video_format->derive_layout(driver_format->width, driver_format->height);
        } else {
            for (unsigned j = 0; j < driver_format->num_planes; j += 1) {
                surface.logical_destination_layout.push_back({
                    j,
                    driver_format->plane_fmt[j].sizeimage,
                    driver_format->plane_fmt[j].bytesperline,
                    (j > 0) ? (surface.logical_destination_layout[j - 1].offset
                                  + surface.logical_destination_layout[j - 1].size)
                            : 0,
                });
            }
        }

        surface.destination_index = i;
    }
}

VAStatus RequestCreateSurfaces(
    VADriverContextP context, int width, int height, int format, int surfaces_count, VASurfaceID* surfaces_ids)
{
    return RequestCreateSurfaces2(context, format, width, height, surfaces_ids, surfaces_count, NULL, 0);
}

VAStatus RequestDestroySurfaces(VADriverContextP context, VASurfaceID* surfaces_ids, int surfaces_count)
{
    auto driver_data = static_cast<RequestData*>(context->pDriverData);

    std::lock_guard<std::mutex> guard(driver_data->mutex);
    for (int i = 0; i < surfaces_count; i++) {
        if (!driver_data->surfaces.contains(surfaces_ids[i])) {
            return VA_STATUS_ERROR_INVALID_SURFACE;
        }
        auto& surface = driver_data->surfaces.at(surfaces_ids[i]);

        if (surface.request_fd > 0)
            close(surface.request_fd);

        driver_data->surfaces.erase(surfaces_ids[i]);
    }

    return VA_STATUS_SUCCESS;
}

VAStatus RequestSyncSurface(VADriverContextP context, VASurfaceID surface_id)
{
    auto driver_data = static_cast<RequestData*>(context->pDriverData);

    if (!driver_data->video_format) {
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    if (!driver_data->surfaces.contains(surface_id)) {
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }
    auto& surface = driver_data->surfaces.at(surface_id);

    if (surface.status != VASurfaceRendering) {
        return VA_STATUS_SUCCESS;
    }

    if (surface.request_fd >= 0) {
        try {
            errno_wrapper(media_request_queue, surface.request_fd);
            errno_wrapper(media_request_wait_completion, surface.request_fd);
            errno_wrapper(media_request_reinit, surface.request_fd);
        } catch (std::runtime_error& e) {
            close(surface.request_fd);
            surface.request_fd = -1;
            error_log(context, "Failed to process request: %s\n", e.what());
            return VA_STATUS_ERROR_OPERATION_FAILED;
        }
    }

    try {
        driver_data->device.buffer(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, surface.source_index).dequeue();
        driver_data->device.buffer(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, surface.destination_index).dequeue();
    } catch (std::runtime_error& e) {
        error_log(context, "Failed to dequeue buffer: %s\n", e.what());
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    surface.status = VASurfaceDisplaying;

    return VA_STATUS_SUCCESS;
}

VAStatus RequestQuerySurfaceAttributes(
    VADriverContextP context, VAConfigID config, VASurfaceAttrib* attributes, unsigned int* attributes_count)
{
    VASurfaceAttrib* attributes_list;
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
    attributes_list[i].flags = VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE;
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

VAStatus RequestQuerySurfaceStatus(VADriverContextP context, VASurfaceID surface_id, VASurfaceStatus* status)
{
    auto driver_data = static_cast<RequestData*>(context->pDriverData);

    if (!driver_data->surfaces.contains(surface_id)) {
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }
    *status = driver_data->surfaces.at(surface_id).status;

    return VA_STATUS_SUCCESS;
}

VAStatus RequestPutSurface(VADriverContextP context, VASurfaceID surface_id, void* draw, short src_x, short src_y,
    unsigned short src_width, unsigned short src_height, short dst_x, short dst_y, unsigned short dst_width,
    unsigned short dst_height, VARectangle* cliprects, unsigned int cliprects_count, unsigned int flags)
{
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus RequestLockSurface(VADriverContextP context, VASurfaceID surface_id, unsigned int* fourcc,
    unsigned int* luma_stride, unsigned int* chroma_u_stride, unsigned int* chroma_v_stride, unsigned int* luma_offset,
    unsigned int* chroma_u_offset, unsigned int* chroma_v_offset, unsigned int* buffer_name, void** buffer)
{
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus RequestUnlockSurface(VADriverContextP context, VASurfaceID surface_id)
{
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus RequestExportSurfaceHandle(
    VADriverContextP context, VASurfaceID surface_id, uint32_t mem_type, uint32_t flags, void* descriptor)
{
    auto driver_data = static_cast<RequestData*>(context->pDriverData);
    auto surface_descriptor = static_cast<VADRMPRIMESurfaceDescriptor*>(descriptor);

    if (!driver_data->video_format)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    if (mem_type != VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2)
        return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;

    if (!driver_data->surfaces.contains(surface_id)) {
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }
    const auto& surface = driver_data->surfaces.at(surface_id);

    auto& destination_buffer
        = driver_data->device.buffer(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, surface.destination_index);
    std::vector<int> export_fds;
    try {
        export_fds = destination_buffer.export_(O_RDONLY);
    } catch (std::runtime_error& e) {
        error_log(context, "Failed to export buffer: %s\n", e.what());
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    surface_descriptor->fourcc = VA_FOURCC_NV12;
    surface_descriptor->width = surface.width;
    surface_descriptor->height = surface.height;
    surface_descriptor->num_objects = export_fds.size();

    const auto& mapping = destination_buffer.mapping();
    for (unsigned i = 0; i < export_fds.size(); i += 1) {
        surface_descriptor->objects[i].drm_format_modifier = driver_data->video_format->drm_modifier;
        surface_descriptor->objects[i].fd = export_fds[i];
        surface_descriptor->objects[i].size = mapping[i].size();
    }

    surface_descriptor->num_layers = 1;

    surface_descriptor->layers[0].drm_format = driver_data->video_format->drm_format;
    surface_descriptor->layers[0].num_planes = surface.logical_destination_layout.size();

    for (unsigned i = 0; i < surface_descriptor->layers[0].num_planes; i++) {
        surface_descriptor->layers[0].object_index[i] = surface.logical_destination_layout[i].physical_plane_index;
        surface_descriptor->layers[0].pitch[i] = surface.logical_destination_layout[i].pitch;
        surface_descriptor->layers[0].offset[i] = surface.logical_destination_layout[i].offset;
    }

    return VA_STATUS_SUCCESS;
}
