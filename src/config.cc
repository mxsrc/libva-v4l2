/*
 * Copyright (C) 2007 Intel Corporation
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
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

#include "config.h"

#include <algorithm>
#include <cassert>
#include <cstring>

extern "C" {
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <va/va.h>
}

#include "request.h"
#include "utils.h"
#include "v4l2.h"

VAStatus RequestCreateConfig(VADriverContextP context, VAProfile profile,
			     VAEntrypoint entrypoint,
			     VAConfigAttrib *attributes, int attributes_count,
			     VAConfigID *config_id)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	int i, index;

 	// TODO: Should check whether profile is actually supported by driver in use.
	switch (profile) {
	case VAProfileMPEG2Simple:
	case VAProfileMPEG2Main:
	case VAProfileH264Main:
	case VAProfileH264High:
	case VAProfileH264ConstrainedBaseline:
	case VAProfileH264MultiviewHigh:
	case VAProfileH264StereoHigh:
	case VAProfileVP8Version0_3:
	case VAProfileVP9Profile0:
	case VAProfileVP9Profile1:
	case VAProfileVP9Profile2:
	case VAProfileVP9Profile3:
		if (entrypoint != VAEntrypointVLD)
			return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
		break;

	default:
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
	}

	if (static_cast<unsigned>(attributes_count) > Config::max_attributes) {
		attributes_count = Config::max_attributes;
	}

	*config_id = smallest_free_key(driver_data->configs);
	auto [config, inserted] = driver_data->configs.emplace(std::make_pair(*config_id, Config{
		.profile = profile,
		.entrypoint = entrypoint,
		.attributes{{VAConfigAttribRTFormat, VA_RT_FORMAT_YUV420}},
		.attributes_count = 1,
	}));
	if (!inserted) {
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
	}

	for (i = 1; i < attributes_count; i++) {
		index = config->second.attributes_count++;
		config->second.attributes[index].type = attributes[index].type;
		config->second.attributes[index].value = attributes[index].value;
	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestDestroyConfig(VADriverContextP context, VAConfigID config_id)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);

	if (!driver_data->configs.erase(config_id)) {
		return VA_STATUS_ERROR_INVALID_CONFIG;
	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestQueryConfigProfiles(VADriverContextP context,
				    VAProfile *profiles, int *profiles_count)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	unsigned int index = 0;
	bool found;

	found = v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_MPEG2) ||
		v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_MPEG2_SLICE);
	if (found && index < (Config::max_attributes - 2)) {
		profiles[index++] = VAProfileMPEG2Simple;
		profiles[index++] = VAProfileMPEG2Main;
	}

	found = v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_H264) ||
		v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_H264_SLICE);
	if (found && index < (Config::max_attributes - 5)) {
		// TODO: Query `h264_profile` to determine exact supported profile set
		profiles[index++] = VAProfileH264Main;
		profiles[index++] = VAProfileH264High;
		profiles[index++] = VAProfileH264ConstrainedBaseline;
		profiles[index++] = VAProfileH264MultiviewHigh;
		profiles[index++] = VAProfileH264StereoHigh;
	}

	found = v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_VP8_FRAME);
	if (found && index < (Config::max_attributes - 1)) {
		profiles[index++] = VAProfileVP8Version0_3;
	}

	found = v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_VP9) ||
		v4l2_find_format(driver_data->device.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_VP9_FRAME);
	if (found && index < (Config::max_attributes - 1)) {
		// TODO: Query `vp9_profile` to determine exact supported profile set
		profiles[index++] = VAProfileVP9Profile0;
		profiles[index++] = VAProfileVP9Profile1;
		profiles[index++] = VAProfileVP9Profile2;
		profiles[index++] = VAProfileVP9Profile3;
	}


	*profiles_count = index;

	return VA_STATUS_SUCCESS;
}

VAStatus RequestQueryConfigEntrypoints(VADriverContextP context,
				       VAProfile profile,
				       VAEntrypoint *entrypoints,
				       int *entrypoints_count)
{
	switch (profile) {
	case VAProfileMPEG2Simple:
	case VAProfileMPEG2Main:
	case VAProfileH264Main:
	case VAProfileH264High:
	case VAProfileH264ConstrainedBaseline:
	case VAProfileH264MultiviewHigh:
	case VAProfileH264StereoHigh:
	case VAProfileVP8Version0_3:
	case VAProfileVP9Profile0:
	case VAProfileVP9Profile1:
	case VAProfileVP9Profile2:
	case VAProfileVP9Profile3:
		entrypoints[0] = VAEntrypointVLD;
		*entrypoints_count = 1;
		break;

	default:
		*entrypoints_count = 0;
		break;
	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestQueryConfigAttributes(VADriverContextP context,
				      VAConfigID config_id, VAProfile *profile,
				      VAEntrypoint *entrypoint,
				      VAConfigAttrib *attributes,
				      int *attributes_count)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);

	if (!driver_data->configs.contains(config_id)) {
		return VA_STATUS_ERROR_INVALID_CONFIG;
	}
	const auto& config = driver_data->configs.at(config_id);

	if (profile != NULL)
		*profile = config.profile;

	if (entrypoint != NULL)
		*entrypoint = config.entrypoint;

	if (attributes_count != NULL) {
		*attributes_count = config.attributes_count;
	}

	/* Attributes might be NULL to retrieve the associated count. */
	if (attributes != NULL) {
		std::copy_n(config.attributes, config.attributes_count, attributes);
	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestGetConfigAttributes(VADriverContextP context, VAProfile profile,
				    VAEntrypoint entrypoint,
				    VAConfigAttrib *attributes,
				    int attributes_count)
{
	for (int i = 0; i < attributes_count; i++) {
		switch (attributes[i].type) {
		case VAConfigAttribRTFormat:
			attributes[i].value = VA_RT_FORMAT_YUV420;
			break;
		default:
			attributes[i].value = VA_ATTRIB_NOT_SUPPORTED;
			break;
		}
	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestQueryDisplayAttributes(VADriverContextP context,
				       VADisplayAttribute *attributes,
				       int *attributes_count)
{
	return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus RequestGetDisplayAttributes(VADriverContextP context,
				     VADisplayAttribute *attributes,
				     int attributes_count)
{
	return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus RequestSetDisplayAttributes(VADriverContextP context,
				     VADisplayAttribute *attributes,
				     int attributes_count)
{
	return VA_STATUS_ERROR_UNIMPLEMENTED;
}
