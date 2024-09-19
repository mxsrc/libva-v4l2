#pragma once

extern "C" {
#include <va/va.h>
}

struct object_buffer;
struct Context;
struct object_surface;
struct RequestData;

VAStatus vp8_store_buffer(RequestData *driver_data,
			  struct object_surface *surface_object,
			  struct object_buffer *buffer_object);
int vp8_set_controls(RequestData *data,
		     const Context& context,
		     struct object_surface *surface);
