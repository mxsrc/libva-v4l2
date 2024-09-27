/*
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2023 Max Schettler <max.schettler@posteo.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <map>
#include <stdexcept>
#include <system_error>

extern "C" {
#include <va/va_backend.h>
}

template<typename F, typename... Args>
std::invoke_result_t<F, Args...>
errno_wrapper(F f, Args... args) {
	std::invoke_result_t<F, Args...> result = f(std::forward<Args>(args)...);
	if (result < 0) {
		throw std::system_error(errno, std::generic_category());
	}
	return result;
}

/**
 * libVA-independent error log.
 */
void request_log(const char *format, ...);

/**
 * Utility function to access the libVA info callback.
 */
void info_log(VADriverContextP ctx, const char *format, ...) __attribute__ ((format (printf, 2, 3)));

/**
 * Utility function to access the libVA error callback.
 */
void error_log(VADriverContextP ctx, const char *format, ...) __attribute__ ((format (printf, 2, 3)));

template<typename K, typename V>
K smallest_free_key(const std::map<K, V>& map) {
	K i = {};

	for (auto it = map.cbegin(), end = map.cend();
				   it != end && i == it->first; ++it, ++i)
	{ }

	return i;
}
