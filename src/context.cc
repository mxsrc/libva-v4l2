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

#include "context.h"

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ranges>
#include <system_error>

extern "C" {
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <va/va.h>
}

#include "config.h"
#include "driver.h"
#include "h264.h"
#include "mpeg2.h"
#include "surface.h"
#include "utils.h"
#include "v4l2.h"
#include "vp8.h"
#ifdef ENABLE_VP9
#include "vp9.h"
#endif

Context::Context(DriverData* driver_data, VAConfigID config_id, int picture_width, int picture_height,
    std::span<VASurfaceID> surface_ids)
    : config_id(config_id)
    , render_surface_id(VA_INVALID_ID)
    , picture_width(picture_width)
    , picture_height(picture_height)
    , driver_data(driver_data)
{
    // Now that the output format is set, we can set the capture format and allocate the surfaces.
    createSurfacesDeferred(driver_data, surface_ids);

    driver_data->device.request_buffers(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, surface_ids.size());

    for (unsigned i = 0; i < surface_ids.size(); i++) {
        driver_data->surfaces.at(i).source_index = i;
    }

    driver_data->device.set_streaming(true);
}

VAStatus createContext(VADriverContextP va_context, VAConfigID config_id, int picture_width, int picture_height,
    int flags, VASurfaceID* surface_ids, int surfaces_count, VAContextID* context_id)
{
    auto driver_data = static_cast<DriverData*>(va_context->pDriverData);

    if (!driver_data->configs.contains(config_id)) {
        return VA_STATUS_ERROR_INVALID_CONFIG;
    }
    const auto& config = driver_data->configs.at(config_id);

    fourcc pixelformat = 0;
    for (auto&& [format, profile_func] : supported_profile_funcs) {
        const auto& supported_profiles = profile_func(driver_data->device);
        if (std::ranges::find(supported_profiles, config.profile) != supported_profiles.end()) {
            pixelformat = format;
            break;
        }
    }
    if (pixelformat == 0) {
        throw std::runtime_error("Invalid profile");
    }

    driver_data->device.set_format(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, pixelformat, picture_width, picture_height);

    auto surfaces = std::span(surface_ids, surfaces_count);
    for (auto&& surface : surfaces) {
        if (!driver_data->surfaces.contains(surface)) {
            return VA_STATUS_ERROR_INVALID_SURFACE;
        }
    }

    if (!driver_data->video_format) {
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    std::lock_guard<std::mutex> guard(driver_data->mutex);
    *context_id = smallest_free_key(driver_data->contexts);
    try {
        std::pair<decltype(driver_data->contexts)::iterator, bool> insert_result
            = { driver_data->contexts.end(), false };
        switch (pixelformat) {
        case V4L2_PIX_FMT_MPEG2_SLICE:
            insert_result = driver_data->contexts.emplace(std::make_pair(
                *context_id, new MPEG2Context(driver_data, config_id, picture_width, picture_height, surfaces)));
            break;
        case V4L2_PIX_FMT_H264_SLICE:
            insert_result = driver_data->contexts.emplace(std::make_pair(
                *context_id, new H264Context(driver_data, config_id, picture_width, picture_height, surfaces)));
            break;
        case V4L2_PIX_FMT_VP8_FRAME:
            insert_result = driver_data->contexts.emplace(std::make_pair(
                *context_id, new VP8Context(driver_data, config_id, picture_width, picture_height, surfaces)));
            break;
#ifdef ENABLE_VP9
        case V4L2_PIX_FMT_VP9_FRAME:
            insert_result = driver_data->contexts.emplace(std::make_pair(
                *context_id, new VP9Context(driver_data, config_id, picture_width, picture_height, surfaces)));
            break;
#endif
        }
        if (!insert_result.second) {
            error_log(va_context, "Failed to create context\n");
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
        }
    } catch (std::exception& e) {
        error_log(va_context, "Failed to create context: %s\n", e.what());
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    return VA_STATUS_SUCCESS;
}

VAStatus destroyContext(VADriverContextP va_context, VAContextID context_id)
{
    auto driver_data = static_cast<DriverData*>(va_context->pDriverData);

    if (!driver_data->video_format) {
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    std::lock_guard<std::mutex> guard(driver_data->mutex);
    if (!driver_data->contexts.contains(context_id)) {
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }
    driver_data->contexts.erase(context_id);

    try {
        driver_data->device.set_streaming(false);
    } catch (std::system_error& e) {
        error_log(va_context, "Unable to disable streaming: %s\n", e.what());
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    driver_data->device.request_buffers(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 0);

    return VA_STATUS_SUCCESS;
}
