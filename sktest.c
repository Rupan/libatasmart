/*-*- Mode: C; c-basic-offset: 8 -*-*/

/***
    This file is part of libatasmart.

    Copyright 2008 Lennart Poettering

    libatasmart is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation, either version 2.1 of the
    License, or (at your option) any later version.

    libatasmart is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with libatasmart. If not, If not, see
    <http://www.gnu.org/licenses/>.
***/

#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "atasmart.h"

int main(int argc, char *argv[]) {
        int ret;
        const char *device;
        SkDisk *d;
        SkSmartSelfTest test;

        if (argc < 3) {
                fprintf(stderr, "%s [DEVICE] [short|extended|conveyance|abort]\n", argv[0]);
                return 1;
        }

        device = argv[1];

        if (!strcasecmp(argv[2], sk_smart_self_test_to_string(SK_SMART_SELF_TEST_SHORT)))
                test = SK_SMART_SELF_TEST_SHORT;
        else if (!strcasecmp(argv[2], sk_smart_self_test_to_string(SK_SMART_SELF_TEST_EXTENDED)))
                test = SK_SMART_SELF_TEST_EXTENDED;
        else if (!(strcasecmp(argv[2], sk_smart_self_test_to_string(SK_SMART_SELF_TEST_CONVEYANCE))))
                test = SK_SMART_SELF_TEST_CONVEYANCE;
        else if (!(strcasecmp(argv[2], sk_smart_self_test_to_string(SK_SMART_SELF_TEST_ABORT))))
                test = SK_SMART_SELF_TEST_ABORT;
        else {
                fprintf(stderr, "Unknown test '%s'.\n", argv[2]);
                return 1;
        }

        if ((ret = sk_disk_open(device, &d)) < 0) {
                fprintf(stderr, "Failed to open disk %s: %s\n", device, strerror(errno));
                return 1;
        }

        if ((ret = sk_disk_smart_self_test(d, test)) < 0) {
                fprintf(stderr, "Failed to start sel-test: %s\n", strerror(errno));
                return 1;

        }

        sk_disk_free(d);

        return 0;
}
