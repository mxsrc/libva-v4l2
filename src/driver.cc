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

#include "driver.h"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <va/va.h>
#include <va/va_backend.h>
}

#include "buffer.h"
#include "config.h"
#include "context.h"
#include "image.h"
#include "picture.h"
#include "subpicture.h"
#include "surface.h"
#include "utils.h"

namespace {

std::optional<std::string> getenv_opt(const std::string& name)
{
    auto val = getenv(name.c_str());
    return val ? std::optional<std::string>(val) : std::optional<std::string>();
}

} // namespace

DriverData::DriverData(const std::vector<std::pair<std::string, std::optional<std::string>>>& device_paths)
    : devices()
{
    for (auto&& [video_path, media_path] : device_paths) {
        devices.emplace_back(video_path, media_path);
    }
}

/* Set default visibility for the init function only. */
VAStatus __attribute__((visibility("default"))) VA_DRIVER_INIT_FUNC(VADriverContextP context);

extern "C" VAStatus VA_DRIVER_INIT_FUNC(VADriverContextP context)
{

    auto devices = V4L2M2MDevice::enumerate_devices();

    if (const auto video_path_env = getenv_opt("LIBVA_V4L2_VIDEO_PATH"); video_path_env) {
        const auto media_path_env = getenv_opt("LIBVA_V4L2_MEDIA_PATH");
        info_log(context, "Overriding V4L2 device with %s & %s.\n", video_path_env.value().c_str(),
            media_path_env.value_or("").c_str());
        devices.clear();
        devices.push_back({ video_path_env.value(), media_path_env });
    }
    auto driver_data = new DriverData(devices);

    struct VADriverVTable* vtable = context->vtable;

    context->version_major = VA_MAJOR_VERSION;
    context->version_minor = VA_MINOR_VERSION;
    context->max_profiles = V4L2_MAX_PROFILES;
    context->max_entrypoints = V4L2_MAX_ENTRYPOINTS;
    context->max_attributes = Config::max_attributes;
    context->max_image_formats = V4L2_MAX_IMAGE_FORMATS;
    context->max_subpic_formats = V4L2_MAX_SUBPIC_FORMATS;
    context->max_display_attributes = V4L2_MAX_DISPLAY_ATTRIBUTES;
    context->str_vendor = V4L2_STR_VENDOR;

    vtable->vaTerminate = terminate;
    vtable->vaQueryConfigEntrypoints = queryConfigEntrypoints;
    vtable->vaQueryConfigProfiles = queryConfigProfiles;
    vtable->vaQueryConfigEntrypoints = queryConfigEntrypoints;
    vtable->vaQueryConfigAttributes = queryConfigAttributes;
    vtable->vaCreateConfig = createConfig;
    vtable->vaDestroyConfig = destroyConfig;
    vtable->vaGetConfigAttributes = getConfigAttributes;
    vtable->vaCreateSurfaces = createSurfaces;
    vtable->vaCreateSurfaces2 = createSurfaces2;
    vtable->vaDestroySurfaces = destroySurfaces;
    vtable->vaExportSurfaceHandle = exportSurfaceHandle;
    vtable->vaCreateContext = createContext;
    vtable->vaDestroyContext = destroyContext;
    vtable->vaCreateBuffer = createBuffer;
    vtable->vaBufferSetNumElements = bufferSetNumElements;
    vtable->vaMapBuffer = mapBuffer;
    vtable->vaUnmapBuffer = unmapBuffer;
    vtable->vaDestroyBuffer = destroyBuffer;
    vtable->vaBufferInfo = bufferInfo;
    vtable->vaAcquireBufferHandle = acquireBufferHandle;
    vtable->vaReleaseBufferHandle = releaseBufferHandle;
    vtable->vaBeginPicture = beginPicture;
    vtable->vaRenderPicture = renderPicture;
    vtable->vaEndPicture = endPicture;
    vtable->vaSyncSurface = syncSurface;
    vtable->vaQuerySurfaceAttributes = querySurfaceAttributes;
    vtable->vaQuerySurfaceStatus = querySurfaceStatus;
    vtable->vaPutSurface = putSurface;
    vtable->vaQueryImageFormats = queryImageFormats;
    vtable->vaCreateImage = createImage;
    vtable->vaDeriveImage = deriveImage;
    vtable->vaDestroyImage = destroyImage;
    vtable->vaSetImagePalette = setImagePalette;
    vtable->vaGetImage = getImage;
    vtable->vaPutImage = putImage;
    vtable->vaQuerySubpictureFormats = querySubpictureFormats;
    vtable->vaCreateSubpicture = createSubpicture;
    vtable->vaDestroySubpicture = destroySubpicture;
    vtable->vaSetSubpictureImage = setSubpictureImage;
    vtable->vaSetSubpictureChromakey = setSubpictureChromakey;
    vtable->vaSetSubpictureGlobalAlpha = setSubpictureGlobalAlpha;
    vtable->vaAssociateSubpicture = associateSubpicture;
    vtable->vaDeassociateSubpicture = deassociateSubpicture;
    vtable->vaQueryDisplayAttributes = queryDisplayAttributes;
    vtable->vaGetDisplayAttributes = getDisplayAttributes;
    vtable->vaSetDisplayAttributes = setDisplayAttributes;
    vtable->vaLockSurface = lockSurface;
    vtable->vaUnlockSurface = unlockSurface;

    context->pDriverData = driver_data;

    return VA_STATUS_SUCCESS;
}

VAStatus terminate(VADriverContextP va_context)
{
    auto driver_data = static_cast<DriverData*>(va_context->pDriverData);

    /* Cleanup leftover buffers. */
    for (auto&& [id, config] : driver_data->configs) {
        destroyConfig(va_context, id);
    }

    for (auto&& [id, ctx] : driver_data->contexts) {
        destroyContext(va_context, id);
    }

    for (auto&& [id, surface] : driver_data->surfaces) {
        VASurfaceID id_ = id;
        destroySurfaces(va_context, &id_, 1);
    }

    for (auto&& [id, buffer] : driver_data->buffers) {
        destroyBuffer(va_context, id);
    }

    for (auto&& [id, image] : driver_data->images) {
        destroyImage(va_context, id);
    }

    delete driver_data;
    va_context->pDriverData = nullptr;

    return VA_STATUS_SUCCESS;
}
