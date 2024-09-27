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
#include <functional>
#include <ranges>
#include <span>
#include <vector>

extern "C" {
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <va/va.h>
}

#include "request.h"
#include "utils.h"
#include "v4l2.h"

namespace {

std::vector<VAProfile> supported_profiles(const V4L2M2MDevice& device) {
	std::vector<VAProfile> result;
	for (auto&& profile : supported_profile_funcs
			| std::views::transform([&device](auto&& entry) { return entry.second(device); })
			| std::views::join) {
		result.push_back(profile);
	}
	return result;
}

} // namespace

VAStatus RequestCreateConfig(VADriverContextP context, VAProfile profile,
			     VAEntrypoint entrypoint,
			     VAConfigAttrib *attributes, int attributes_count,
			     VAConfigID *config_id)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	int i, index;

	const auto& supported = supported_profiles(driver_data->device);
	if (std::ranges::find(supported, profile) == supported.end()) {
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
	}
	if (entrypoint != VAEntrypointVLD) {
		return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
	}

	if (static_cast<unsigned>(attributes_count) > Config::max_attributes) {
		attributes_count = Config::max_attributes;
	}

	std::lock_guard<std::mutex> guard(driver_data->mutex);
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

	std::lock_guard<std::mutex> guard(driver_data->mutex);
	if (!driver_data->configs.erase(config_id)) {
		return VA_STATUS_ERROR_INVALID_CONFIG;
	}

	return VA_STATUS_SUCCESS;
}

VAStatus RequestQueryConfigProfiles(VADriverContextP context,
				    VAProfile* profiles_, int* profile_count)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);

	std::span<VAProfile> profiles(profiles_, V4L2_REQUEST_MAX_PROFILES);
	const auto& supported = supported_profiles(driver_data->device);

	*profile_count = std::min(profiles.size(), supported.size());

	std::ranges::copy_n(supported.begin(), *profile_count, profiles.begin());

	return VA_STATUS_SUCCESS;
}

VAStatus RequestQueryConfigEntrypoints(VADriverContextP context,
				       VAProfile profile,
				       VAEntrypoint *entrypoints,
				       int *entrypoints_count)
{
	auto driver_data = static_cast<RequestData*>(context->pDriverData);
	const auto& supported = supported_profiles(driver_data->device);
	if (std::ranges::find(supported, profile) != supported.end()) {
		entrypoints[0] = VAEntrypointVLD;
		*entrypoints_count = 1;
	} else {
		*entrypoints_count = 0;
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
