/*
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

#include <optional>
#include <string>
#include <span>
#include <vector>

extern "C" {
#include <linux/videodev2.h>
}

#define SOURCE_SIZE_MAX						(1024 * 1024)

class V4L2M2MDevice {
public:
	V4L2M2MDevice(const std::string& video_path, const std::optional<std::string> media_path);
	~V4L2M2MDevice();
	void set_format(enum v4l2_buf_type type, unsigned int pixelformat, unsigned int width, unsigned int height);
	unsigned request_buffers(enum v4l2_buf_type type, unsigned count);
	bool format_supported(v4l2_buf_type type, unsigned pixelformat);
	unsigned query_buffer(v4l2_buf_type type, unsigned index, unsigned *lengths, unsigned *offsets);
	void queue_buffer(int request_fd, v4l2_buf_type type, timeval* timestamp,
			unsigned index, unsigned size, unsigned buffers_count);
	void dequeue_buffer(int request_fd, v4l2_buf_type type, unsigned index);
	void export_buffer(v4l2_buf_type type, unsigned index, unsigned flags,
			int *export_fds, unsigned export_fds_count);
	void set_control(int request_fd, unsigned id, void* data, unsigned size);
	void set_controls(int request_fd, std::span<v4l2_ext_control> controls);
	void set_streaming(bool enable);

	int video_fd;
	int media_fd;
	struct v4l2_format capture_format;
	struct v4l2_format output_format;
	unsigned capture_buffer_count;
	unsigned output_buffer_count;
};
