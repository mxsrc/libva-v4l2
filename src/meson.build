# Copyright (C) 2019 Bootlin
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sub license, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice (including the
# next paragraph) shall be included in all copies or substantial portions
# of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
# IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
# ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

sources = [
	'request.c',
	'object_heap.c',
	'config.c',
	'surface.c',
	'context.c',
	'buffer.c',
	'picture.c',
	'subpicture.c',
	'image.c',
	'utils.c',
	'tiled_yuv.S',
	'video.c',
	'media.c',
	'v4l2.c',
	'mpeg2.c',
	'h264.c',
	'h265.c',
	'vp8.c',
	'vp9.c',
]

headers = [
	'request.h',
	'object_heap.h',
	'config.h',
	'surface.h',
	'context.h',
	'buffer.h',
	'picture.h',
	'subpicture.h',
	'image.h',
	'utils.h',
	'tiled_yuv.h',
	'video.h',
	'media.h',
	'v4l2.h',
	'mpeg2.h',
	'h264.h',
	'h265.h',
	'vp8.h',
	'vp9.h',
]

v4l2_request_drv_video = shared_module('v4l2_request_drv_video',
	name_prefix: '',
	install: true,
	install_dir: join_paths(get_option('libdir'), 'dri'),
	c_args: [
		'-Wall',
		'-fvisibility=hidden',
		'-DVA_DRIVER_INIT_FUNC=' + va_driver_init_func,
		'-D__STDC_WANT_LIB_EXT2__',
		'-DGST_USE_UNSTABLE_API',
	],
	sources: [ sources, headers ],
	dependencies: [
		libva_dep,
		libdrm_dep,
		libgstcodecparsers_dep,
		libgstcodecs_dep,
	])
