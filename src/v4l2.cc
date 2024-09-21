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
	struct v4l2_capability capability = {0};
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

bool v4l2_find_format(int video_fd, enum v4l2_buf_type type, unsigned int pixelformat)
{
	struct v4l2_fmtdesc fmtdesc;
	int rc;

	memset(&fmtdesc, 0, sizeof(fmtdesc));
	fmtdesc.type = type;
	fmtdesc.index = 0;

	do {
		rc = ioctl(video_fd, VIDIOC_ENUM_FMT, &fmtdesc);
		if (rc < 0)
			break;

		if (fmtdesc.pixelformat == pixelformat)
			return true;

		fmtdesc.index++;
	} while (rc >= 0);

	return false;
}

int v4l2_query_buffer(int video_fd, enum v4l2_buf_type type, unsigned int index,
		      unsigned int *lengths, unsigned int *offsets,
		      unsigned* buffers_count)
{
	struct v4l2_plane planes[VIDEO_MAX_PLANES] = {};
	struct v4l2_buffer buffer = {
		.index = index,
		.type = type,
		.m = { .planes = planes },
		.length = VIDEO_MAX_PLANES,
	};
	unsigned int i;
	int rc;

	rc = ioctl(video_fd, VIDIOC_QUERYBUF, &buffer);
	if (rc < 0) {
		request_log("Unable to query buffer: %s\n", strerror(errno));
		return -1;
	}

	if (buffers_count) {
		*buffers_count = buffer.length;
	}
	if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
		if (lengths != NULL)
			for (i = 0; i < buffer.length; i++)
				lengths[i] = buffer.m.planes[i].length;

		if (offsets != NULL)
			for (i = 0; i < buffer.length; i++)
				offsets[i] = buffer.m.planes[i].m.mem_offset;
	} else {
		if (lengths != NULL)
			lengths[0] = buffer.length;

		if (offsets != NULL)
			offsets[0] = buffer.m.offset;
	}

	return 0;
}

int v4l2_queue_buffer(int video_fd, int request_fd, enum v4l2_buf_type type,
		      struct timeval *timestamp, unsigned int index,
		      unsigned int size, unsigned int buffers_count)
{
	struct v4l2_plane planes[buffers_count];
	struct v4l2_buffer buffer;
	unsigned int i;
	int rc;

	memset(planes, 0, sizeof(planes));
	memset(&buffer, 0, sizeof(buffer));

	buffer.type = type;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.index = index;
	buffer.length = buffers_count;
	buffer.m.planes = planes;

	for (i = 0; i < buffers_count; i++)
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

	rc = ioctl(video_fd, VIDIOC_QBUF, &buffer);
	if (rc < 0) {
		request_log("Unable to queue buffer: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

int v4l2_dequeue_buffer(int video_fd, int request_fd, enum v4l2_buf_type type,
			unsigned int index, unsigned int buffers_count)
{
	struct v4l2_plane planes[buffers_count];
	struct v4l2_buffer buffer;
	int rc;

	memset(planes, 0, sizeof(planes));
	memset(&buffer, 0, sizeof(buffer));

	buffer.type = type;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.index = index;
	buffer.length = buffers_count;
	buffer.m.planes = planes;

	if (request_fd >= 0) {
		buffer.flags = V4L2_BUF_FLAG_REQUEST_FD;
		buffer.request_fd = request_fd;
	}

	rc = ioctl(video_fd, VIDIOC_DQBUF, &buffer);
	if (rc < 0) {
		request_log("Unable to dequeue buffer: %s\n", strerror(errno));
		return -1;
	}
	if (buffer.flags & V4L2_BUF_FLAG_ERROR) {
		request_log("Dequeued buffer marked erroneous by driver.\n");
		return -1;
	}

	return 0;
}

int v4l2_export_buffer(int video_fd, enum v4l2_buf_type type, unsigned int index,
		       unsigned int flags, int *export_fds,
		       unsigned int export_fds_count)
{
	struct v4l2_exportbuffer exportbuffer;
	unsigned int i;
	int rc;

	for (i = 0; i < export_fds_count; i++) {
		memset(&exportbuffer, 0, sizeof(exportbuffer));
		exportbuffer.type = type;
		exportbuffer.index = index;
		exportbuffer.plane = i;
		exportbuffer.flags = flags;

		rc = ioctl(video_fd, VIDIOC_EXPBUF, &exportbuffer);
		if (rc < 0) {
			request_log("Unable to export buffer: %s\n",
				    strerror(errno));
			return -1;
		}

		export_fds[i] = exportbuffer.fd;
	}

	return 0;
}

int v4l2_set_control(int video_fd, int request_fd, unsigned int id, void *data,
		     unsigned int size)
{
	struct v4l2_ext_control control;
	struct v4l2_ext_controls controls;
	int rc;

	memset(&control, 0, sizeof(control));
	memset(&controls, 0, sizeof(controls));

	control.id = id;
	control.ptr = data;
	control.size = size;

	controls.controls = &control;
	controls.count = 1;

	if (request_fd >= 0) {
		controls.which = V4L2_CTRL_WHICH_REQUEST_VAL;
		controls.request_fd = request_fd;
	}

	rc = ioctl(video_fd, VIDIOC_S_EXT_CTRLS, &controls);
	if (rc < 0) {
		request_log("Unable to set control: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

int v4l2_set_controls(int video_fd, int request_fd, struct v4l2_ext_control* controls,
		     unsigned int count)
{
	struct v4l2_ext_controls meta = { 0 };
	int rc;

	meta.controls = controls;
	meta.count = count;

	if (request_fd >= 0) {
		meta.which = V4L2_CTRL_WHICH_REQUEST_VAL;
		meta.request_fd = request_fd;
	}

	rc = ioctl(video_fd, VIDIOC_S_EXT_CTRLS, &meta);
	if (rc < 0) {
		request_log("Unable to set control: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

int v4l2_set_stream(int video_fd, enum v4l2_buf_type type, bool enable)
{
	enum v4l2_buf_type buf_type = type;
	int rc;

	rc = ioctl(video_fd, enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF,
		   &buf_type);
	if (rc < 0) {
		request_log("Unable to %sable stream: %s\n",
			    enable ? "en" : "dis", strerror(errno));
		return -1;
	}

	return 0;
}
