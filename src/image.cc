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

#include "image.h"

#include <cassert>
#include <cstring>
#include <va/va.h>

extern "C" {
#include <linux/videodev2.h>
}

#include "buffer.h"
#include "driver.h"
#include "format.h"
#include "surface.h"
#include "utils.h"
#include "v4l2.h"

namespace {

VAStatus copy_surface_to_image(DriverData* driver_data, const Surface& surface, VAImage* image)
{
    unsigned int i;

    if (!driver_data->buffers.contains(image->buf)) {
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }
    auto& buffer = driver_data->buffers.at(image->buf);

    for (i = 0; i < surface.logical_destination_layout.size(); i++) {
        const auto& mapping
            = driver_data->device.buffer(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, surface.destination_index).mapping();

        memcpy(buffer.data.get() + image->offsets[i],
            mapping[surface.logical_destination_layout[i].physical_plane_index].data()
                + surface.logical_destination_layout[i].offset,
            surface.logical_destination_layout[i].size);
    }

    return VA_STATUS_SUCCESS;
}

} // namespace

VAStatus createImage(VADriverContextP context, VAImageFormat* format, int width, int height, VAImage* image)
{
    auto driver_data = static_cast<DriverData*>(context->pDriverData);
    unsigned int destination_sizes[VIDEO_MAX_PLANES];
    VABufferID buffer_id;
    VAStatus status;
    unsigned int i;

    memset(image, 0, sizeof(*image));
    image->format = *format;
    image->width = width;
    image->height = height;

    auto format_spec = lookup_format(driver_data->device.capture_format.fmt.pix_mp.pixelformat);

    struct v4l2_pix_format_mplane* driver_format = &driver_data->device.capture_format.fmt.pix_mp;

    if (format_spec.v4l2.derive_layout) {
        // Have to use the driver format to get the actual height, which may differ due to block alignment.
        const auto layout = format_spec.v4l2.derive_layout(driver_format->width, driver_format->height);

        image->num_planes = layout.size();
        for (unsigned i = 0; i < image->num_planes; i += 1) {
            destination_sizes[i] = layout[i].size;
            image->pitches[i] = layout[i].pitch;
        }
    } else {
        image->num_planes = driver_format->num_planes;
        for (unsigned i = 0; i < image->num_planes; i += 1) {
            destination_sizes[i] = driver_format->plane_fmt[i].sizeimage;
            image->pitches[i] = driver_format->plane_fmt[i].bytesperline;
        }
    }

    for (i = 0; i < image->num_planes; i++) {
        image->data_size += destination_sizes[i]; // The size returned by V4L2 covers buffers, not logical planes.
        image->offsets[i] = i > 0 ? destination_sizes[i - 1] : 0;
    }

    status = createBuffer(context, 0, VAImageBufferType, image->data_size, 1, NULL, &buffer_id);
    if (status != VA_STATUS_SUCCESS) {
        return status;
    }
    image->buf = buffer_id;

    std::lock_guard<std::mutex> guard(driver_data->mutex);
    image->image_id = smallest_free_key(driver_data->images);
    auto [image_it, inserted] = driver_data->images.emplace(std::make_pair(image->image_id, *image));
    if (!inserted) {
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    return VA_STATUS_SUCCESS;
}

VAStatus destroyImage(VADriverContextP context, VAImageID image_id)
{
    auto driver_data = static_cast<DriverData*>(context->pDriverData);

    if (!driver_data->images.contains(image_id)) {
        return VA_STATUS_ERROR_INVALID_IMAGE;
    }
    auto& image = driver_data->images.at(image_id);

    VAStatus status = destroyBuffer(context, image.buf);
    if (status != VA_STATUS_SUCCESS) {
        return status;
    }

    std::lock_guard<std::mutex> guard(driver_data->mutex);
    if (!driver_data->images.erase(image_id)) {
        return VA_STATUS_ERROR_INVALID_IMAGE;
    }

    return VA_STATUS_SUCCESS;
}

VAStatus deriveImage(VADriverContextP context, VASurfaceID surface_id, VAImage* image)
{
    auto driver_data = static_cast<DriverData*>(context->pDriverData);
    VAImageFormat format;
    VAStatus status;

    if (!driver_data->surfaces.contains(surface_id)) {
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }
    auto& surface = driver_data->surfaces.at(surface_id);

    if (surface.status == VASurfaceRendering) {
        status = syncSurface(context, surface_id);
        if (status != VA_STATUS_SUCCESS)
            return status;
    }

    format.fourcc = VA_FOURCC_NV12;

    status = createImage(context, &format, surface.width, surface.height, image);
    if (status != VA_STATUS_SUCCESS)
        return status;

    status = copy_surface_to_image(driver_data, surface, image);
    if (status != VA_STATUS_SUCCESS)
        return status;

    surface.status = VASurfaceReady;

    if (!driver_data->buffers.contains(image->buf)) {
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }
    driver_data->buffers.at(image->buf).derived_surface_id = surface_id;

    return VA_STATUS_SUCCESS;
}

VAStatus queryImageFormats(VADriverContextP context, VAImageFormat* formats, int* formats_count)
{
    formats[0].fourcc = VA_FOURCC_NV12;
    *formats_count = 1;

    return VA_STATUS_SUCCESS;
}

VAStatus setImagePalette(VADriverContextP context, VAImageID image_id, unsigned char* palette)
{
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus getImage(VADriverContextP context, VASurfaceID surface_id, int x, int y, unsigned int width,
    unsigned int height, VAImageID image_id)
{
    auto driver_data = static_cast<DriverData*>(context->pDriverData);

    if (!driver_data->surfaces.contains(surface_id)) {
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }

    if (!driver_data->images.contains(image_id)) {
        return VA_STATUS_ERROR_INVALID_IMAGE;
    }
    auto& image = driver_data->images.at(image_id);

    if (x != 0 || y != 0 || width != image.width || height != image.height)
        return VA_STATUS_ERROR_UNIMPLEMENTED;

    return copy_surface_to_image(driver_data, driver_data->surfaces.at(surface_id), &image);
}

VAStatus putImage(VADriverContextP context, VASurfaceID surface_id, VAImageID image, int src_x, int src_y,
    unsigned int src_width, unsigned int src_height, int dst_x, int dst_y, unsigned int dst_width,
    unsigned int dst_height)
{
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}
