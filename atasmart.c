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

#include <arpa/inet.h>
#include <stdlib.h>
#include <alloca.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <scsi/scsi_ioctl.h>
#include <linux/hdreg.h>
#include <linux/fs.h>
#include <sys/types.h>
#include <regex.h>

#include "atasmart.h"

#ifndef STRPOOL
#define _P(x) x
#endif

#define SK_TIMEOUT 2000

typedef enum SkDirection {
        SK_DIRECTION_NONE,
        SK_DIRECTION_IN,
        SK_DIRECTION_OUT,
        _SK_DIRECTION_MAX
} SkDirection;

typedef enum SkDiskType {
        SK_DISK_TYPE_ATA_PASSTHROUGH, /* ATA passthrough over SCSI transport */
        SK_DISK_TYPE_ATA,
        SK_DISK_TYPE_UNKNOWN,
        _SK_DISK_TYPE_MAX
} SkDiskType;

struct SkDisk {
        char *name;
        int fd;
        SkDiskType type;

        uint64_t size;

        uint8_t identify[512];
        uint8_t smart_data[512];
        uint8_t smart_threshold_data[512];

        SkBool identify_data_valid:1;
        SkBool smart_data_valid:1;
        SkBool smart_threshold_data_valid:1;

        SkIdentifyParsedData identify_parsed_data;
        SkSmartParsedData smart_parsed_data;
};

/* ATA commands */
typedef enum SkAtaCommand {
        SK_ATA_COMMAND_IDENTIFY_DEVICE = 0xEC,
        SK_ATA_COMMAND_IDENTIFY_PACKET_DEVICE = 0xA1,
        SK_ATA_COMMAND_SMART = 0xB0,
        SK_ATA_COMMAND_CHECK_POWER_MODE = 0xE5
} SkAtaCommand;

/* ATA SMART subcommands (ATA8 7.52.1) */
typedef enum SkSmartCommand {
        SK_SMART_COMMAND_READ_DATA = 0xD0,
        SK_SMART_COMMAND_READ_THRESHOLDS = 0xD1,
        SK_SMART_COMMAND_EXECUTE_OFFLINE_IMMEDIATE = 0xD4,
        SK_SMART_COMMAND_ENABLE_OPERATIONS = 0xD8,
        SK_SMART_COMMAND_DISABLE_OPERATIONS = 0xD9,
        SK_SMART_COMMAND_RETURN_STATUS = 0xDA
} SkSmartCommand;

static SkBool disk_smart_is_available(SkDisk *d) {
        return d->identify_data_valid && !!(d->identify[164] & 1);
}

static SkBool disk_smart_is_enabled(SkDisk *d) {
        return d->identify_data_valid && !!(d->identify[170] & 1);
}

static SkBool disk_smart_is_conveyance_test_available(SkDisk *d) {
        assert(d->smart_data_valid);

        return !!(d->smart_data[367] & 32);
}
static SkBool disk_smart_is_short_and_extended_test_available(SkDisk *d) {
        assert(d->smart_data_valid);

        return !!(d->smart_data[367] & 16);
}

static SkBool disk_smart_is_start_test_available(SkDisk *d) {
        assert(d->smart_data_valid);

        return !!(d->smart_data[367] & 1);
}

static SkBool disk_smart_is_abort_test_available(SkDisk *d) {
        assert(d->smart_data_valid);

        return !!(d->smart_data[367] & 41);
}

static int disk_ata_command(SkDisk *d, SkAtaCommand command, SkDirection direction, void* cmd_data, void* data, size_t *len) {
        uint8_t *bytes = cmd_data;
        int ret;

        assert(d->type == SK_DISK_TYPE_ATA);

        switch (direction) {

                case SK_DIRECTION_OUT:

                        /* We could use HDIO_DRIVE_TASKFILE here, but
                         * that's a deprecated ioctl(), hence we don't
                         * do it. And we don't need writing anyway. */

                        errno = ENOTSUP;
                        return -1;

                case SK_DIRECTION_IN: {
                        uint8_t *ioctl_data;

                        /* We have HDIO_DRIVE_CMD which can only read, but not write,
                         * and cannot do LBA. We use it for all read commands. */

                        ioctl_data = alloca(4 + *len);
                        memset(ioctl_data, 0, 4 + *len);

                        ioctl_data[0] = (uint8_t) command;  /* COMMAND */
                        ioctl_data[1] = ioctl_data[0] == WIN_SMART ? bytes[9] : bytes[3];  /* SECTOR/NSECTOR */
                        ioctl_data[2] = bytes[1];          /* FEATURE */
                        ioctl_data[3] = bytes[3];          /* NSECTOR */

                        if ((ret = ioctl(d->fd, HDIO_DRIVE_CMD, ioctl_data)) < 0)
                                return ret;

                        memset(bytes, 0, 12);
                        bytes[11] = ioctl_data[0];
                        bytes[1] = ioctl_data[1];
                        bytes[3] = ioctl_data[2];

                        memcpy(data, ioctl_data+4, *len);

                        return ret;
                }

                case SK_DIRECTION_NONE: {
                        uint8_t ioctl_data[7];

                        /* We have HDIO_DRIVE_TASK which can neither read nor
                         * write, but can do LBA. We use it for all commands that
                         * do neither read nor write */

                        memset(ioctl_data, 0, sizeof(ioctl_data));

                        ioctl_data[0] = (uint8_t) command;  /* COMMAND */
                        ioctl_data[1] = bytes[1];         /* FEATURE */
                        ioctl_data[2] = bytes[3];         /* NSECTOR */

                        ioctl_data[3] = bytes[9];         /* LBA LOW */
                        ioctl_data[4] = bytes[8];         /* LBA MID */
                        ioctl_data[5] = bytes[7];         /* LBA HIGH */
                        ioctl_data[6] = bytes[10];        /* SELECT */

                        if ((ret = ioctl(d->fd, HDIO_DRIVE_TASK, ioctl_data)))
                                return ret;

                        memset(bytes, 0, 12);
                        bytes[11] = ioctl_data[0];
                        bytes[1] = ioctl_data[1];
                        bytes[3] = ioctl_data[2];

                        bytes[9] = ioctl_data[3];
                        bytes[8] = ioctl_data[4];
                        bytes[7] = ioctl_data[5];

                        bytes[10] = ioctl_data[6];

                        return ret;
                }

                default:
                        assert(FALSE);
                        return -1;
        }
}

/* Sends a SCSI command block */
static int sg_io(int fd, int direction,
                 const void *cdb, size_t cdb_len,
                 void *data, size_t data_len,
                 void *sense, size_t sense_len) {

        struct sg_io_hdr io_hdr;

        memset(&io_hdr, 0, sizeof(struct sg_io_hdr));

        io_hdr.interface_id = 'S';
        io_hdr.cmdp = (unsigned char*) cdb;
        io_hdr.cmd_len = cdb_len;
        io_hdr.dxferp = data;
        io_hdr.dxfer_len = data_len;
        io_hdr.sbp = sense;
        io_hdr.mx_sb_len = sense_len;
        io_hdr.dxfer_direction = direction;
        io_hdr.timeout = SK_TIMEOUT;

        return ioctl(fd, SG_IO, &io_hdr);
}

static int disk_passthrough_command(SkDisk *d, SkAtaCommand command, SkDirection direction, void* cmd_data, void* data, size_t *len) {
        uint8_t *bytes = cmd_data;
        uint8_t cdb[16];
        uint8_t sense[32];
        uint8_t *desc = sense+8;
        int ret;

        static const int direction_map[] = {
                [SK_DIRECTION_NONE] = SG_DXFER_NONE,
                [SK_DIRECTION_IN] = SG_DXFER_FROM_DEV,
                [SK_DIRECTION_OUT] = SG_DXFER_TO_DEV
        };

        assert(d->type == SK_DISK_TYPE_ATA_PASSTHROUGH);

        /* ATA Pass-Through 16 byte command, as described in "T10 04-262r8
         * ATA Command Pass-Through":
         * http://www.t10.org/ftp/t10/document.04/04-262r8.pdf */

        memset(cdb, 0, sizeof(cdb));

        cdb[0] = 0x85; /* OPERATION CODE: 16 byte pass through */

        if (direction == SK_DIRECTION_NONE) {
                cdb[1] = 3 << 1;   /* PROTOCOL: Non-Data */
                cdb[2] = 0x20;     /* OFF_LINE=0, CK_COND=1, T_DIR=0, BYT_BLOK=0, T_LENGTH=0 */

        } else if (direction == SK_DIRECTION_IN) {
                cdb[1] = 4 << 1;   /* PROTOCOL: PIO Data-in */
                cdb[2] = 0x2e;     /* OFF_LINE=0, CK_COND=1, T_DIR=1, BYT_BLOK=1, T_LENGTH=2 */

        } else if (direction == SK_DIRECTION_OUT) {
                cdb[1] = 5 << 1;   /* PROTOCOL: PIO Data-Out */
                cdb[2] = 0x26;     /* OFF_LINE=0, CK_COND=1, T_DIR=0, BYT_BLOK=1, T_LENGTH=2 */
        }

        cdb[3] = bytes[0]; /* FEATURES */
        cdb[4] = bytes[1];

        cdb[5] = bytes[2]; /* SECTORS */
        cdb[6] = bytes[3];

        cdb[8] = bytes[9]; /* LBA LOW */
        cdb[10] = bytes[8]; /* LBA MID */
        cdb[12] = bytes[7]; /* LBA HIGH */

        cdb[13] = bytes[10] & 0x4F; /* SELECT */
        cdb[14] = (uint8_t) command;

        memset(sense, 0, sizeof(sense));

        if ((ret = sg_io(d->fd, direction_map[direction], cdb, sizeof(cdb), data, (size_t) cdb[6] * 512, sense, sizeof(sense))) < 0)
                return ret;

        if (sense[0] != 0x72 || desc[0] != 0x9 || desc[1] != 0x0c) {
                errno = EIO;
                return -1;
        }

        memset(bytes, 0, 12);

        bytes[1] = desc[3];
        bytes[2] = desc[4];
        bytes[3] = desc[5];
        bytes[9] = desc[7];
        bytes[8] = desc[9];
        bytes[7] = desc[11];
        bytes[10] = desc[12];
        bytes[11] = desc[13];

        return ret;
}

static int disk_command(SkDisk *d, SkAtaCommand command, SkDirection direction, void* cmd_data, void* data, size_t *len) {

        static int (* const disk_command_table[_SK_DISK_TYPE_MAX]) (SkDisk *d, SkAtaCommand command, SkDirection direction, void* cmd_data, void* data, size_t *len) = {
                [SK_DISK_TYPE_ATA] = disk_ata_command,
                [SK_DISK_TYPE_ATA_PASSTHROUGH] = disk_passthrough_command,
        };

        assert(d);
        assert(d->type <= _SK_DISK_TYPE_MAX);
        assert(direction <= _SK_DIRECTION_MAX);

        assert(direction == SK_DIRECTION_NONE || (data && len && *len > 0));
        assert(direction != SK_DIRECTION_NONE || (!data && !len));

        return disk_command_table[d->type](d, command, direction, cmd_data, data, len);
}

static int disk_identify_device(SkDisk *d) {
        uint16_t cmd[6];
        int ret;
        size_t len = 512;

        memset(cmd, 0, sizeof(cmd));

        cmd[1] = htons(1);

        if ((ret = disk_command(d, SK_ATA_COMMAND_IDENTIFY_DEVICE, SK_DIRECTION_IN, cmd, d->identify, &len)) < 0)
                return ret;

        if (len != 512) {
                errno = EIO;
                return -1;
        }

        d->identify_data_valid = TRUE;

        return 0;
}

int sk_disk_check_sleep_mode(SkDisk *d, SkBool *awake) {
        int ret;
        uint16_t cmd[6];

        if (!d->identify_data_valid) {
                errno = ENOTSUP;
                return -1;
        }

        memset(cmd, 0, sizeof(cmd));

        if ((ret = disk_command(d, SK_ATA_COMMAND_CHECK_POWER_MODE, SK_DIRECTION_NONE, cmd, NULL, 0)) < 0)
                return ret;

        if (cmd[0] != 0 || (ntohs(cmd[5]) & 1) != 0) {
                errno = EIO;
                return -1;
        }

        *awake = ntohs(cmd[1]) == 0xFF;

        return 0;
}

static int disk_smart_enable(SkDisk *d, SkBool b) {
        uint16_t cmd[6];

        if (!disk_smart_is_available(d)) {
                errno = ENOTSUP;
                return -1;
        }

        memset(cmd, 0, sizeof(cmd));

        cmd[0] = htons(b ? SK_SMART_COMMAND_ENABLE_OPERATIONS : SK_SMART_COMMAND_DISABLE_OPERATIONS);
        cmd[2] = htons(0x0000U);
        cmd[3] = htons(0x00C2U);
        cmd[4] = htons(0x4F00U);

        return disk_command(d, SK_ATA_COMMAND_SMART, SK_DIRECTION_NONE, cmd, NULL, 0);
}

int sk_disk_smart_read_data(SkDisk *d) {
        uint16_t cmd[6];
        int ret;
        size_t len = 512;

        if (!disk_smart_is_available(d)) {
                errno = ENOTSUP;
                return -1;
        }

        memset(cmd, 0, sizeof(cmd));

        cmd[0] = htons(SK_SMART_COMMAND_READ_DATA);
        cmd[1] = htons(1);
        cmd[2] = htons(0x0000U);
        cmd[3] = htons(0x00C2U);
        cmd[4] = htons(0x4F00U);

        if ((ret = disk_command(d, SK_ATA_COMMAND_SMART, SK_DIRECTION_IN, cmd, d->smart_data, &len)) < 0)
                return ret;

        d->smart_data_valid = TRUE;

        return ret;
}

static int disk_smart_read_thresholds(SkDisk *d) {
        uint16_t cmd[6];
        int ret;
        size_t len = 512;

        if (!disk_smart_is_available(d)) {
                errno = ENOTSUP;
                return -1;
        }

        memset(cmd, 0, sizeof(cmd));

        cmd[0] = htons(SK_SMART_COMMAND_READ_THRESHOLDS);
        cmd[1] = htons(1);
        cmd[2] = htons(0x0000U);
        cmd[3] = htons(0x00C2U);
        cmd[4] = htons(0x4F00U);

        if ((ret = disk_command(d, SK_ATA_COMMAND_SMART, SK_DIRECTION_IN, cmd, d->smart_threshold_data, &len)) < 0)
                return ret;

        d->smart_threshold_data_valid = TRUE;

        return ret;
}

int sk_disk_smart_status(SkDisk *d, SkBool *good) {
        uint16_t cmd[6];
        int ret;

        if (!disk_smart_is_available(d)) {
                errno = ENOTSUP;
                return -1;
        }

        memset(cmd, 0, sizeof(cmd));

        cmd[0] = htons(SK_SMART_COMMAND_RETURN_STATUS);
        cmd[1] = htons(0x0000U);
        cmd[3] = htons(0x00C2U);
        cmd[4] = htons(0x4F00U);

        if ((ret = disk_command(d, SK_ATA_COMMAND_SMART, SK_DIRECTION_NONE, cmd, NULL, 0)) < 0)
                return ret;

        if (cmd[3] == htons(0x00C2U) &&
            cmd[4] == htons(0x4F00U))
                *good = TRUE;
        else if (cmd[3] == htons(0x002CU) &&
            cmd[4] == htons(0xF400U))
                *good = FALSE;
        else {
                errno = EIO;
                return -1;
        }

        return ret;
}

int sk_disk_smart_self_test(SkDisk *d, SkSmartSelfTest test) {
        uint16_t cmd[6];
        int ret;

        if (!disk_smart_is_available(d)) {
                errno = ENOTSUP;
                return -1;
        }

        if (!d->smart_data_valid)
                if ((ret = sk_disk_smart_read_data(d)) < 0)
                        return -1;

        assert(d->smart_data_valid);

        if (test != SK_SMART_SELF_TEST_SHORT &&
            test != SK_SMART_SELF_TEST_EXTENDED &&
            test != SK_SMART_SELF_TEST_CONVEYANCE &&
            test != SK_SMART_SELF_TEST_ABORT) {
                errno = EINVAL;
                return -1;
        }

        if (!disk_smart_is_start_test_available(d)
            || (test == SK_SMART_SELF_TEST_ABORT && !disk_smart_is_abort_test_available(d))
            || ((test == SK_SMART_SELF_TEST_SHORT || test == SK_SMART_SELF_TEST_EXTENDED) && !disk_smart_is_short_and_extended_test_available(d))
            || (test == SK_SMART_SELF_TEST_CONVEYANCE && !disk_smart_is_conveyance_test_available(d))) {
                errno = ENOTSUP;
                return -1;
        }

        if (test == SK_SMART_SELF_TEST_ABORT &&
            !disk_smart_is_abort_test_available(d)) {
                errno = ENOTSUP;
                return -1;
        }

        memset(cmd, 0, sizeof(cmd));

        cmd[0] = htons(SK_SMART_COMMAND_EXECUTE_OFFLINE_IMMEDIATE);
        cmd[2] = htons(0x0000U);
        cmd[3] = htons(0x00C2U);
        cmd[4] = htons(0x4F00U | (uint16_t) test);

        return disk_command(d, SK_ATA_COMMAND_SMART, SK_DIRECTION_NONE, cmd, NULL, NULL);
}

static void swap_strings(char *s, size_t len) {
        assert((len & 1) == 0);

        for (; len > 0; s += 2, len -= 2) {
                char t;
                t = s[0];
                s[0] = s[1];
                s[1] = t;
        }
}

static void clean_strings(char *s) {
        char *e;

        for (e = s; *e; e++)
                if (*e < ' ' || *e >= 127)
                        *e = ' ';
}

static void drop_spaces(char *s) {
        char *d = s;
        SkBool prev_space = FALSE;

        s += strspn(s, " ");

        for (;*s; s++) {

                if (prev_space) {
                        if (*s != ' ') {
                                prev_space = FALSE;
                                *(d++) = ' ';
                        }
                } else {
                        if (*s == ' ')
                                prev_space = TRUE;
                        else
                                *(d++) = *s;
                }
        }

        *d = 0;
}

static void read_string(char *d, uint8_t *s, size_t len) {
        memcpy(d, s, len);
        d[len] = 0;
        swap_strings(d, len);
        clean_strings(d);
        drop_spaces(d);
}

int sk_disk_identify_parse(SkDisk *d, const SkIdentifyParsedData **ipd) {

        if (!d->identify_data_valid) {
                errno = ENOENT;
                return -1;
        }

        read_string(d->identify_parsed_data.serial, d->identify+20, 20);
        read_string(d->identify_parsed_data.firmware, d->identify+46, 8);
        read_string(d->identify_parsed_data.model, d->identify+54, 40);

        *ipd = &d->identify_parsed_data;

        return 0;
}

int sk_disk_smart_is_available(SkDisk *d, SkBool *b) {

        if (!d->identify_data_valid) {
                errno = ENOTSUP;
                return -1;
        }

        *b = disk_smart_is_available(d);
        return 0;
}

int sk_disk_identify_is_available(SkDisk *d, SkBool *b) {

        *b = d->identify_data_valid;
        return 0;
}

const char *sk_smart_offline_data_collection_status_to_string(SkSmartOfflineDataCollectionStatus status) {

        /* %STRINGPOOLSTART% */
        static const char* const map[] = {
                [SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_NEVER] = "Off-line data collection activity was never started.",
                [SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_SUCCESS] = "Off-line data collection activity was completed without error.",
                [SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_INPROGRESS] = "Off-line activity in progress.",
                [SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_SUSPENDED] = "Off-line data collection activity was suspended by an interrupting command from host.",
                [SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_ABORTED] = "Off-line data collection activity was aborted by an interrupting command from host.",
                [SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_FATAL] = "Off-line data collection activity was aborted by the device with a fatal error.",
                [SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_UNKNOWN] = "Unknown status"
        };
        /* %STRINGPOOLSTOP% */

        if (status >= _SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_MAX)
                return NULL;

        return _P(map[status]);
}

const char *sk_smart_self_test_execution_status_to_string(SkSmartSelfTestExecutionStatus status) {

        /* %STRINGPOOLSTART% */
        static const char* const map[] = {
                [SK_SMART_SELF_TEST_EXECUTION_STATUS_SUCCESS_OR_NEVER] = "The previous self-test routine completed without error or no self-test has ever been run.",
                [SK_SMART_SELF_TEST_EXECUTION_STATUS_ABORTED] = "The self-test routine was aborted by the host.",
                [SK_SMART_SELF_TEST_EXECUTION_STATUS_INTERRUPTED] = "The self-test routine was interrupted by the host with a hardware or software reset.",
                [SK_SMART_SELF_TEST_EXECUTION_STATUS_FATAL] = "A fatal error or unknown test error occurred while the device was executing its self-test routine and the device was unable to complete the self-test routine.",
                [SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_UNKNOWN] = "The previous self-test completed having a test element that failed and the test element that failed.",
                [SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_ELECTRICAL] = "The previous self-test completed having the electrical element of the test failed.",
                [SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_SERVO] = "The previous self-test completed having the servo (and/or seek) test element of the test failed.",
                [SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_READ] = "The previous self-test completed having the read element of the test failed.",
                [SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_HANDLING] = "The previous self-test completed having a test element that failed and the device is suspected of having handling damage.",
                [SK_SMART_SELF_TEST_EXECUTION_STATUS_INPROGRESS] = "Self-test routine in progress"
        };
        /* %STRINGPOOLSTOP% */

        if (status >= _SK_SMART_SELF_TEST_EXECUTION_STATUS_MAX)
                return NULL;

        return _P(map[status]);
}

const char* sk_smart_self_test_to_string(SkSmartSelfTest test) {

        switch (test) {
                case SK_SMART_SELF_TEST_SHORT:
                        return "short";
                case SK_SMART_SELF_TEST_EXTENDED:
                        return "extended";
                case SK_SMART_SELF_TEST_CONVEYANCE:
                        return "conveyance";
                case SK_SMART_SELF_TEST_ABORT:
                        return "abort";
        }

        return NULL;
}

SkBool sk_smart_self_test_available(const SkSmartParsedData *d, SkSmartSelfTest test) {

        if (!d->start_test_available)
                return FALSE;

        switch (test) {
                case SK_SMART_SELF_TEST_SHORT:
                case SK_SMART_SELF_TEST_EXTENDED:
                        return d->short_and_extended_test_available;
                case SK_SMART_SELF_TEST_CONVEYANCE:
                        return d->conveyance_test_available;
                case SK_SMART_SELF_TEST_ABORT:
                        return d->abort_test_available;
                default:
                        return FALSE;
        }
}

unsigned sk_smart_self_test_polling_minutes(const SkSmartParsedData *d, SkSmartSelfTest test) {

        if (!sk_smart_self_test_available(d, test))
                return 0;

        switch (test) {
                case SK_SMART_SELF_TEST_SHORT:
                        return d->short_test_polling_minutes;
                case SK_SMART_SELF_TEST_EXTENDED:
                        return d->extended_test_polling_minutes;
                case SK_SMART_SELF_TEST_CONVEYANCE:
                        return d->conveyance_test_polling_minutes;
                default:
                        return 0;
        }
}

static void make_pretty(SkSmartAttributeParsedData *a) {
        uint64_t fourtyeight;

        if (!a->name)
                return;

        if (a->pretty_unit == SK_SMART_ATTRIBUTE_UNIT_UNKNOWN)
                return;

        fourtyeight =
                ((uint64_t) a->raw[0]) |
                (((uint64_t) a->raw[1]) << 8) |
                (((uint64_t) a->raw[2]) << 16) |
                (((uint64_t) a->raw[3]) << 24) |
                (((uint64_t) a->raw[4]) << 32) |
                (((uint64_t) a->raw[5]) << 40);

        if (!strcmp(a->name, "spin-up-time"))
                a->pretty_value = fourtyeight & 0xFFFF;
        else if (!strcmp(a->name, "airflow-temperature-celsius") ||
                 !strcmp(a->name, "temperature-celsius-1") ||
                 !strcmp(a->name, "temperature-celsius-2"))
                a->pretty_value = (fourtyeight & 0xFFFF)*1000 + 273150;
        else if (!strcmp(a->name, "temperature-centi-celsius"))
                a->pretty_value = (fourtyeight & 0xFFFF)*100 + 273150;
        else if (!strcmp(a->name, "power-on-minutes"))
                a->pretty_value = fourtyeight * 60 * 1000;
        else if (!strcmp(a->name, "power-on-seconds"))
                a->pretty_value = fourtyeight * 1000;
        else if (!strcmp(a->name, "power-on-half-minutes"))
                a->pretty_value = fourtyeight * 30 * 1000;
        else if (!strcmp(a->name, "power-on-hours") ||
                 !strcmp(a->name, "loaded-hours") ||
                 !strcmp(a->name, "head-flying-hours"))
                a->pretty_value = fourtyeight * 60 * 60 * 1000;
        else
                a->pretty_value = fourtyeight;
}

typedef struct SkSmartAttributeInfo {
        const char *name;
        SkSmartAttributeUnit unit;
} SkSmartAttributeInfo;

/* This data is stolen from smartmontools */

/* %STRINGPOOLSTART% */
static const SkSmartAttributeInfo const attribute_info[255] = {
        [1]   = { "raw-read-error-rate",         SK_SMART_ATTRIBUTE_UNIT_NONE },
        [2]   = { "throughput-perfomance",       SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
        [3]   = { "spin-up-time",                SK_SMART_ATTRIBUTE_UNIT_MSECONDS },
        [4]   = { "start-stop-count",            SK_SMART_ATTRIBUTE_UNIT_NONE },
        [5]   = { "reallocated-sector-count",    SK_SMART_ATTRIBUTE_UNIT_NONE },
        [6]   = { "read-channel-margin",         SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
        [7]   = { "seek-error-rate",             SK_SMART_ATTRIBUTE_UNIT_NONE },
        [8]   = { "seek-time-perfomance",        SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
        [9]   = { "power-on-hours",              SK_SMART_ATTRIBUTE_UNIT_MSECONDS },
        [10]  = { "spin-retry-count",            SK_SMART_ATTRIBUTE_UNIT_NONE },
        [11]  = { "calibration-retry-count",     SK_SMART_ATTRIBUTE_UNIT_NONE },
        [12]  = { "power-cycle-count",           SK_SMART_ATTRIBUTE_UNIT_NONE },
        [13]  = { "read-soft-error-rate",        SK_SMART_ATTRIBUTE_UNIT_NONE },
        [187] = { "reported-uncorrect",          SK_SMART_ATTRIBUTE_UNIT_SECTORS },
        [189] = { "high-fly-writes",             SK_SMART_ATTRIBUTE_UNIT_NONE },
        [190] = { "airflow-temperature-celsius", SK_SMART_ATTRIBUTE_UNIT_MKELVIN },
        [191] = { "g-sense-error-rate",          SK_SMART_ATTRIBUTE_UNIT_NONE },
        [192] = { "power-off-retract-count-1",   SK_SMART_ATTRIBUTE_UNIT_NONE },
        [193] = { "load-cycle-count-1",          SK_SMART_ATTRIBUTE_UNIT_NONE },
        [194] = { "temperature-celsius-2",       SK_SMART_ATTRIBUTE_UNIT_MKELVIN },
        [195] = { "hardware-ecc-recovered",      SK_SMART_ATTRIBUTE_UNIT_NONE },
        [196] = { "reallocated-event-count",     SK_SMART_ATTRIBUTE_UNIT_NONE },
        [197] = { "current-pending-sector",      SK_SMART_ATTRIBUTE_UNIT_SECTORS },
        [198] = { "offline-uncorrectable",       SK_SMART_ATTRIBUTE_UNIT_SECTORS },
        [199] = { "udma-crc-error-count",        SK_SMART_ATTRIBUTE_UNIT_NONE },
        [200] = { "multi-zone-error-rate",       SK_SMART_ATTRIBUTE_UNIT_NONE },
        [201] = { "soft-read-error-rate",        SK_SMART_ATTRIBUTE_UNIT_NONE },
        [202] = { "ta-increase-count",           SK_SMART_ATTRIBUTE_UNIT_NONE },
        [203] = { "run-out-cancel",              SK_SMART_ATTRIBUTE_UNIT_NONE },
        [204] = { "shock-count-write-opern",     SK_SMART_ATTRIBUTE_UNIT_NONE },
        [205] = { "shock-rate-write-opern",      SK_SMART_ATTRIBUTE_UNIT_NONE },
        [206] = { "flying-height",               SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
        [207] = { "spin-high-current",           SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
        [208] = { "spin-buzz",                   SK_SMART_ATTRIBUTE_UNIT_UNKNOWN},
        [209] = { "offline-seek-perfomance",     SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
        [220] = { "disk-shift",                  SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
        [221] = { "g-sense-error-rate-2",        SK_SMART_ATTRIBUTE_UNIT_NONE },
        [222] = { "loaded-hours",                SK_SMART_ATTRIBUTE_UNIT_MSECONDS },
        [223] = { "load-retry-count",            SK_SMART_ATTRIBUTE_UNIT_NONE },
        [224] = { "load-friction",               SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
        [225] = { "load-cycle-count-2",          SK_SMART_ATTRIBUTE_UNIT_NONE },
        [226] = { "load-in-time",                SK_SMART_ATTRIBUTE_UNIT_MSECONDS },
        [227] = { "torq-amp-count",              SK_SMART_ATTRIBUTE_UNIT_NONE },
        [228] = { "power-off-retract-count-2",   SK_SMART_ATTRIBUTE_UNIT_NONE },
        [230] = { "head-amplitude",              SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
        [231] = { "temperature-celsius-1",       SK_SMART_ATTRIBUTE_UNIT_MKELVIN },
        [240] = { "head-flying-hours",           SK_SMART_ATTRIBUTE_UNIT_MSECONDS },
        [250] = { "read-error-retry-rate",       SK_SMART_ATTRIBUTE_UNIT_NONE }
};
/* %STRINGPOOLSTOP% */

typedef enum SkSmartQuirk {
        SK_SMART_QUIRK_9_POWERONMINUTES = 1,
        SK_SMART_QUIRK_9_POWERONSECONDS = 2,
        SK_SMART_QUIRK_9_POWERONHALFMINUTES = 4,
        SK_SMART_QUIRK_192_EMERGENCYRETRACTCYCLECT = 8,
        SK_SMART_QUIRK_193_LOADUNLOAD = 16,
        SK_SMART_QUIRK_194_10XCELSIUS = 32,
        SK_SMART_QUIRK_194_UNKNOWN = 64,
        SK_SMART_QUIRK_200_WRITEERRORCOUNT = 128,
        SK_SMART_QUIRK_201_DETECTEDTACOUNT = 256,
} SkSmartQuirk;

/* %STRINGPOOLSTART% */
static const char *quirk_name[] = {
        "9_POWERONMINUTES",
        "9_POWERONSECONDS",
        "9_POWERONHALFMINUTES",
        "192_EMERGENCYRETRACTCYCLECT",
        "193_LOADUNLOAD",
        "194_10XCELSIUS",
        "194_UNKNOWN",
        "200_WRITEERRORCOUNT",
        "201_DETECTEDTACOUNT",
        NULL
};
/* %STRINGPOOLSTOP% */

typedef struct SkSmartQuirkDatabase {
        const char *model;
        const char *firmware;
        SkSmartQuirk quirk;
} SkSmartQuirkDatabase;

/* %STRINGPOOLSTART% */
static const SkSmartQuirkDatabase quirk_database[] = { {
                "^FUJITSU MHR2040AT$",
                NULL,
                SK_SMART_QUIRK_9_POWERONSECONDS|
                SK_SMART_QUIRK_192_EMERGENCYRETRACTCYCLECT|
                SK_SMART_QUIRK_200_WRITEERRORCOUNT
        }, {
                "^FUJITSU MHS20[6432]0AT(  .)?$",
                NULL,
                SK_SMART_QUIRK_9_POWERONSECONDS|
                SK_SMART_QUIRK_192_EMERGENCYRETRACTCYCLECT|
                SK_SMART_QUIRK_200_WRITEERRORCOUNT|
                SK_SMART_QUIRK_201_DETECTEDTACOUNT

        }, {
                "^SAMSUNG SV4012H$",
                NULL,
                SK_SMART_QUIRK_9_POWERONHALFMINUTES
        }, {
                "^SAMSUNG SV0412H$",
                NULL,
                SK_SMART_QUIRK_9_POWERONHALFMINUTES|
                SK_SMART_QUIRK_194_10XCELSIUS
        }, {
                "^SAMSUNG SV1204H$",
                NULL,
                SK_SMART_QUIRK_9_POWERONHALFMINUTES|
                SK_SMART_QUIRK_194_10XCELSIUS
        }, {
                "^SAMSUNG SP40A2H$",
                "^RR100-07$",
                SK_SMART_QUIRK_9_POWERONHALFMINUTES
        }, {
                "^SAMSUNG SP8004H$",
                "^QW100-61$",
                SK_SMART_QUIRK_9_POWERONHALFMINUTES
        }, {
                "^SAMSUNG",
                ".*-(2[3-9]|3[0-9])$",
                SK_SMART_QUIRK_9_POWERONHALFMINUTES

        }, {
                "^Maxtor 2B0(0[468]|1[05]|20)H1$",
                NULL,
                SK_SMART_QUIRK_9_POWERONMINUTES|
                SK_SMART_QUIRK_194_UNKNOWN
        }, {
                "^Maxtor 4G(120J6|160J[68])$",
                NULL,
                SK_SMART_QUIRK_9_POWERONMINUTES|
                SK_SMART_QUIRK_194_UNKNOWN
        }, {
                "^Maxtor 4D0(20H1|40H2|60H3|80H4)$",
                NULL,
                SK_SMART_QUIRK_9_POWERONMINUTES|
                SK_SMART_QUIRK_194_UNKNOWN

        }, {
                "^HITACHI_DK14FA-20B$",
                NULL,
                SK_SMART_QUIRK_9_POWERONMINUTES|
                SK_SMART_QUIRK_193_LOADUNLOAD
        }, {
                "^HITACHI_DK23..-..B?$",
                NULL,
                SK_SMART_QUIRK_9_POWERONMINUTES|
                SK_SMART_QUIRK_193_LOADUNLOAD
        }, {
                "^(HITACHI_DK23FA-20J|HTA422020F9AT[JN]0)$",
                NULL,
                SK_SMART_QUIRK_9_POWERONMINUTES|
                SK_SMART_QUIRK_193_LOADUNLOAD

        }, {
                "Maxtor",
                NULL,
                SK_SMART_QUIRK_9_POWERONMINUTES
        }, {
                "MAXTOR",
                NULL,
                SK_SMART_QUIRK_9_POWERONMINUTES
        }, {
                "Fujitsu",
                NULL,
                SK_SMART_QUIRK_9_POWERONSECONDS
        }, {
                "FUJITSU",
                NULL,
                SK_SMART_QUIRK_9_POWERONSECONDS
        }, {
                NULL,
                NULL,
                0
        }
};
/* %STRINGPOOLSTOP% */

static int match(const char*regex, const char *s, SkBool *result) {
        int k;
        regex_t re;

        *result = FALSE;

        if (regcomp(&re, regex, REG_EXTENDED|REG_NOSUB) != 0) {
                errno = EINVAL;
                return -1;
        }

        if ((k = regexec(&re, s, 0, NULL, 0)) != 0) {

                if (k != REG_NOMATCH) {
                        regfree(&re);
                        errno = EINVAL;
                        return -1;
                }

        } else
                *result = TRUE;

        regfree(&re);

        return 0;
}

static int lookup_quirks(const char *model, const char *firmware, SkSmartQuirk *quirk) {
        int k;
        const SkSmartQuirkDatabase *db;

        *quirk = 0;

        for (db = quirk_database; db->model || db->firmware; db++) {

                if (db->model) {
                        SkBool matching = FALSE;

                        if ((k = match(_P(db->model), model, &matching)) < 0)
                                return k;

                        if (!matching)
                                continue;
                }

                if (db->firmware) {
                        SkBool matching = FALSE;

                        if ((k = match(_P(db->firmware), firmware, &matching)) < 0)
                                return k;

                        if (!matching)
                                continue;
                }

                *quirk = db->quirk;
                return 0;
        }

        return 0;
}

static const SkSmartAttributeInfo *lookup_attribute(SkDisk *d, uint8_t id) {
        const SkIdentifyParsedData *ipd;
        SkSmartQuirk quirk = 0;

        /* These are the complex ones */
        if (sk_disk_identify_parse(d, &ipd) < 0)
                return NULL;

        if (lookup_quirks(ipd->model, ipd->firmware, &quirk) < 0)
                return NULL;

        if (quirk) {
                switch (id) {

                        case 9:
                                /* %STRINGPOOLSTART% */
                                if (quirk & SK_SMART_QUIRK_9_POWERONMINUTES) {
                                        static const SkSmartAttributeInfo a = {
                                                "power-on-minutes", SK_SMART_ATTRIBUTE_UNIT_MSECONDS
                                        };
                                        return &a;

                                } else if (quirk & SK_SMART_QUIRK_9_POWERONSECONDS) {
                                        static const SkSmartAttributeInfo a = {
                                                "power-on-seconds", SK_SMART_ATTRIBUTE_UNIT_MSECONDS
                                        };
                                        return &a;

                                } else if (quirk & SK_SMART_QUIRK_9_POWERONHALFMINUTES) {
                                        static const SkSmartAttributeInfo a = {
                                                "power-on-half-minutes", SK_SMART_ATTRIBUTE_UNIT_MSECONDS
                                        };
                                        return &a;
                                }
                                /* %STRINGPOOLSTOP% */

                                break;

                        case 192:
                                /* %STRINGPOOLSTART% */
                                if (quirk & SK_SMART_QUIRK_192_EMERGENCYRETRACTCYCLECT) {
                                        static const SkSmartAttributeInfo a = {
                                                "emergency-retract-cycle-count", SK_SMART_ATTRIBUTE_UNIT_NONE
                                        };
                                        return &a;
                                }
                                /* %STRINGPOOLSTOP% */

                                break;

                        case 194:
                                /* %STRINGPOOLSTART% */
                                if (quirk & SK_SMART_QUIRK_194_10XCELSIUS) {
                                        static const SkSmartAttributeInfo a = {
                                                "temperature-centi-celsius", SK_SMART_ATTRIBUTE_UNIT_MKELVIN
                                        };
                                        return &a;
                                } else if (quirk & SK_SMART_QUIRK_194_UNKNOWN)
                                        return NULL;
                                /* %STRINGPOOLSTOP% */

                                break;

                        case 200:
                                /* %STRINGPOOLSTART% */
                                if (quirk & SK_SMART_QUIRK_200_WRITEERRORCOUNT) {
                                        static const SkSmartAttributeInfo a = {
                                                "write-error-count", SK_SMART_ATTRIBUTE_UNIT_NONE
                                        };
                                        return &a;
                                }
                                /* %STRINGPOOLSTOP% */

                                break;

                        case 201:
                                /* %STRINGPOOLSTART% */
                                if (quirk & SK_SMART_QUIRK_201_DETECTEDTACOUNT) {
                                        static const SkSmartAttributeInfo a = {
                                                "detected-ta-count", SK_SMART_ATTRIBUTE_UNIT_NONE
                                        };
                                        return &a;
                                }
                                /* %STRINGPOOLSTOP% */

                                break;
                }
        }

        /* These are the simple cases */
        if (attribute_info[id].name)
                return &attribute_info[id];

        return NULL;
}

int sk_disk_smart_parse(SkDisk *d, const SkSmartParsedData **spd) {

        if (!d->smart_data_valid) {
                errno = ENOENT;
                return -1;
        }

        switch (d->smart_data[362]) {
                case 0x00:
                case 0x80:
                        d->smart_parsed_data.offline_data_collection_status = SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_NEVER;
                        break;

                case 0x02:
                case 0x82:
                        d->smart_parsed_data.offline_data_collection_status = SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_SUCCESS;
                        break;

                case 0x03:
                        d->smart_parsed_data.offline_data_collection_status = SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_INPROGRESS;
                        break;

                case 0x04:
                case 0x84:
                        d->smart_parsed_data.offline_data_collection_status = SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_SUSPENDED;
                        break;

                case 0x05:
                case 0x85:
                        d->smart_parsed_data.offline_data_collection_status = SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_ABORTED;
                        break;

                case 0x06:
                case 0x86:
                        d->smart_parsed_data.offline_data_collection_status = SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_FATAL;
                        break;

                default:
                        d->smart_parsed_data.offline_data_collection_status = SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_UNKNOWN;
                        break;
        }

        d->smart_parsed_data.self_test_execution_percent_remaining = 10*(d->smart_data[363] & 0xF);
        d->smart_parsed_data.self_test_execution_status = (d->smart_data[363] >> 4) & 0xF;

        d->smart_parsed_data.total_offline_data_collection_seconds = (uint16_t) d->smart_data[364] | ((uint16_t) d->smart_data[365] << 8);

        d->smart_parsed_data.conveyance_test_available = disk_smart_is_conveyance_test_available(d);
        d->smart_parsed_data.short_and_extended_test_available = disk_smart_is_short_and_extended_test_available(d);
        d->smart_parsed_data.start_test_available = disk_smart_is_start_test_available(d);
        d->smart_parsed_data.abort_test_available = disk_smart_is_abort_test_available(d);

        d->smart_parsed_data.short_test_polling_minutes = d->smart_data[372];
        d->smart_parsed_data.extended_test_polling_minutes = d->smart_data[373] != 0xFF ? d->smart_data[373] : ((uint16_t) d->smart_data[376] << 8 | (uint16_t) d->smart_data[375]);
        d->smart_parsed_data.conveyance_test_polling_minutes = d->smart_data[374];

        *spd = &d->smart_parsed_data;

        return 0;
}

static void find_threshold(SkDisk *d, SkSmartAttributeParsedData *a) {
        uint8_t *p;
        unsigned n;

        if (!d->smart_threshold_data_valid) {
                a->threshold_valid = FALSE;
                return;
        }

        for (n = 0, p = d->smart_threshold_data+2; n < 30; n++, p+=12)
                if (p[0] == a->id)
                        break;

        if (n >= 30) {
                a->threshold_valid = FALSE;
                a->good_valid = FALSE;
                return;
        }

        a->threshold = p[1];
        a->threshold_valid = p[1] != 0xFE;

        a->good_valid = FALSE;
        a->good = TRUE;

        /* Always-Fail and Always-Pssing thresholds are not relevant
         * for our assessment. */
        if (p[1] >= 1 && p[1] <= 0xFD) {

                if (a->worst_value_valid) {
                        a->good = a->good && (a->worst_value > a->threshold);
                        a->good_valid = TRUE;
                }

                if (a->current_value_valid) {
                        a->good = a->good && (a->current_value > a->threshold);
                        a->good_valid = TRUE;
                }
        }
}

int sk_disk_smart_parse_attributes(SkDisk *d, SkSmartAttributeParseCallback cb, void* userdata) {
        uint8_t *p;
        unsigned n;

        if (!d->smart_data_valid) {
                errno = ENOENT;
                return -1;
        }

        for (n = 0, p = d->smart_data + 2; n < 30; n++, p+=12) {
                SkSmartAttributeParsedData a;
                const SkSmartAttributeInfo *i;
                char *an = NULL;

                if (p[0] == 0)
                        continue;

                memset(&a, 0, sizeof(a));
                a.id = p[0];
                a.current_value = p[3];
                a.current_value_valid = p[3] >= 1 && p[3] <= 0xFD;
                a.worst_value = p[4];
                a.worst_value_valid = p[4] >= 1 && p[4] <= 0xFD;

                a.flags = ((uint16_t) p[2] << 8) | p[1];
                a.prefailure = !!(p[1] & 1);
                a.online = !!(p[1] & 2);

                memcpy(a.raw, p+5, 6);

                if ((i = lookup_attribute(d, p[0]))) {
                        a.name = _P(i->name);
                        a.pretty_unit = i->unit;
                } else {
                        if (asprintf(&an, "attribute-%u", a.id) < 0) {
                                errno = ENOMEM;
                                return -1;
                        }

                        a.name = an;
                        a.pretty_unit = SK_SMART_ATTRIBUTE_UNIT_UNKNOWN;
                }

                make_pretty(&a);

                find_threshold(d, &a);

                cb(d, &a, userdata);

                free(an);
        }

        return 0;
}

static const char *yes_no(SkBool b) {
        return  b ? "yes" : "no";
}

const char* sk_smart_attribute_unit_to_string(SkSmartAttributeUnit unit) {

        /* %STRINGPOOLSTART% */
        const char * const map[] = {
                [SK_SMART_ATTRIBUTE_UNIT_UNKNOWN] = NULL,
                [SK_SMART_ATTRIBUTE_UNIT_NONE] = "",
                [SK_SMART_ATTRIBUTE_UNIT_MSECONDS] = "ms",
                [SK_SMART_ATTRIBUTE_UNIT_SECTORS] = "sectors",
                [SK_SMART_ATTRIBUTE_UNIT_MKELVIN] = "mK"
        };
        /* %STRINGPOOLSTOP% */

        if (unit >= _SK_SMART_ATTRIBUTE_UNIT_MAX)
                return NULL;

        return _P(map[unit]);
}

static char* print_name(char *s, size_t len, uint8_t id, const char *k) {

        if (k)
                strncpy(s, k, len);
        else
                snprintf(s, len, "%u", id);

        s[len-1] = 0;

        return s;
}

static char *print_value(char *s, size_t len, const SkSmartAttributeParsedData *a) {

        switch (a->pretty_unit) {
                case SK_SMART_ATTRIBUTE_UNIT_MSECONDS:

                        if (a->pretty_value >= 1000LLU*60LLU*60LLU*24LLU*365LLU)
                                snprintf(s, len, "%0.1f years", ((double) a->pretty_value)/(1000.0*60*60*24*365));
                        else if (a->pretty_value >= 1000LLU*60LLU*60LLU*24LLU*30LLU)
                                snprintf(s, len, "%0.1f months", ((double) a->pretty_value)/(1000.0*60*60*24*30));
                        else if (a->pretty_value >= 1000LLU*60LLU*60LLU*24LLU)
                                snprintf(s, len, "%0.1f days", ((double) a->pretty_value)/(1000.0*60*60*24));
                        else if (a->pretty_value >= 1000LLU*60LLU*60LLU)
                                snprintf(s, len, "%0.1f h", ((double) a->pretty_value)/(1000.0*60*60));
                        else if (a->pretty_value >= 1000LLU*60LLU)
                                snprintf(s, len, "%0.1f min", ((double) a->pretty_value)/(1000.0*60));
                        else if (a->pretty_value >= 1000LLU)
                                snprintf(s, len, "%0.1f s", ((double) a->pretty_value)/(1000.0));
                        else
                                snprintf(s, len, "%llu ms", (unsigned long long) a->pretty_value);

                        break;

                case SK_SMART_ATTRIBUTE_UNIT_MKELVIN:
                        snprintf(s, len, "%0.1f C", ((double) a->pretty_value - 273150) / 1000);
                        break;

                case SK_SMART_ATTRIBUTE_UNIT_SECTORS:
                        snprintf(s, len, "%llu sectors", (unsigned long long) a->pretty_value);
                        break;

                case SK_SMART_ATTRIBUTE_UNIT_NONE:
                        snprintf(s, len, "%llu", (unsigned long long) a->pretty_value);
                        break;

                case SK_SMART_ATTRIBUTE_UNIT_UNKNOWN:
                        snprintf(s, len, "n/a");
                        break;

                case _SK_SMART_ATTRIBUTE_UNIT_MAX:
                        assert(FALSE);
        }

        s[len-1] = 0;

        return s;
}

#define HIGHLIGHT "\x1B[1m"
#define ENDHIGHLIGHT "\x1B[0m"

static void disk_dump_attributes(SkDisk *d, const SkSmartAttributeParsedData *a, void* userdata) {
        char name[32];
        char pretty[32];
        char tt[32], tw[32], tc[32];
        SkBool highlight;

        snprintf(tt, sizeof(tt), "%3u", a->threshold);
        tt[sizeof(tt)-1] = 0;
        snprintf(tw, sizeof(tw), "%3u", a->worst_value);
        tw[sizeof(tw)-1] = 0;
        snprintf(tc, sizeof(tc), "%3u", a->current_value);
        tc[sizeof(tc)-1] = 0;

        highlight = a->good_valid && !a->good && isatty(1);

        if (highlight)
                fprintf(stderr, HIGHLIGHT);

        printf("%3u %-27s %-3s   %-3s   %-3s   %-11s 0x%02x%02x%02x%02x%02x%02x %-7s %-7s %-3s\n",
               a->id,
               print_name(name, sizeof(name), a->id, a->name),
               a->current_value_valid ? tc : "n/a",
               a->worst_value_valid ? tw : "n/a",
               a->threshold_valid ? tt : "n/a",
               print_value(pretty, sizeof(pretty), a),
               a->raw[0], a->raw[1], a->raw[2], a->raw[3], a->raw[4], a->raw[5],
               a->prefailure ? "prefail" : "old-age",
               a->online ? "online" : "offline",
               a->good_valid ? yes_no(a->good) : "n/a");

        if (highlight)
                fprintf(stderr, ENDHIGHLIGHT);
}

int sk_disk_dump(SkDisk *d) {
        int ret;
        SkBool awake = FALSE;

        printf("Device: %s\n"
               "Size: %lu MiB\n",
               d->name,
               (unsigned long) (d->size/1024/1024));

        if (d->identify_data_valid) {
                const SkIdentifyParsedData *ipd;
                SkSmartQuirk quirk = 0;
                unsigned i;

                if ((ret = sk_disk_identify_parse(d, &ipd)) < 0)
                        return ret;

                printf("Model: [%s]\n"
                       "Serial: [%s]\n"
                       "Firmware: [%s]\n"
                       "SMART Available: %s\n",
                       ipd->model,
                       ipd->serial,
                       ipd->firmware,
                       yes_no(disk_smart_is_available(d)));

                if ((ret = lookup_quirks(ipd->model, ipd->firmware, &quirk)))
                        return ret;

                printf("Quirks:");

                for (i = 0; quirk_name[i]; i++)
                        if (quirk & (1<<i))
                                printf(" %s", quirk_name[i]);

                printf("\n");

        }

        ret = sk_disk_check_sleep_mode(d, &awake);
        printf("Awake: %s\n",
               ret >= 0 ? yes_no(awake) : "unknown");

        if (disk_smart_is_available(d)) {
                const SkSmartParsedData *spd;
                SkBool good;

                if ((ret = sk_disk_smart_status(d, &good)) < 0)
                        return ret;

                printf("Disk Health Good: %s\n",
                        yes_no(good));

                if ((ret = sk_disk_smart_read_data(d)) < 0)
                        return ret;

                if ((ret = sk_disk_smart_parse(d, &spd)) < 0)
                        return ret;

                printf("Off-line Data Collection Status: [%s]\n"
                       "Total Time To Complete Off-Line Data Collection: %u s\n"
                       "Self-Test Execution Status: [%s]\n"
                       "Percent Self-Test Remaining: %u%%\n"
                       "Conveyance Self-Test Available: %s\n"
                       "Short/Extended Self-Test Available: %s\n"
                       "Start Self-Test Available: %s\n"
                       "Abort Self-Test Available: %s\n"
                       "Short Self-Test Polling Time: %u min\n"
                       "Extended Self-Test Polling Time: %u min\n"
                       "Conveyance Self-Test Polling Time: %u min\n",
                       sk_smart_offline_data_collection_status_to_string(spd->offline_data_collection_status),
                       spd->total_offline_data_collection_seconds,
                       sk_smart_self_test_execution_status_to_string(spd->self_test_execution_status),
                       spd->self_test_execution_percent_remaining,
                       yes_no(spd->conveyance_test_available),
                       yes_no(spd->short_and_extended_test_available),
                       yes_no(spd->start_test_available),
                       yes_no(spd->abort_test_available),
                        spd->short_test_polling_minutes,
                       spd->extended_test_polling_minutes,
                       spd->conveyance_test_polling_minutes);

                printf("%3s %-27s %5s %5s %5s %-11s %-14s %-7s %-7s %-3s\n",
                       "ID#",
                       "Name",
                       "Value",
                       "Worst",
                       "Thres",
                       "Pretty",
                       "Raw",
                       "Type",
                       "Updates",
                       "Good");

                if ((ret = sk_disk_smart_parse_attributes(d, disk_dump_attributes, NULL)) < 0)
                        return ret;
        }

        return 0;
}

int sk_disk_get_size(SkDisk *d, uint64_t *bytes) {

        *bytes = d->size;
        return 0;
}

int sk_disk_open(const char *name, SkDisk **_d) {
        SkDisk *d;
        int ret = -1;
        struct stat st;

        assert(name);
        assert(_d);

        if (!(d = calloc(1, sizeof(SkDisk)))) {
                errno = ENOMEM;
                goto fail;
        }

        if (!(d->name = strdup(name))) {
                errno = ENOMEM;
                goto fail;
        }

        if ((d->fd = open(name, O_RDWR|O_NOCTTY)) < 0) {
                ret = d->fd;
                goto fail;
        }

        if ((ret = fstat(d->fd, &st)) < 0)
                goto fail;

        if (!S_ISBLK(st.st_mode)) {
                errno = ENODEV;
                ret = -1;
                goto fail;
        }

        /* So, it's a block device. Let's make sure the ioctls work */

        if ((ret = ioctl(d->fd, BLKGETSIZE64, &d->size)) < 0)
                goto fail;

        if (d->size <= 0 || d->size == (uint64_t) -1) {
                errno = EIO;
                ret = -1;
                goto fail;
        }

        /* OK, it's a real block device with a size. Find a way to
         * identify the device. */
        for (d->type = 0; d->type != SK_DISK_TYPE_UNKNOWN; d->type++)
                if (disk_identify_device(d) >= 0)
                        break;

        /* Check if driver can do SMART, and enable if necessary */
        if (disk_smart_is_available(d)) {

                if (!disk_smart_is_enabled(d)) {
                        if ((ret = disk_smart_enable(d, TRUE)) < 0)
                                goto fail;

                        if ((ret = disk_identify_device(d)) < 0)
                                goto fail;

                        if (!disk_smart_is_enabled(d)) {
                                errno = EIO;
                                ret = -1;
                                goto fail;
                        }
                }

                disk_smart_read_thresholds(d);
        }

        *_d = d;

        return 0;

fail:

        if (d)
                sk_disk_free(d);

        return ret;
}

void sk_disk_free(SkDisk *d) {
        assert(d);

        if (d->fd >= 0)
                close(d->fd);

        free(d->name);
        free(d);
}
