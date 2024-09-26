#pragma once

#include <vector>

extern "C" {
#include <va/va.h>
}

struct Buffer;
struct Context;
struct RequestData;
struct Surface;
class V4L2M2MDevice;

VAStatus vp8_store_buffer(RequestData *driver_data,
			  Surface& surface,
			  const Buffer& buffer);
int vp8_set_controls(RequestData *data,
		     const Context& context,
		     Surface& surface);
std::vector<VAProfile> vp8_supported_profiles(const V4L2M2MDevice& device);
