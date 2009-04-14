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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "atasmart.h"

enum {
        MODE_DUMP = 10,
        MODE_OVERALL,
        MODE_POWER_ON,
        MODE_POWER_CYCLE,
        MODE_BAD,
        MODE_TEMPERATURE,
        MODE_STATUS,
        MODE_CAN_SMART,
        MODE_SAVE
};

int mode = MODE_DUMP;

enum {
        ARG_SAVE = 256,
        ARG_LOAD
};

int main(int argc, char *argv[]) {
        int ret;
        const char *device = NULL, *argv0, *p, *file = NULL;
        SkDisk *d;
        int q = 1;
        SkBool from_blob = FALSE;

        static const struct option long_options[] = {
                {"overall",     no_argument, &mode, MODE_OVERALL},
                {"power-on",    no_argument, &mode, MODE_POWER_ON},
                {"power-cycle", no_argument, &mode, MODE_POWER_CYCLE},
                {"bad",         no_argument, &mode, MODE_BAD},
                {"temperature", no_argument, &mode, MODE_TEMPERATURE},
                {"can-smart",   no_argument, &mode, MODE_CAN_SMART},
                {"status",      no_argument, &mode, MODE_STATUS},
                {"save",        optional_argument, NULL, ARG_SAVE},
                {"load",        optional_argument, NULL, ARG_LOAD},
                {"help",        no_argument, NULL, 'h' },
                {0, 0, 0, 0}
        };

        argv0 = argv[0];
        if ((p = strrchr(argv0, '/')))
                argv0 = p+1;

        for (;;) {
                int opt;

                if ((opt = getopt_long(argc, argv, "h", long_options, NULL)) < 0)
                        break;

                switch (opt) {
                        case 0:
                                break;

                        case 'h':
                                fprintf(stderr,
                                        "Usage: %s [PARAMETERS] DEVICE\n"
                                        "Reads ATA SMART data from a device and parses it.\n"
                                        "\n"
                                        "\t--overall        \tShow overall status\n"
                                        "\t--status         \tShow SMART status\n"
                                        "\t--can-smart      \tShow whether SMART is supported\n"
                                        "\t--power-on       \tPrint power on time in ms\n"
                                        "\t--power-cycle    \tPrint number of power cycles\n"
                                        "\t--bad            \tPrint bad sector count\n"
                                        "\t--temperature    \tPrint drive temperature in mKelvin\n"
                                        "\t--save[=FILENAME]\tSave raw data to file/STDOUT\n"
                                        "\t--load[=FILENAME]\tRead data from a file/STDIN instead of device\n"
                                        "\t-h | --help      \tShow this help\n", argv0);

                                return 0;

                        case ARG_SAVE:
                                file = optarg;
                                mode = MODE_SAVE;
                                break;

                        case ARG_LOAD:
                                device = optarg ? optarg : "-";
                                from_blob = TRUE;
                                break;

                        case '?':
                                return 1;

                        default:
                                fprintf(stderr, "Invalid arguments.\n");
                                return 1;
                }
        }

        if (!device) {
                if (optind != argc-1) {
                        fprintf(stderr, "No or more than one device specified.\n");
                        return 1;
                }

                device = argv[optind];
        } else {
                if (optind != argc) {
                        fprintf(stderr, "Too many arguments.\n");
                        return 1;
                }
        }

        if (from_blob) {
                uint8_t blob[4096];
                size_t size;
                FILE *f = stdin;

                if ((ret = sk_disk_open(NULL, &d)) < 0) {
                        fprintf(stderr, "Failed to open disk: %s\n", strerror(errno));
                        return 1;
                }

                if (strcmp(device, "-")) {
                        if (!(f = fopen(device, "r"))) {
                                fprintf(stderr, "Failed to open file: %s\n", strerror(errno));
                                goto finish;
                        }
                }

                size = fread(blob, 1, sizeof(blob), f);

                if (f != stdin)
                        fclose(f);

                if (size >= sizeof(blob)) {
                        fprintf(stderr, "File too large for buffer.\n");
                        goto finish;
                }

                if ((ret = sk_disk_set_blob(d, blob, size)) < 0) {
                        fprintf(stderr, "Failed to set blob: %s\n", strerror(errno));
                        goto finish;
                }

        } else {
                if ((ret = sk_disk_open(device, &d)) < 0) {
                        fprintf(stderr, "Failed to open disk %s: %s\n", device, strerror(errno));
                        return 1;
                }
        }

        switch (mode) {
                case MODE_DUMP:
                        if ((ret = sk_disk_dump(d)) < 0) {
                                fprintf(stderr, "Failed to dump disk data: %s\n", strerror(errno));
                                goto finish;
                        }

                        break;

                case MODE_CAN_SMART: {
                        SkBool available;

                        if ((ret = sk_disk_smart_is_available(d, &available)) < 0) {
                                fprintf(stderr, "Failed to query whether SMART is available: %s\n", strerror(errno));
                                goto finish;
                        }

                        printf("%s\n", available ? "YES" : "NO");
                        q = available ? 0 : 1;
                        break;
                }

                case MODE_OVERALL: {
                        SkSmartOverall overall;

                        if ((ret = sk_disk_smart_read_data(d)) < 0) {
                                fprintf(stderr, "Failed to read SMART data: %s\n", strerror(errno));
                                goto finish;
                        }

                        if ((ret = sk_disk_smart_get_overall(d, &overall)) < 0) {
                                fprintf(stderr, "Failed to get overall status: %s\n", strerror(errno));
                                goto finish;
                        }

                        printf("%s\n", sk_smart_overall_to_string(overall));
                        q = overall == SK_SMART_OVERALL_GOOD ? 0 : 1;
                        goto finish;
                }

                case MODE_STATUS: {
                        SkBool good;

                        if ((ret = sk_disk_smart_status(d, &good)) < 0) {
                                fprintf(stderr, "Failed to get SMART status: %s\n", strerror(errno));
                                goto finish;
                        }

                        printf("%s\n", good ? "GOOD" : "BAD");
                        q = good ? 0 : 1;
                        goto finish;
                }

                case MODE_POWER_ON: {
                        uint64_t ms;

                        if ((ret = sk_disk_smart_read_data(d)) < 0) {
                                fprintf(stderr, "Failed to read SMART data: %s\n", strerror(errno));
                                goto finish;
                        }

                        if ((ret = sk_disk_smart_get_power_on(d, &ms)) < 0) {
                                fprintf(stderr, "Failed to get power on time: %s\n", strerror(errno));
                                goto finish;
                        }

                        printf("%llu\n", (unsigned long long) ms);
                        break;
                }

                case MODE_POWER_CYCLE: {
                        uint64_t count;

                        if ((ret = sk_disk_smart_read_data(d)) < 0) {
                                fprintf(stderr, "Failed to read SMART data: %s\n", strerror(errno));
                                goto finish;
                        }

                        if ((ret = sk_disk_smart_get_power_cycle(d, &count)) < 0) {
                                fprintf(stderr, "Failed to get power cycles: %s\n", strerror(errno));
                                goto finish;
                        }

                        printf("%llu\n", (unsigned long long) count);
                        break;
                }

                case MODE_BAD: {
                        uint64_t bad;

                        if ((ret = sk_disk_smart_read_data(d)) < 0) {
                                fprintf(stderr, "Failed to read SMART data: %s\n", strerror(errno));
                                goto finish;
                        }

                        if ((ret = sk_disk_smart_get_bad(d, &bad)) < 0) {
                                fprintf(stderr, "Failed to get bad sectors: %s\n", strerror(errno));
                                goto finish;
                        }

                        printf("%llu\n", (unsigned long long) bad);
                        q = !!bad;
                        goto finish;
                }

                case MODE_TEMPERATURE: {
                        uint64_t mkelvin;

                        if ((ret = sk_disk_smart_read_data(d)) < 0) {
                                fprintf(stderr, "Failed to read SMART data: %s\n", strerror(errno));
                                goto finish;
                        }

                        if ((ret = sk_disk_smart_get_temperature(d, &mkelvin)) < 0) {
                                fprintf(stderr, "Failed to get temperature: %s\n", strerror(errno));
                                goto finish;
                        }

                        printf("%llu\n", (unsigned long long) mkelvin);
                        break;
                }

                case MODE_SAVE: {
                        const void *blob;
                        size_t size;
                        FILE *f = stdout;
                        size_t n;

                        if ((ret = sk_disk_smart_read_data(d)) < 0) {
                                fprintf(stderr, "Failed to read SMART data: %s\n", strerror(errno));
                                goto finish;
                        }

                        if ((ret = sk_disk_get_blob(d, &blob, &size)) < 0) {
                                fprintf(stderr, "Failed to get blob: %s\n", strerror(errno));
                                goto finish;
                        }

                        if (file && strcmp(file, "-")) {
                                if (!(f = fopen(file, "w"))) {
                                        fprintf(stderr, "Failed to open '%s': %s\n", file, strerror(errno));
                                        goto finish;
                                }
                        }

                        n = fwrite(blob, 1, size, f);

                        if (f != stdout)
                                fclose(f);

                        if (n != size) {
                                fprintf(stderr, "Failed to write to disk: %s\n", strerror(errno));
                                goto finish;
                        }

                        break;
                }

        }


        q = 0;

finish:

        if (d)
                sk_disk_free(d);

        return q;
}
