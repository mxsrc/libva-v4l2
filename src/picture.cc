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
#include <functional>
#include <system_error>

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
#include "utils.h"
#include "v4l2.h"
#include "vp8.h"
#ifdef ENABLE_VP9
#include "vp9.h"
#endif

using fourcc = uint32_t;

VAStatus RequestBeginPicture(VADriverContextP va_context, VAContextID context_id, VASurfaceID surface_id)
{
    auto driver_data = static_cast<RequestData*>(va_context->pDriverData);

    if (!driver_data->contexts.contains(context_id)) {
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }
    auto& context = *driver_data->contexts.at(context_id);

    if (!driver_data->surfaces.contains(surface_id)) {
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }
    auto& surface = driver_data->surfaces.at(surface_id);

    if (surface.status == VASurfaceRendering)
        RequestSyncSurface(va_context, surface_id);

    surface.status = VASurfaceRendering;
    context.render_surface_id = surface_id;

    return VA_STATUS_SUCCESS;
}

VAStatus RequestRenderPicture(
    VADriverContextP va_context, VAContextID context_id, VABufferID* buffers_ids, int buffers_count)
{
    auto driver_data = static_cast<RequestData*>(va_context->pDriverData);
    int rc;
    int i;

    if (!driver_data->contexts.contains(context_id)) {
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }
    const auto& context = *driver_data->contexts.at(context_id);

    if (!driver_data->surfaces.contains(context.render_surface_id)) {
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }
    for (i = 0; i < buffers_count; i++) {
        if (!driver_data->buffers.contains(buffers_ids[i])) {
            return VA_STATUS_ERROR_INVALID_BUFFER;
        }

        rc = context.store_buffer(driver_data->buffers.at(buffers_ids[i]));
        if (rc != VA_STATUS_SUCCESS)
            return rc;
    }

    return VA_STATUS_SUCCESS;
}

VAStatus RequestEndPicture(VADriverContextP va_context, VAContextID context_id)
{
    auto driver_data = static_cast<RequestData*>(va_context->pDriverData);
    int request_fd;
    VAStatus status;

    if (!driver_data->video_format)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    if (!driver_data->contexts.contains(context_id)) {
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }
    auto& context = *driver_data->contexts.at(context_id);
    auto& surface = driver_data->surfaces.at(context.render_surface_id);

    gettimeofday(&surface.timestamp, NULL);

    request_fd = surface.request_fd;
    if (driver_data->device.media_fd >= 0) {
        if (request_fd < 0) {
            request_fd = media_request_alloc(driver_data->device.media_fd);
            if (request_fd < 0)
                return VA_STATUS_ERROR_OPERATION_FAILED;

            surface.request_fd = request_fd;
        }

        status = context.set_controls();
        if (status != VA_STATUS_SUCCESS)
            return status;
    }

    try {
        driver_data->device.buffer(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, surface.destination_index).queue();
        driver_data->device.buffer(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, surface.source_index)
            .queue(request_fd, &surface.timestamp, surface.source_size_used);
    } catch (std::system_error& e) {
        error_log(va_context, "Unable to queue buffer: %s\n", e.what());
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    surface.source_size_used = 0;

    status = RequestSyncSurface(va_context, context.render_surface_id);
    if (status != VA_STATUS_SUCCESS)
        return status;

    context.render_surface_id = VA_INVALID_ID;
    memset(&surface.params, 0, sizeof(surface.params));

    return VA_STATUS_SUCCESS;
}
