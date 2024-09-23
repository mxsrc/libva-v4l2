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

static std::vector<std::span<uint8_t>> map_buffer(int video_fd, v4l2_buf_type type, unsigned index) {
	struct v4l2_plane planes[VIDEO_MAX_PLANES] = {};
	struct v4l2_buffer buffer = {
		.index = index,
		.type = type,
		.m = { .planes = planes },
		.length = VIDEO_MAX_PLANES,
	};
	errno_wrapper(ioctl, video_fd, VIDIOC_QUERYBUF, &buffer);

	std::vector<std::span<uint8_t>> result(buffer.length);
	for (unsigned i = 0; i < buffer.length; i++) {
		result[i] = {
			static_cast<uint8_t*>(mmap(NULL, buffer.m.planes[i].length, PROT_READ | PROT_WRITE, MAP_SHARED,
						video_fd, buffer.m.planes[i].m.mem_offset)),
			buffer.m.planes[i].length
		};
		if (result[i].data() == MAP_FAILED) {
			throw std::system_error(errno, std::generic_category());
		}
	}
	return result;
}

V4L2M2MDevice::Buffer::Buffer(V4L2M2MDevice& owner, v4l2_buf_type type, unsigned index) :
	owner_(owner), type_(type), index_(index), mapping_(map_buffer(owner.video_fd, type, index)) {}

V4L2M2MDevice::Buffer::Buffer(V4L2M2MDevice::Buffer&& other) :
		owner_(other.owner_), type_(other.type_), index_(other.index_), mapping_(other.mapping_) {
	other.mapping_.clear();
}

V4L2M2MDevice::Buffer& V4L2M2MDevice::Buffer::operator=(V4L2M2MDevice::Buffer&& other) {
	this->~Buffer();
	new (this) V4L2M2MDevice::Buffer(std::move(other));
	return *this;
}

V4L2M2MDevice::Buffer::~Buffer() {
	for (auto&& map : mapping_) {
		munmap(map.data(), map.size());
	}
}

void V4L2M2MDevice::Buffer::queue(int request_fd, timeval* timestamp, unsigned size) const {
	struct v4l2_plane planes[VIDEO_MAX_PLANES] = {};
	struct v4l2_buffer buffer = {
		.index = index_,
		.type = type_,
		.memory = V4L2_MEMORY_MMAP,
		.m = { .planes = planes },
		.length = static_cast<uint32_t>(mapping_.size()),
	};

	for (unsigned i = 0; i < mapping_.size(); i++) {
		if (V4L2_TYPE_IS_MULTIPLANAR(type_)) {
			buffer.m.planes[i].bytesused = size;
		} else {
			buffer.bytesused = size;
		}
	}

	if (request_fd >= 0) {
		buffer.flags = V4L2_BUF_FLAG_REQUEST_FD;
		buffer.request_fd = request_fd;
	}

	if (timestamp != NULL)
		buffer.timestamp = *timestamp;

	errno_wrapper(ioctl, owner_.video_fd, VIDIOC_QBUF, &buffer);
}

void V4L2M2MDevice::Buffer::dequeue() const {
	struct v4l2_plane planes[VIDEO_MAX_PLANES] = {};
	struct v4l2_buffer buffer = {
		.index = index_,
		.type = type_,
		.memory = V4L2_MEMORY_MMAP,
		.m = { .planes = planes },
		.length = VIDEO_MAX_PLANES,
	};

	errno_wrapper(ioctl, owner_.video_fd, VIDIOC_DQBUF, &buffer);
	if (buffer.flags & V4L2_BUF_FLAG_ERROR) {
		throw std::runtime_error("Dequeued buffer marked erroneous by driver.");
	}
}

std::vector<int> V4L2M2MDevice::Buffer::export_(unsigned flags) const {
	std::vector<int> result;
	for (unsigned i = 0; i < mapping_.size(); i++) {
		v4l2_exportbuffer exportbuffer = {
			.type = type_,
			.index = index_,
			.plane = i,
			.flags = flags,
		};

		errno_wrapper(ioctl, owner_.video_fd, VIDIOC_EXPBUF, &exportbuffer);
		result.push_back(exportbuffer.fd);
	}
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
	}
	if (media_fd >= 0) {
		close(media_fd);
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
	struct v4l2_requestbuffers req_buffers = {
		.count = count,
		.type = type,
		.memory = V4L2_MEMORY_MMAP,
	};

	errno_wrapper(ioctl, video_fd, VIDIOC_REQBUFS, &req_buffers);

	auto& buffers = V4L2_TYPE_IS_CAPTURE(type) ? capture_buffers : output_buffers;

	buffers.clear();
	for (unsigned i = 0; i < req_buffers.count; i += 1) {
		buffers.emplace_back(*this, type, i);
	}

	return buffers.size();  // Actual amount may differ
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

const V4L2M2MDevice::Buffer& V4L2M2MDevice::buffer(v4l2_buf_type type, unsigned index) {
	return (V4L2_TYPE_IS_CAPTURE(type) ? capture_buffers : output_buffers)[index];
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
