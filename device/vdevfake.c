/******************************************************************************
 * Copyright (C) 2014-2018 Zhifeng Gong <gozfree@163.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libraries; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <libmacro.h>
#include <liblog.h>
#include "device.h"
#include "imgconvert.h"

struct vdev_fake_ctx {
    int fd;
    int on_read_fd;
    int on_write_fd;
    int width;
    int height;
    struct iovec buf;
    int buf_index;
    int req_count;
};

static int vf_open(struct device_ctx *dc, const char *dev, struct media_params *media)
{
    int fd = -1;
    int fds[2];
    char notify = '1';
    struct vdev_fake_ctx *vc = CALLOC(1, struct vdev_fake_ctx);
    if (!vc) {
        loge("malloc vdev_fake_ctx failed!\n");
        return -1;
    }

    if (pipe(fds)) {
        loge("create pipe failed(%d): %s\n", errno, strerror(errno));
        goto failed;
    }
    vc->on_read_fd = fds[0];
    vc->on_write_fd = fds[1];

    fd = open(dev, O_RDWR);
    if (fd == -1) {
        loge("open %s failed: %s\n", dev, strerror(errno));
        goto failed;
    }
    vc->fd = fd;
    vc->width = media->video.width;
    vc->height = media->video.height;

    dc->fd = vc->on_read_fd;//use pipe fd to trigger event
    dc->media.video.width = vc->width;
    dc->media.video.height = vc->height;
    dc->media.video.pix_fmt = YUV420;
    if (write(vc->on_write_fd, &notify, 1) != 1) {
        loge("Failed writing to notify pipe\n");
        goto failed;
    }
    dc->priv = vc;
    return 0;

failed:
    if (vc) {
        free(vc);
    }
    if (fd != -1) {
        close(fd);
    }
    if (fds[0] != -1 || fds[1] != -1) {
        close(fds[0]);
        close(fds[1]);
    }
    return -1;
}

static int vf_read(struct device_ctx *dc, void *buf, int len)
{
    struct vdev_fake_ctx *vc = (struct vdev_fake_ctx *)dc->priv;
    int flen;
    char notify;

    if (read(vc->on_read_fd, &notify, sizeof(notify)) != 1) {
        loge("Failed read from notify pipe");
    }

    flen = read(vc->fd, buf, len);
    lseek(vc->fd, 0L, SEEK_SET);
    if (flen == -1) {
        loge("read failed!\n");
        return -1;
    }
    if (flen > len) {
        loge("read frame is %d bytes, but buffer len is %d, not enough!\n", flen, len);
        return -1;
    }
    logd("read frame is %d bytes, but buffer len is %d\n", flen, len);
    usleep(200*1000);

    return len;
}

static int vf_write(struct device_ctx *dc, void *buf, int len)
{
    char notify = '1';
    struct vdev_fake_ctx *vc = (struct vdev_fake_ctx *)dc->priv;
    if (write(vc->on_write_fd, &notify, 1) != 1) {
        loge("Failed writing to notify pipe: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static void vf_close(struct device_ctx *dc)
{
    struct vdev_fake_ctx *vc = (struct vdev_fake_ctx *)dc->priv;
    if (!vc) {
        return;
    }

    close(vc->fd);
    close(vc->on_read_fd);
    close(vc->on_write_fd);
    free(vc);
    vc = NULL;
}


struct device aq_vdevfake_device = {
    .name  = "vdevfake",
    .open  = vf_open,
    .read  = vf_read,
    .write = vf_write,
    .ioctl = NULL,
    .close = vf_close,
};
