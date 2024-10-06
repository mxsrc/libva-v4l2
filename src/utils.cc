/*
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
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

#include "utils.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "request.h"

void request_log(const char* format, ...)
{
    va_list arguments;

    fprintf(stderr, "%s: ", V4L2_REQUEST_STR_VENDOR);

    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
}

void info_log(VADriverContextP ctx, const char* format, ...)
{
    char* string;
    va_list args;

    va_start(args, format);
    if (0 > vasprintf(&string, format, args))
        string = NULL; // this is for logging, so failed allocation is not fatal
    va_end(args);

    if (string) {
        ctx->info_callback(ctx, string);
        free(string);
    } else {
        ctx->error_callback(ctx, "Error while logging a message: Memory allocation failed.\n");
    }
}

void error_log(VADriverContextP ctx, const char* format, ...)
{
    char* string;
    va_list args;

    va_start(args, format);
    if (0 > vasprintf(&string, format, args))
        string = NULL; // this is for logging, so failed allocation is not fatal
    va_end(args);

    if (string) {
        ctx->error_callback(ctx, string);
        free(string);
    } else {
        ctx->error_callback(ctx, "Error while logging a message: Memory allocation failed.\n");
    }
}
