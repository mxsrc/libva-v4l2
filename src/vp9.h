#pragma once

extern "C" {
#include <va/va.h>
}

#include "buffer.h"
#include "surface.h"

struct Context;
struct RequestData;

VAStatus vp9_store_buffer(RequestData *driver_data,
			  Surface& surface,
			  const Buffer& buffer);
int vp9_set_controls(RequestData *data,
		     const Context& context,
		     Surface& surface);
