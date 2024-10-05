/*
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

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

extern "C" {
#include <linux/videodev2.h>
}

#define SOURCE_SIZE_MAX (1024 * 1024)

using fourcc = uint32_t;

class V4L2M2MDevice {
public:
    class Buffer {
    public:
        void queue(int request_fd = -1, timeval* timestamp = nullptr, unsigned size = 0) const;
        void dequeue() const;
        std::vector<int> export_(unsigned flags) const;
        std::vector<std::span<uint8_t>> mapping() const { return mapping_; }
        V4L2M2MDevice& owner() const { return owner_; }

        Buffer(V4L2M2MDevice& owner, v4l2_buf_type type, unsigned index);
        Buffer(Buffer&& other);
        Buffer& operator=(Buffer&& other);
        ~Buffer();

    private:
        V4L2M2MDevice& owner_;
        v4l2_buf_type type_;
        unsigned index_;
        std::vector<std::span<uint8_t>> mapping_;

        friend class V4L2M2MDevice;
    };

    static std::vector<std::pair<std::string, std::string>> enumerate_devices();

    V4L2M2MDevice(const std::string& video_path, const std::optional<std::string>& media_path);
    V4L2M2MDevice(V4L2M2MDevice&& other);
    V4L2M2MDevice& operator=(V4L2M2MDevice&& other);
    ~V4L2M2MDevice();
    void set_format(enum v4l2_buf_type type, unsigned int pixelformat, unsigned int width, unsigned int height);
    unsigned request_buffers(enum v4l2_buf_type type, unsigned count);
    bool format_supported(v4l2_buf_type type, unsigned pixelformat) const;
    const Buffer& buffer(v4l2_buf_type type, unsigned index);
    void set_control(int request_fd, unsigned id, void* data, unsigned size);
    void set_controls(int request_fd, std::span<v4l2_ext_control> controls);
    void set_streaming(bool enable);

    int video_fd;
    int media_fd;
    v4l2_format capture_format;
    v4l2_format output_format;
    std::set<fourcc> supported_output_formats;
    std::set<fourcc> supported_capture_formats;

private:
    std::vector<Buffer> capture_buffers;
    std::vector<Buffer> output_buffers;
};
