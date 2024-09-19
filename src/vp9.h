#pragma once

extern "C" {
#include <va/va.h>
}

struct object_buffer;
struct object_context;
struct object_surface;
struct RequestData;

VAStatus vp9_store_buffer(RequestData *driver_data,
			  struct object_surface *surface_object,
			  struct object_buffer *buffer_object);
int vp9_set_controls(RequestData *data,
		     struct object_context *context,
		     struct object_surface *surface);
