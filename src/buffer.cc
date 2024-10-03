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

#include "buffer.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>

extern "C" {
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <va/va.h>
#include <va/va_drmcommon.h>
}

#include "driver.h"
#include "utils.h"
#include "v4l2.h"

Buffer::Buffer(VABufferType type, unsigned count, unsigned size, VASurfaceID derived_surface_id = VA_INVALID_ID)
    : type(type)
    , initial_count(count)
    , count(count)
    , data(static_cast<uint8_t*>(malloc(size * count)))
    , size(size)
    , derived_surface_id(derived_surface_id)
    , info({ .handle = static_cast<uintptr_t>(-1) })
{
}

VAStatus createBuffer(VADriverContextP context, VAContextID context_id, VABufferType type, unsigned int size,
    unsigned int count, void* data, VABufferID* buffer_id)
{
    auto driver_data = static_cast<DriverData*>(context->pDriverData);

    switch (type) {
    case VAPictureParameterBufferType:
    case VAIQMatrixBufferType:
    case VASliceParameterBufferType:
    case VASliceDataBufferType:
    case VAImageBufferType:
    case VAProbabilityBufferType:
        break;

    default:
        return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
    }

    std::lock_guard<std::mutex> guard(driver_data->mutex);
    *buffer_id = smallest_free_key(driver_data->buffers);
    auto [buffer, inserted] = driver_data->buffers.emplace(std::make_pair(*buffer_id, Buffer(type, count, size)));
    if (!inserted || !buffer->second.data) {
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    if (data) {
        std::copy_n(static_cast<uint8_t*>(data), size * count, buffer->second.data.get());
    }

    return VA_STATUS_SUCCESS;
}

VAStatus destroyBuffer(VADriverContextP context, VABufferID buffer_id)
{
    auto driver_data = static_cast<DriverData*>(context->pDriverData);

    std::lock_guard<std::mutex> guard(driver_data->mutex);
    auto buffer_it = driver_data->buffers.find(buffer_id);
    if (buffer_it == driver_data->buffers.end()) {
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }
    driver_data->buffers.erase(buffer_it);

    return VA_STATUS_SUCCESS;
}

VAStatus mapBuffer(VADriverContextP context, VABufferID buffer_id, void** data_map)
{
    auto driver_data = static_cast<DriverData*>(context->pDriverData);

    if (!driver_data->buffers.contains(buffer_id)) {
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }

    /* Our buffers are always mapped. */
    *data_map = driver_data->buffers.at(buffer_id).data.get();

    return VA_STATUS_SUCCESS;
}

VAStatus unmapBuffer(VADriverContextP context, VABufferID buffer_id)
{
    auto driver_data = static_cast<DriverData*>(context->pDriverData);

    /* Our buffers are always mapped. */
    if (!driver_data->buffers.contains(buffer_id)) {
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }

    return VA_STATUS_SUCCESS;
}

VAStatus bufferSetNumElements(VADriverContextP context, VABufferID buffer_id, unsigned int count)
{
    auto driver_data = static_cast<DriverData*>(context->pDriverData);

    if (!driver_data->buffers.contains(buffer_id)) {
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }
    auto& buffer = driver_data->buffers.at(buffer_id);

    if (count > buffer.initial_count)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    buffer.count = count;

    return VA_STATUS_SUCCESS;
}

VAStatus bufferInfo(
    VADriverContextP context, VABufferID buffer_id, VABufferType* type, unsigned int* size, unsigned int* count)
{
    auto driver_data = static_cast<DriverData*>(context->pDriverData);

    if (!driver_data->buffers.contains(buffer_id)) {
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }
    auto& buffer = driver_data->buffers.at(buffer_id);

    *type = buffer.type;
    *size = buffer.size;
    *count = buffer.count;

    return VA_STATUS_SUCCESS;
}

VAStatus acquireBufferHandle(VADriverContextP context, VABufferID buffer_id, VABufferInfo* buffer_info)
{
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus releaseBufferHandle(VADriverContextP context, VABufferID buffer_id)
{
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}
