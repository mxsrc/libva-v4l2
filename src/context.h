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

#include <span>

extern "C" {
#include <va/va_backend.h>
}

#include "buffer.h"
#include "v4l2.h"

struct DriverData;

class Context {
public:
    static Context* create(DriverData* driver_data, VAProfile profile, int picture_width, int picture_height,
        std::span<VASurfaceID> surface_ids);
    static std::set<VAProfile> supported_profiles(const std::vector<V4L2M2MDevice>& devices);

    Context(DriverData* driver_data, V4L2M2MDevice& device, fourcc pixelformat, int picture_width, int picture_height,
        std::span<VASurfaceID> surface_ids);
    virtual ~Context();

    virtual VAStatus store_buffer(const Buffer& buffer) const = 0;
    virtual int set_controls() = 0;

    VASurfaceID render_surface_id;
    int picture_width;
    int picture_height;
    DriverData* driver_data;
    V4L2M2MDevice& device;
};

VAStatus createContext(VADriverContextP va_context, VAConfigID config_id, int picture_width, int picture_height,
    int flags, VASurfaceID* surfaces_ids, int surfaces_count, VAContextID* context_id);
VAStatus destroyContext(VADriverContextP va_context, VAContextID context_id);
