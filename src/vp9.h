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

VAStatus vp9_store_buffer(RequestData *driver_data,
			  Surface& surface,
			  const Buffer& buffer);
int vp9_set_controls(RequestData *data,
		     const Context& context,
		     Surface& surface);
std::vector<VAProfile> vp9_supported_profiles(const V4L2M2MDevice& device);
