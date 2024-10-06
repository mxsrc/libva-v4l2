/*
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

#include "media.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <linux/media.h>
#include <sys/ioctl.h>
#include <sys/select.h>
}

#include "utils.h"

int media_request_alloc(int media_fd)
{
    int fd;
    int rc;

    rc = ioctl(media_fd, MEDIA_IOC_REQUEST_ALLOC, &fd);
    if (rc < 0) {
        request_log("Unable to allocate media request: %s\n", strerror(errno));
        return -1;
    }

    return fd;
}

int media_request_reinit(int request_fd)
{
    int rc;

    rc = ioctl(request_fd, MEDIA_REQUEST_IOC_REINIT, NULL);
    if (rc < 0) {
        request_log("Unable to reinit media request: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int media_request_queue(int request_fd)
{
    int rc;

    rc = ioctl(request_fd, MEDIA_REQUEST_IOC_QUEUE, NULL);
    if (rc < 0) {
        request_log("Unable to queue media request: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int media_request_wait_completion(int request_fd)
{
    timeval tv = { 0, 300000 };
    fd_set except_fds;
    int rc;

    FD_ZERO(&except_fds);
    FD_SET(request_fd, &except_fds);

    rc = select(request_fd + 1, NULL, NULL, &except_fds, &tv);
    if (rc == 0) {
        request_log("Timeout when waiting for media request\n");
        return -1;
    } else if (rc < 0) {
        request_log("Unable to select media request: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}
