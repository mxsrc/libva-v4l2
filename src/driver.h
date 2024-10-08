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

#pragma once

#include <map>
#include <memory>
#include <mutex>

extern "C" {
#include <linux/videodev2.h>

#include <va/va.h>
}

#include "buffer.h"
#include "config.h"
#include "context.h"
#include "surface.h"
#include "v4l2.h"

class Context;

#define V4L2_STR_VENDOR "v4l2"
#define V4L2_MAX_PROFILES 11
#define V4L2_MAX_ENTRYPOINTS 5
#define V4L2_MAX_IMAGE_FORMATS 10
#define V4L2_MAX_SUBPIC_FORMATS 4
#define V4L2_MAX_DISPLAY_ATTRIBUTES 4

struct DriverData {
    DriverData(const std::vector<std::pair<std::string, std::optional<std::string>>>& device_paths);

    std::map<VAConfigID, Config> configs;
    std::map<VAContextID, std::unique_ptr<Context>> contexts;
    std::map<VASurfaceID, Surface> surfaces;
    std::map<VABufferID, Buffer> buffers;
    std::map<VAImageID, VAImage> images;
    std::vector<V4L2M2MDevice> devices;
    std::mutex mutex;
};

extern "C" VAStatus VA_DRIVER_INIT_FUNC(VADriverContextP context);
VAStatus terminate(VADriverContextP va_context);
