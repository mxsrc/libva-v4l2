#pragma once

extern "C" {
#include <va/va.h>
}

#include "buffer.h"
#include "surface.h"

struct Context;
struct RequestData;

VAStatus vp8_store_buffer(RequestData *driver_data,
			  Surface& surface,
			  const Buffer& buffer);
int vp8_set_controls(RequestData *data,
		     const Context& context,
		     Surface& surface);
