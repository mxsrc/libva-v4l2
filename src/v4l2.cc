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

#include "v4l2.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <system_error>

extern "C" {
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
}

#include "utils.h"

template<typename F, typename... Args>
std::invoke_result_t<F, Args...>
errno_wrapper(F f, Args... args) {
	std::invoke_result_t<F, Args...> result = f(std::forward<Args>(args)...);
	if (result < 0) {
		throw std::system_error(errno, std::generic_category());
	}
	return result;
}

static unsigned query_capabilities(int video_fd) {
	struct v4l2_capability capability = {};
	errno_wrapper(ioctl, video_fd, VIDIOC_QUERYCAP, &capability);

	if ((capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0) {
		return capability.device_caps;
	} else {
		return capability.capabilities;
	}
}

static v4l2_format get_format(int video_fd, v4l2_buf_type type) {
	v4l2_format result = { .type = type };
	errno_wrapper(ioctl, video_fd, VIDIOC_G_FMT, &result);
	return result;
}

V4L2M2MDevice::V4L2M2MDevice(const std::string& video_path, const std::optional<std::string> media_path) :
		video_fd(errno_wrapper(open, video_path.c_str(), O_RDWR | O_NONBLOCK)),
		media_fd((media_path) ? errno_wrapper(open, media_path->c_str(), O_RDWR | O_NONBLOCK) : -1),
		capture_format(get_format(video_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)),
		output_format(get_format(video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)) {
	if (!(query_capabilities(video_fd) & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE))) {
		std::runtime_error("Missing device capabilities");
	}
}

V4L2M2MDevice::~V4L2M2MDevice() {
	if (video_fd >= 0) {
		close(video_fd);
		video_fd = -1;
	}
	if (media_fd >= 0) {
		close(media_fd);
		media_fd = -1;
	}
}

void V4L2M2MDevice::set_format(v4l2_buf_type type, unsigned int pixelformat,
		    unsigned int width, unsigned int height) {
	struct v4l2_format* format = V4L2_TYPE_IS_CAPTURE(type) ? &capture_format : &output_format;

	format->type = type;
	format->fmt.pix_mp.pixelformat = pixelformat;
	format->fmt.pix_mp.width = width;
	format->fmt.pix_mp.height = height;

	// Automatic size is insufficient for data buffers
	format->fmt.pix_mp.plane_fmt[0].sizeimage = V4L2_TYPE_IS_OUTPUT(type) ? SOURCE_SIZE_MAX : 0;

	errno_wrapper(ioctl, video_fd, VIDIOC_S_FMT, format);
}

unsigned V4L2M2MDevice::request_buffers(v4l2_buf_type type, unsigned count) {
	struct v4l2_requestbuffers buffers = {
		.count = count,
		.type = type,
		.memory = V4L2_MEMORY_MMAP,
	};

	errno_wrapper(ioctl, video_fd, VIDIOC_REQBUFS, &buffers);

	return buffers.count;  // Actual amount may differ
}

bool V4L2M2MDevice::format_supported(v4l2_buf_type type, unsigned pixelformat) {
	for (
			v4l2_fmtdesc fmtdesc = { .type = type };
			ioctl(video_fd, VIDIOC_ENUM_FMT, &fmtdesc) >= 0;
			fmtdesc.index += 1) {
		if (fmtdesc.pixelformat == pixelformat) {
			return true;
		}
	}
	return false;
}

unsigned V4L2M2MDevice::query_buffer(v4l2_buf_type type, unsigned index, unsigned *lengths, unsigned *offsets)
{
	struct v4l2_plane planes[VIDEO_MAX_PLANES] = {};
	struct v4l2_buffer buffer = {
		.index = index,
		.type = type,
		.m = { .planes = planes },
		.length = VIDEO_MAX_PLANES,
	};
	errno_wrapper(ioctl, video_fd, VIDIOC_QUERYBUF, &buffer);

	if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
		if (lengths) {
			for (unsigned i = 0; i < buffer.length; i++) {
				lengths[i] = buffer.m.planes[i].length;
			}
		}

		if (offsets) {
			for (unsigned i = 0; i < buffer.length; i++) {
				offsets[i] = buffer.m.planes[i].m.mem_offset;
			}
		}
		return buffer.length;
	} else {
		if (lengths) {
			lengths[0] = buffer.length;
		}

		if (offsets) {
			offsets[0] = buffer.m.offset;
		}
		return 1;
	}
}

void V4L2M2MDevice::queue_buffer(int request_fd, v4l2_buf_type type, timeval* timestamp,
		unsigned index, unsigned size, unsigned buffers_count) {
	struct v4l2_plane planes[VIDEO_MAX_PLANES] = {};
	struct v4l2_buffer buffer = {
		.index = index,
		.type = type,
		.memory = V4L2_MEMORY_MMAP,
		.m = { .planes = planes },
		.length = buffers_count,
	};

	for (unsigned i = 0; i < buffers_count; i++)
		if (V4L2_TYPE_IS_MULTIPLANAR(type))
			buffer.m.planes[i].bytesused = size;
		else
			buffer.bytesused = size;

	if (request_fd >= 0) {
		buffer.flags = V4L2_BUF_FLAG_REQUEST_FD;
		buffer.request_fd = request_fd;
	}

	if (timestamp != NULL)
		buffer.timestamp = *timestamp;

	errno_wrapper(ioctl, video_fd, VIDIOC_QBUF, &buffer);
}

void V4L2M2MDevice::dequeue_buffer(int request_fd, v4l2_buf_type type, unsigned index)
{
	struct v4l2_plane planes[VIDEO_MAX_PLANES] = {};
	struct v4l2_buffer buffer = {
		.index = index,
		.type = type,
		.memory = V4L2_MEMORY_MMAP,
		.m = { .planes = planes },
		.length = VIDEO_MAX_PLANES,
	};

	if (request_fd >= 0) {
		buffer.flags = V4L2_BUF_FLAG_REQUEST_FD;
		buffer.request_fd = request_fd;
	}

	errno_wrapper(ioctl, video_fd, VIDIOC_DQBUF, &buffer);
	if (buffer.flags & V4L2_BUF_FLAG_ERROR) {
		throw std::runtime_error("Dequeued buffer marked erroneous by driver.");
	}
}

void V4L2M2MDevice::export_buffer(v4l2_buf_type type, unsigned index, unsigned flags,
		int *export_fds, unsigned export_fds_count) {
	for (unsigned i = 0; i < export_fds_count; i++) {
		v4l2_exportbuffer exportbuffer = {
			.type = type,
			.index = index,
			.plane = i,
			.flags = flags,
		};

		errno_wrapper(ioctl, video_fd, VIDIOC_EXPBUF, &exportbuffer);
		export_fds[i] = exportbuffer.fd;
	}
}

void V4L2M2MDevice::set_control(int request_fd, unsigned id, void* data, unsigned size) {
	v4l2_ext_control control = {
		.id = id,
		.size = size,
		.ptr = data,
	};
	set_controls(request_fd, std::span(&control, 1));
}

void V4L2M2MDevice::set_controls(int request_fd, std::span<v4l2_ext_control> controls) {
	struct v4l2_ext_controls meta = {
		.count = static_cast<uint32_t>(controls.size()),
		.controls = controls.data(),
	};

	if (request_fd >= 0) {
		meta.which = V4L2_CTRL_WHICH_REQUEST_VAL;
		meta.request_fd = request_fd;
	}

	errno_wrapper(ioctl, video_fd, VIDIOC_S_EXT_CTRLS, &meta);
}

void V4L2M2MDevice::set_streaming(bool enable) {
	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	errno_wrapper(ioctl, video_fd, enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type);
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	errno_wrapper(ioctl, video_fd, enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type);
}
