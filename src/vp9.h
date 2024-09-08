#pragma once

#include <va/va.h>

struct object_buffer;
struct object_context;
struct object_surface;
struct request_data;

VAStatus vp9_store_buffer(struct request_data *driver_data,
			  struct object_surface *surface_object,
			  struct object_buffer *buffer_object);
int vp9_set_controls(struct request_data *data,
		     struct object_context *context,
		     struct object_surface *surface);
