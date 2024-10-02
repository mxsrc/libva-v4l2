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
	'driver.cc',
	'config.cc',
	'surface.cc',
	'context.cc',
	'buffer.cc',
	'picture.cc',
	'subpicture.cc',
	'image.cc',
	'utils.cc',
	'video.cc',
	'media.cc',
	'v4l2.cc',
	'mpeg2.cc',
	'h264.cc',
	'vp8.cc',
]

headers = [
	'driver.h',
	'config.h',
	'surface.h',
	'context.h',
	'buffer.h',
	'picture.h',
	'subpicture.h',
	'image.h',
	'utils.h',
	'video.h',
	'media.h',
	'v4l2.h',
	'mpeg2.h',
	'h264.h',
	'vp8.h',
]
cpp_args = [
	'-Wall',
	'-fvisibility=hidden',
	'-DVA_DRIVER_INIT_FUNC=' + va_driver_init_func,
	'-D__STDC_WANT_LIB_EXT2__',
	'-DGST_USE_UNSTABLE_API',
	'-std=c++20',
]

if libgstcodecparsers_dep.found() and libgstcodecs_dep.found()
	sources += 'vp9.cc'
	headers += 'vp9.h'
	cpp_args += '-DENABLE_VP9'
endif

v4l2_drv_video = shared_module('v4l2_drv_video',
	name_prefix: '',
	install: true,
	install_dir: join_paths(get_option('libdir'), 'dri'),
	cpp_args: cpp_args,
	sources: [ sources, headers ],
	dependencies: [
		libva_dep,
		libdrm_dep,
		libgstcodecparsers_dep,
		libgstcodecs_dep,
		libudev_dep,
		kernel_dep,
	])
