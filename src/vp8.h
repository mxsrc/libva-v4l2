#pragma once

extern "C" {
#include <va/va.h>
}

#include "surface.h"

struct object_buffer;
struct Context;
struct RequestData;

VAStatus vp8_store_buffer(RequestData *driver_data,
			  Surface& surface,
			  struct object_buffer *buffer_object);
int vp8_set_controls(RequestData *data,
		     const Context& context,
		     Surface& surface);
