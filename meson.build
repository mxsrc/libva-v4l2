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

project('libva-v4l2-request', 'cpp',
	version: '1.0.0',
	meson_version: '>= 0.56.0')

cc = meson.get_compiler('cpp')

if not (cc.get_define('LINUX_VERSION_MAJOR', prefix: '#include <linux/version.h>').to_int() >= 6)
	error('Require at least Linux 6.0')
endif

libva_dep = dependency('libva', version : '>= 1.1.0')
libdrm_dep = dependency('libdrm', version : '>= 2.4.52')
libgstcodecparsers_dep = dependency('gstreamer-codecparsers-1.0', version : '>= 1.12', required: false)
libgstcodecs_dep = cc.find_library('gstcodecs-1.0',  # Present but not advertised via pkgconfig.
	has_headers: ['gst/codecs/gstvp9statefulparser.h'],
	header_dependencies: libgstcodecparsers_dep,
        required: false,
)
libudev_dep = dependency('libudev', version : '>= 252')

va_api_version_array = libva_dep.version().split('.')
va_api_major_version = va_api_version_array[0]
va_api_minor_version = va_api_version_array[1]

va_driver_init_func = '__vaDriverInit_@0@_@1@'.format(va_api_major_version, va_api_minor_version)

subdir('src')
