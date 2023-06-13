#pragma once

struct request_data;
struct object_context;
struct object_surface;

int vp8_set_controls(struct request_data *data,
		     struct object_context *context,
		     struct object_surface *surface);
