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

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

extern "C" {
#include <linux/videodev2.h>

#include <va/va.h>
}

#include "buffer.h"
#include "config.h"
#include "context.h"
#include "surface.h"
#include "v4l2.h"
#include "video.h"

class Context;

#define V4L2_REQUEST_STR_VENDOR "v4l2-request"

#define V4L2_REQUEST_MAX_PROFILES 11
#define V4L2_REQUEST_MAX_ENTRYPOINTS 5
#define V4L2_REQUEST_MAX_IMAGE_FORMATS 10
#define V4L2_REQUEST_MAX_SUBPIC_FORMATS 4
#define V4L2_REQUEST_MAX_DISPLAY_ATTRIBUTES 4

extern const std::map<fourcc, std::function<std::vector<VAProfile>(const V4L2M2MDevice&)>> supported_profile_funcs;

struct RequestData {
    RequestData(const std::string& video_path, const std::optional<std::string> media_path)
        : device(video_path, media_path)
    {
    }

    std::map<VAConfigID, Config> configs;
    std::map<VAContextID, std::unique_ptr<Context>> contexts;
    std::map<VASurfaceID, Surface> surfaces;
    std::map<VABufferID, Buffer> buffers;
    std::map<VAImageID, VAImage> images;
    V4L2M2MDevice device;

    const struct video_format* video_format = nullptr;
    std::mutex mutex;
};

extern "C" VAStatus VA_DRIVER_INIT_FUNC(VADriverContextP context);
VAStatus RequestTerminate(VADriverContextP va_context);
