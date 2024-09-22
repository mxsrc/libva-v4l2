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

#include <string>
#include <optional>

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
	void set_streaming(bool enable);

	int video_fd;
	int media_fd;
	struct v4l2_format capture_format;
	struct v4l2_format output_format;
	unsigned capture_buffer_count;
	unsigned output_buffer_count;
};

int v4l2_query_buffer(int video_fd, enum v4l2_buf_type type, unsigned int index,
		      unsigned int *lengths, unsigned int *offsets,
		      unsigned* buffer_count);
int v4l2_queue_buffer(int video_fd, int request_fd, enum v4l2_buf_type type,
		      struct timeval *timestamp, unsigned int index,
		      unsigned int size, unsigned int buffers_count);
int v4l2_dequeue_buffer(int video_fd, int request_fd, enum v4l2_buf_type type,
			unsigned int index, unsigned int buffers_count);
int v4l2_export_buffer(int video_fd, enum v4l2_buf_type type, unsigned int index,
		       unsigned int flags, int *export_fds,
		       unsigned int export_fds_count);
int v4l2_set_control(int video_fd, int request_fd, unsigned int id, void *data,
		     unsigned int size);
int v4l2_set_controls(int video_fd, int request_fd, struct v4l2_ext_control*,  unsigned int count);
