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

extern "C" {
#include <va/va_backend.h>
}

struct Config {
    static constexpr unsigned max_attributes = 10;

    VAProfile profile;
    VAEntrypoint entrypoint;
    VAConfigAttrib attributes[max_attributes];
    int attributes_count;
};

VAStatus createConfig(VADriverContextP context, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib* attributes,
    int attributes_count, VAConfigID* config_id);
VAStatus destroyConfig(VADriverContextP context, VAConfigID config_id);
VAStatus queryConfigProfiles(VADriverContextP context, VAProfile* profiles, int* profiles_count);
VAStatus queryConfigEntrypoints(
    VADriverContextP context, VAProfile profile, VAEntrypoint* entrypoints, int* entrypoints_count);
VAStatus queryConfigAttributes(VADriverContextP context, VAConfigID config_id, VAProfile* profile,
    VAEntrypoint* entrypoint, VAConfigAttrib* attributes, int* attributes_count);
VAStatus getConfigAttributes(VADriverContextP context, VAProfile profile, VAEntrypoint entrypoint,
    VAConfigAttrib* attributes, int attributes_count);
VAStatus queryDisplayAttributes(VADriverContextP context, VADisplayAttribute* attributes, int* attributes_count);
VAStatus getDisplayAttributes(VADriverContextP context, VADisplayAttribute* attributes, int attributes_count);
VAStatus setDisplayAttributes(VADriverContextP context, VADisplayAttribute* attributes, int attributes_count);
