/*-*- Mode: C; c-basic-offset: 8 -*-*/

/***
    This file is part of SmartKit.

    Copyright 2008 Lennart Poettering

    SmartKit is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation, either version 2.1 of the
    License, or (at your option) any later version.

    SmartKit is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with SmartKit. If not, If not, see
    <http://www.gnu.org/licenses/>.
***/

#include <string.h>
#include <errno.h>

#include "smart.h"

int main(int argc, char *argv[]) {
        int ret;
        const char *device;
        SkDisk *d;

        if (argc != 2) {
                g_printerr("%s [DEVICE]\n", argv[0]);
                return 1;
        }

        device = argv[1];

        if ((ret = sk_disk_open(device, &d)) < 0) {
                g_printerr("Failed to open disk %s: %s\n", device, g_strerror(errno));
                return 1;
        }

        if ((ret = sk_disk_dump(d)) < 0) {
                g_printerr("Failed to dump disk data: %s\n", g_strerror(errno));
                return 1;
        }

        sk_disk_free(d);

        return 0;
}
