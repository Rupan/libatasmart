#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <scsi/scsi_ioctl.h>
#include <linux/hdreg.h>
#include <linux/fs.h>
#include <glib.h>

#include "smart.h"

#define SK_TIMEOUT 2000

typedef enum SkDirection {
    SK_DIRECTION_NONE,
    SK_DIRECTION_IN,
    SK_DIRECTION_OUT,
    _SK_DIRECTION_MAX
} SkDirection;

typedef enum SkDeviceType {
    SK_DEVICE_TYPE_ATA_PASSTHROUGH, /* ATA passthrough over SCSI transport */
    SK_DEVICE_TYPE_ATA,
    SK_DEVICE_TYPE_UNKNOWN,
    _SK_DEVICE_TYPE_MAX
} SkDeviceType;

struct SkDevice {
    gchar *name;
    int fd;
    SkDeviceType type;

    guint64 size;

    guint8 identify[512];
    guint8 smart_data[512];
    guint8 smart_threshold_data[512];

    gboolean identify_data_valid:1;
    gboolean smart_data_valid:1;
    gboolean smart_threshold_data_valid:1;

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

/* ATA SMART test type (ATA8 7.52.5.2) */
typedef enum SkSmartTest {
    SK_SMART_TEST_OFFLINE_FULL = 0,
    SK_SMART_TEST_OFFLINE_SHORT = 1,
    SK_SMART_TEST_OFFLINE_EXTENDED = 2,
    SK_SMART_TEST_OFFLINE_CONVEYANCE = 3,
    SK_SMART_TEST_OFFLINE_SELECTIVE = 4,

    SK_SMART_TEST_CAPTIVE_SHORT = 129,
    SK_SMART_TEST_CAPTIVE_EXTENDED = 130,
    SK_SMART_TEST_CAPTIVE_CONVEYANCE = 131,
    SK_SMART_TEST_CAPTIVE_SELECTIVE = 132,

    SK_SMART_TEST_CAPTIVE_MASK = 128,
    SK_SMART_TEST_ABORT = 127
} SkSmartTest;

static gboolean disk_smart_is_available(SkDevice *d) {
    return d->identify_data_valid && !!(d->identify[164] & 1);
}

static gboolean disk_smart_is_enabled(SkDevice *d) {
    return d->identify_data_valid && !!(d->identify[170] & 1);
}

static int disk_ata_command(SkDevice *d, SkAtaCommand command, SkDirection direction, gpointer cmd_data, gpointer data, size_t *len) {
    guint8 *bytes = cmd_data;
    int ret;

    g_assert(d->type == SK_DEVICE_TYPE_ATA);

    switch (direction) {

        case SK_DIRECTION_OUT:

            /* We could use HDIO_DRIVE_TASKFILE here, but that's a
             * deprecated ioctl(), hence we don't do it. */

            errno = ENOTSUP;
            return -1;

        case SK_DIRECTION_IN: {
            guint8 *ioctl_data;

            /* We have HDIO_DRIVE_CMD which can only read, but not write,
             * and cannot do LBA. We use it for all read commands. */

            ioctl_data = g_alloca(4 + *len);
            memset(ioctl_data, 0, 4 + *len);

            ioctl_data[0] = (guint8) command;  /* COMMAND */
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
            guint8 ioctl_data[7];

            /* We have HDIO_DRIVE_TASK which can neither read nor
             * write, but can do LBA. We use it for all commands that
             * do neither read nor write */

            memset(ioctl_data, 0, sizeof(ioctl_data));

            ioctl_data[0] = (guint8) command;  /* COMMAND */
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
            g_assert_not_reached();
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

static int disk_passthrough_command(SkDevice *d, SkAtaCommand command, SkDirection direction, gpointer cmd_data, gpointer data, size_t *len) {
    guint8 *bytes = cmd_data;
    guint8 cdb[16];
    guint8 sense[32];
    guint8 *desc = sense+8;
    int ret;

    static const int direction_map[] = {
        [SK_DIRECTION_NONE] = SG_DXFER_NONE,
        [SK_DIRECTION_IN] = SG_DXFER_FROM_DEV,
        [SK_DIRECTION_OUT] = SG_DXFER_TO_DEV
    };

    g_assert(d->type == SK_DEVICE_TYPE_ATA_PASSTHROUGH);

    /* ATA Pass-Through 16 byte command, as described in "T10 04-262r8
     * ATA Command Pass-Through":
     * http://www.t10.org/ftp/t10/document.04/04-262r8.pdf */

    memset(cdb, 0, sizeof(cdb));

    cdb[0] = 0x85; /* OPERATION CODE: 16 byte pass through */

    if (direction == SK_DIRECTION_NONE) {
        cdb[1] = 3 << 1; /* PROTOCOL: Non-Data */
        cdb[2] = 0x20;     /* OFF_LINE=0, CK_COND=1, T_DIR=0, BYT_BLOK=0, T_LENGTH=0 */

    } else if (direction == SK_DIRECTION_IN) {
        cdb[1] = 4 << 1; /* PROTOCOL: PIO Data-in */
        cdb[2] = 0x2e;     /* OFF_LINE=0, CK_COND=1, T_DIR=1, BYT_BLOK=1, T_LENGTH=2 */

    } else if (direction == SK_DIRECTION_OUT) {
        cdb[1] = 5 << 1; /* PROTOCOL: PIO Data-Out */
        cdb[2] = 0x26;     /* OFF_LINE=0, CK_COND=1, T_DIR=0, BYT_BLOK=1, T_LENGTH=2 */
    }

    cdb[3] = bytes[0]; /* FEATURES */
    cdb[4] = bytes[1];

    cdb[5] = bytes[2]; /* SECTORS */
    cdb[6] = bytes[3];

    cdb[8] = bytes[9]; /* LBA LOW */
    cdb[10] = bytes[8]; /* LBA MED */
    cdb[12] = bytes[7]; /* LBA HIGH */

    cdb[13] = bytes[10] & 0x4F; /* SELECT */
    cdb[14] = (guint8) command;

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
    bytes[7] = desc[10];
    bytes[10] = desc[12];
    bytes[11] = desc[13];

    return ret;
}

static int disk_command(SkDevice *d, SkAtaCommand command, SkDirection direction, gpointer cmd_data, gpointer data, size_t *len) {

    static int (* const disk_command_table[_SK_DEVICE_TYPE_MAX]) (SkDevice *d, SkAtaCommand command, SkDirection direction, gpointer cmd_data, gpointer data, size_t *len) = {
        [SK_DEVICE_TYPE_ATA] = disk_ata_command,
        [SK_DEVICE_TYPE_ATA_PASSTHROUGH] = disk_passthrough_command,
    };

    g_assert(d);
    g_assert(d->type <= _SK_DEVICE_TYPE_MAX);
    g_assert(direction <= _SK_DIRECTION_MAX);

    g_assert(direction == SK_DIRECTION_NONE || (data && len && *len > 0));
    g_assert(direction != SK_DIRECTION_NONE || (!data && !len));

    return disk_command_table[d->type](d, command, direction, cmd_data, data, len);
}

static int disk_identify_device(SkDevice *d) {
    guint16 cmd[6];
    int ret;
    size_t len = 512;

    memset(cmd, 0, sizeof(cmd));

    cmd[1] = GUINT16_TO_BE(1);

    if ((ret = disk_command(d, SK_ATA_COMMAND_IDENTIFY_DEVICE, SK_DIRECTION_IN, cmd, d->identify, &len)) < 0)
        return ret;

    if (len != 512) {
        errno = EIO;
        return -1;
    }

    d->identify_data_valid = TRUE;

    return 0;
}

int sk_disk_check_power_mode(SkDevice *d, gboolean *mode) {
    int ret;
    guint16 cmd[6];

    if (!d->identify_data_valid) {
        errno = ENOTSUP;
        return -1;
    }

    memset(cmd, 0, sizeof(cmd));

    if ((ret = disk_command(d, SK_ATA_COMMAND_CHECK_POWER_MODE, SK_DIRECTION_NONE, cmd, NULL, 0)) < 0)
        return ret;

    if (cmd[0] != 0 || (GUINT16_FROM_BE(cmd[5]) & 1) != 0) {
        errno = EIO;
        return -1;
    }

    *mode = GUINT16_FROM_BE(cmd[1]) == 0xFF;

    return 0;
}

static int disk_smart_enable(SkDevice *d, gboolean b) {
    guint16 cmd[6];

    if (!disk_smart_is_available(d)) {
        errno = ENOTSUP;
        return -1;
    }

    memset(cmd, 0, sizeof(cmd));

    cmd[0] = GUINT16_TO_BE(b ? SK_SMART_COMMAND_ENABLE_OPERATIONS : SK_SMART_COMMAND_DISABLE_OPERATIONS);
    cmd[2] = GUINT16_TO_BE(0x0000U);
    cmd[3] = GUINT16_TO_BE(0x00C2U);
    cmd[4] = GUINT16_TO_BE(0x4F00U);

    return disk_command(d, SK_ATA_COMMAND_SMART, SK_DIRECTION_NONE, cmd, NULL, 0);
}

int sk_disk_smart_read_data(SkDevice *d) {
    guint16 cmd[6];
    int ret;
    size_t len = 512;

    if (!disk_smart_is_available(d)) {
        errno = ENOTSUP;
        return -1;
    }

    memset(cmd, 0, sizeof(cmd));

    cmd[0] = GUINT16_TO_BE(SK_SMART_COMMAND_READ_DATA);
    cmd[1] = GUINT16_TO_BE(1);
    cmd[2] = GUINT16_TO_BE(0x0000U);
    cmd[3] = GUINT16_TO_BE(0x00C2U);
    cmd[4] = GUINT16_TO_BE(0x4F00U);

    if ((ret = disk_command(d, SK_ATA_COMMAND_SMART, SK_DIRECTION_IN, cmd, d->smart_data, &len)) < 0)
        return ret;

    d->smart_data_valid = TRUE;

    return ret;
}

static int disk_smart_read_thresholds(SkDevice *d) {
    guint16 cmd[6];
    int ret;
    size_t len = 512;

    if (!disk_smart_is_available(d)) {
        errno = ENOTSUP;
        return -1;
    }

    memset(cmd, 0, sizeof(cmd));

    cmd[0] = GUINT16_TO_BE(SK_SMART_COMMAND_READ_THRESHOLDS);
    cmd[1] = GUINT16_TO_BE(1);
    cmd[2] = GUINT16_TO_BE(0x0000U);
    cmd[3] = GUINT16_TO_BE(0x00C2U);
    cmd[4] = GUINT16_TO_BE(0x4F00U);

    if ((ret = disk_command(d, SK_ATA_COMMAND_SMART, SK_DIRECTION_IN, cmd, d->smart_threshold_data, &len)) < 0)
        return ret;

    d->smart_threshold_data_valid = TRUE;

    return ret;
}

/* int disk_smart_status(SkDevice *d, SmartLogAddress a, gboolean *b) { */
/*     guint16 cmd[6]; */

/*     guint8 data[16]; */

/*     cmd[0] = GUINT16_TO_BE(SMART_RETURN_STATUS); */
/*     cmd[1] = GUINT16_TO_BE(0x0000U); */
/*     cmd[3] = GUINT16_TO_BE(0x00C2U); */
/*     cmd[4] = GUINT16_TO_BE(0x4F00U | (guint16) a); */

/*     ret = disk_ata_command(SK_ATA_SMART, cmd, sizeof(cmd), NULL, 0); */

/*     return ret; */
/* } */

/* int disk_smart_immediate_offline(SkDevice *d, SmartTestType type) { */
/*     guint16 cmd[6]; */

/*     memset(cmd, 0, sizeof(cmd)); */

/*     cmd[0] = GUINT16_TO_BE(SMART_EXECUTE_OFFLINE_IMMEDIATE); */
/*     cmd[2] = GUINT16_TO_BE(0x0000U); */
/*     cmd[3] = GUINT16_TO_BE(0x00C2U); */
/*     cmd[4] = GUINT16_TO_BE(0x4F00U | (guint16) type); */

/*     return disk_ata_command(SK_ATA_SMART, cmd, sizeof(cmd), NULL, 0); */
/* } */

static void swap_strings(gchar *s, size_t len) {
    g_assert((len & 1) == 0);

    for (; len > 0; s += 2, len -= 2) {
        gchar t;
        t = s[0];
        s[0] = s[1];
        s[1] = t;
    }
}

static void clean_strings(gchar *s) {
    gchar *e;

    for (e = s; *e; e++)
        if (!g_ascii_isprint(*e))
            *e = ' ';
}

static void drop_spaces(gchar *s) {
    gchar *d = s;
    gboolean prev_space = FALSE;

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

static void read_string(gchar *d, guint8 *s, size_t len) {
    memcpy(d, s, len);
    d[len] = 0;
    swap_strings(d, len);
    clean_strings(d);
    drop_spaces(d);
}

int sk_disk_identify_parse(SkDevice *d, const SkIdentifyParsedData **ipd) {

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

int sk_disk_smart_is_available(SkDevice *d, gboolean *b) {

    if (!d->identify_data_valid) {
        errno = ENOTSUP;
        return -1;
    }

    *b = disk_smart_is_available(d);
    return 0;
}

int sk_disk_identify_is_available(SkDevice *d, gboolean *b) {

    *b = d->identify_data_valid;
    return 0;
}

const char *sk_offline_data_collection_status_to_string(SkOfflineDataCollectionStatus status) {

    static const char* const map[] = {
        [SK_OFFLINE_DATA_COLLECTION_STATUS_NEVER] = "Off-line data collection activity was never started.",
        [SK_OFFLINE_DATA_COLLECTION_STATUS_SUCCESS] = "Off-line data collection activity was completed without error.",
        [SK_OFFLINE_DATA_COLLECTION_STATUS_INPROGRESS] = "Off-line activity in progress.",
        [SK_OFFLINE_DATA_COLLECTION_STATUS_SUSPENDED] = "Off-line data collection activity was suspended by an interrupting command from host.",
        [SK_OFFLINE_DATA_COLLECTION_STATUS_ABORTED] = "Off-line data collection activity was aborted by an interrupting command from host.",
        [SK_OFFLINE_DATA_COLLECTION_STATUS_FATAL] = "Off-line data collection activity was aborted by the device with a fatal error.",
        [SK_OFFLINE_DATA_COLLECTION_STATUS_UNKNOWN] = "Unknown status"
    };

    if (status >= _SK_OFFLINE_DATA_COLLECTION_STATUS_MAX)
        return NULL;

    return map[status];
}

typedef struct SkSmartAttributeInfo {
    const char *name;
    SkSmartAttributeUnit unit;
} SkSmartAttributeInfo;

/* This data is stolen from smartmontools */
static const SkSmartAttributeInfo const attribute_info[255] = {
    [1]   = { "raw-read-error-rate",         SK_SMART_ATTRIBUTE_UNIT_NONE },
    [2]   = { "throughput-perfomance",       SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
    [3]   = { "spin-up-time",                SK_SMART_ATTRIBUTE_UNIT_MSECONDS },
    [4]   = { "start-stop-count",            SK_SMART_ATTRIBUTE_UNIT_NONE },
    [5]   = { "reallocated-sector-count",    SK_SMART_ATTRIBUTE_UNIT_NONE },
    [6]   = { "read-channel-margin",         SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
    [7]   = { "seek-error-rate",             SK_SMART_ATTRIBUTE_UNIT_NONE },
    [8]   = { "seek-time-perfomance",        SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
    [10]  = { "spin-retry-count",            SK_SMART_ATTRIBUTE_UNIT_NONE },
    [11]  = { "calibration-retry-count",     SK_SMART_ATTRIBUTE_UNIT_NONE },
    [12]  = { "power-cycle-count",           SK_SMART_ATTRIBUTE_UNIT_NONE },
    [13]  = { "read-soft-error-rate",        SK_SMART_ATTRIBUTE_UNIT_NONE },
    [187] = { "reported-uncorrect",          SK_SMART_ATTRIBUTE_UNIT_SECTORS },
    [189] = { "high-fly-writes",             SK_SMART_ATTRIBUTE_UNIT_NONE },
    [190] = { "airflow-temperature-celsius", SK_SMART_ATTRIBUTE_UNIT_KELVIN },
    [191] = { "g-sense-error-rate",          SK_SMART_ATTRIBUTE_UNIT_NONE },
    [192] = { "power-off-retract-count",     SK_SMART_ATTRIBUTE_UNIT_NONE },
    [193] = { "load-cycle-count",            SK_SMART_ATTRIBUTE_UNIT_NONE },
    [194] = { "temperature-celsius-2",       SK_SMART_ATTRIBUTE_UNIT_KELVIN },
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
    [225] = { "load-cycle-count",            SK_SMART_ATTRIBUTE_UNIT_NONE },
    [226] = { "load-in-time",                SK_SMART_ATTRIBUTE_UNIT_MSECONDS },
    [227] = { "torq-amp-count",              SK_SMART_ATTRIBUTE_UNIT_NONE },
    [228] = { "power-off-retract-count",     SK_SMART_ATTRIBUTE_UNIT_NONE },
    [230] = { "head-amplitude",              SK_SMART_ATTRIBUTE_UNIT_UNKNOWN },
    [231] = { "temperature-celsius-1",       SK_SMART_ATTRIBUTE_UNIT_KELVIN },
    [240] = { "head-flying-hours",           SK_SMART_ATTRIBUTE_UNIT_MSECONDS },
    [250] = { "read-error-retry-rate",       SK_SMART_ATTRIBUTE_UNIT_NONE },
};

static void make_pretty(SkSmartAttribute *a) {
    guint64 fourtyeight;

    if (!a->name)
        return;

    if (a->pretty_unit == SK_SMART_ATTRIBUTE_UNIT_UNKNOWN)
        return;

    fourtyeight =
        ((guint64) a->raw[0]) |
        (((guint64) a->raw[1]) << 8) |
        (((guint64) a->raw[2]) << 16) |
        (((guint64) a->raw[3]) << 24) |
        (((guint64) a->raw[4]) << 32) |
        (((guint64) a->raw[5]) << 40);

    if (!strcmp(a->name, "spin-up-time"))
        a->pretty_value = fourtyeight & 0xFFFF;
    else if (!strcmp(a->name, "airflow-temperature-celsius") ||
        !strcmp(a->name, "temperature-celsius-1") ||
        !strcmp(a->name, "temperature-celsius-2")) {
        a->pretty_value = (fourtyeight & 0xFFFF) + 273;
    } else if (!strcmp(a->name, "power-on-minutes"))
        a->pretty_value = fourtyeight * 60 * 1000;
    else if (!strcmp(a->name, "power-on-seconds"))
        a->pretty_value = fourtyeight * 1000;
    else if (!strcmp(a->name, "power-on-hours") ||
             !strcmp(a->name, "loaded-hours") ||
             !strcmp(a->name, "head-flying-hours"))
        a->pretty_value = fourtyeight * 60 * 60 * 1000;
    else
        a->pretty_value = fourtyeight;

}

static const SkSmartAttributeInfo *lookup_attribute(SkDevice *d, guint8 id, SkSmartAttributeInfo *space) {
    const SkIdentifyParsedData *ipd;

    /* These are the simple cases */
    if (attribute_info[id].name)
        return &attribute_info[id];

    /* These are the complex ones */
    if (sk_disk_identify_parse(d, &ipd) < 0)
        return NULL;

    switch (id) {
        case 9:

            if (strstr(ipd->model, "Maxtor"))
                space->name = "power-on-minutes";
            else if (strstr(ipd->model, "Fujitsu") || strstr(ipd->model, "FUJITSU"))
                space->name = "power-on-seconds";
            else
                space->name = "power-on-hours";

            space->unit = SK_SMART_ATTRIBUTE_UNIT_MSECONDS;

            return space;
    }

    return NULL;
}

int sk_disk_smart_parse(SkDevice *d, const SkSmartParsedData **spd) {

    if (!d->smart_data_valid) {
        errno = ENOENT;
        return -1;
    }

    switch (d->smart_data[362]) {
        case 0x00:
        case 0x80:
            d->smart_parsed_data.offline_data_collection_status = SK_OFFLINE_DATA_COLLECTION_STATUS_NEVER;
            break;

        case 0x02:
        case 0x82:
            d->smart_parsed_data.offline_data_collection_status = SK_OFFLINE_DATA_COLLECTION_STATUS_SUCCESS;
            break;

        case 0x03:
            d->smart_parsed_data.offline_data_collection_status = SK_OFFLINE_DATA_COLLECTION_STATUS_INPROGRESS;
            break;

        case 0x04:
        case 0x84:
            d->smart_parsed_data.offline_data_collection_status = SK_OFFLINE_DATA_COLLECTION_STATUS_SUSPENDED;
            break;

        case 0x05:
        case 0x85:
            d->smart_parsed_data.offline_data_collection_status = SK_OFFLINE_DATA_COLLECTION_STATUS_ABORTED;
            break;

        case 0x06:
        case 0x86:
            d->smart_parsed_data.offline_data_collection_status = SK_OFFLINE_DATA_COLLECTION_STATUS_FATAL;
            break;

        default:
            d->smart_parsed_data.offline_data_collection_status = SK_OFFLINE_DATA_COLLECTION_STATUS_UNKNOWN;
            break;
    }

    d->smart_parsed_data.selftest_execution_percent_remaining = 10*(d->smart_data[363] & 0xF);

    d->smart_parsed_data.total_offline_data_collection_seconds = (guint16) d->smart_data[364] | ((guint16) d->smart_data[365] << 8);

    d->smart_parsed_data.conveyance_test_available = !!(d->smart_data[367] & 32);
    d->smart_parsed_data.short_and_extended_test_available = !!(d->smart_data[367] & 16);
    d->smart_parsed_data.start_test_available = !!(d->smart_data[367] & 1);
    d->smart_parsed_data.abort_test_available = !!(d->smart_data[367] & 41);

    d->smart_parsed_data.short_test_polling_minutes = d->smart_data[372];
    d->smart_parsed_data.extended_test_polling_minutes = d->smart_data[373] != 0xFF ? d->smart_data[373] : ((guint16) d->smart_data[376] << 8 | (guint16) d->smart_data[375]);
    d->smart_parsed_data.conveyance_test_polling_minutes = d->smart_data[374];

    *spd = &d->smart_parsed_data;

    return 0;
}

static void find_threshold(SkDevice *d, SkSmartAttribute *a) {
    guint8 *p;
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
        return;
    }

    a->threshold = p[1];
    a->threshold_valid = TRUE;

    a->bad =
        a->worst_value <= a->threshold ||
        a->current_value <= a->threshold;
}

int sk_disk_smart_parse_attributes(SkDevice *d, SkSmartAttributeCallback cb, gpointer userdata) {
    guint8 *p;
    unsigned n;

    if (!d->smart_data_valid) {
        errno = ENOENT;
        return -1;
    }

    for (n = 0, p = d->smart_data + 2; n < 30; n++, p+=12) {
        SkSmartAttribute a;
        SkSmartAttributeInfo space;
        const SkSmartAttributeInfo *i;

        if (p[0] == 0)
            continue;

        memset(&a, 0, sizeof(a));
        a.id = p[0];
        a.current_value = p[3];
        a.worst_value = p[4];

        a.flag = p[2];
        a.prefailure = !!(p[1] & 1);
        a.online = !!(p[1] & 2);

        memcpy(a.raw, p+5, 6);

        if ((i = lookup_attribute(d, p[0], &space))) {
            a.name = i->name;
            a.pretty_unit = i->unit;
        }

        make_pretty(&a);

        find_threshold(d, &a);

        if (cb)
            cb(d, &a, userdata);
    }

    return 0;
}

static const char *yes_no(gboolean b) {
    return  b ? "yes" : "no";
}

const char* sk_smart_attribute_unit_to_string(SkSmartAttributeUnit unit) {

    const char * const map[] = {
        [SK_SMART_ATTRIBUTE_UNIT_UNKNOWN] = NULL,
        [SK_SMART_ATTRIBUTE_UNIT_NONE] = "",
        [SK_SMART_ATTRIBUTE_UNIT_MSECONDS] = "ms",
        [SK_SMART_ATTRIBUTE_UNIT_SECTORS] = "sectors",
        [SK_SMART_ATTRIBUTE_UNIT_KELVIN] = "K"
    };

    if (unit >= _SK_SMART_ATTRIBUTE_UNIT_MAX)
        return NULL;

    return map[unit];
}

static char* print_name(char *s, size_t len, guint8 id, const char *k) {

    if (k)
        g_strlcpy(s, k, len);
    else
        g_snprintf(s, len, "%u", id);

    return s;

}

static char *print_value(char *s, size_t len, const SkSmartAttribute *a) {

    switch (a->pretty_unit) {
        case SK_SMART_ATTRIBUTE_UNIT_MSECONDS:

            if (a->pretty_value >= 1000LLU*60LLU*60LLU*24LLU*365LLU)
                g_snprintf(s, len, "%0.1f years", ((double) a->pretty_value)/(1000.0*60*60*24*365));
            else if (a->pretty_value >= 1000LLU*60LLU*60LLU*24LLU*30LLU)
                g_snprintf(s, len, "%0.1f months", ((double) a->pretty_value)/(1000.0*60*60*24*30));
            else if (a->pretty_value >= 1000LLU*60LLU*60LLU*24LLU)
                g_snprintf(s, len, "%0.1f days", ((double) a->pretty_value)/(1000.0*60*60*24));
            else if (a->pretty_value >= 1000LLU*60LLU*60LLU)
                g_snprintf(s, len, "%0.1f h", ((double) a->pretty_value)/(1000.0*60*60));
            else if (a->pretty_value >= 1000LLU*60LLU)
                g_snprintf(s, len, "%0.1f min", ((double) a->pretty_value)/(1000.0*60));
            else if (a->pretty_value >= 1000LLU)
                g_snprintf(s, len, "%0.1f s", ((double) a->pretty_value)/(1000.0));
            else
                g_snprintf(s, len, "%llu ms", (unsigned long long) a->pretty_value);

            break;

        case SK_SMART_ATTRIBUTE_UNIT_KELVIN:

            g_snprintf(s, len, "%lli C", (long long) a->pretty_value - 273);
            break;

        case SK_SMART_ATTRIBUTE_UNIT_SECTORS:
            g_snprintf(s, len, "%llu sectors", (unsigned long long) a->pretty_value);
            break;

        case SK_SMART_ATTRIBUTE_UNIT_NONE:
            g_snprintf(s, len, "%llu", (unsigned long long) a->pretty_value);
            break;

        case SK_SMART_ATTRIBUTE_UNIT_UNKNOWN:
            g_snprintf(s, len, "n/a");
            break;

        case _SK_SMART_ATTRIBUTE_UNIT_MAX:
            g_assert_not_reached();
    }

    return s;
};

static void disk_dump_attributes(SkDevice *d, const SkSmartAttribute *a, gpointer userdata) {
    char name[32];
    char pretty[32];
    char t[32];

    g_snprintf(t, sizeof(t), "%3u", a->threshold);

    g_print("%3u %-27s %3u   %3u   %-3s   %-11s %-7s %-7s %-3s\n",
            a->id,
            print_name(name, sizeof(name), a->id, a->name),
            a->current_value,
            a->worst_value,
            a->threshold_valid ? t : "n/a",
            print_value(pretty, sizeof(pretty), a),
            a->prefailure ? "prefail" : "old-age",
            a->online ? "online" : "offline",
            yes_no(!a->bad));
}

int sk_disk_dump(SkDevice *d) {
    int ret;
    gboolean powered = FALSE;

    g_print("Device: %s\n"
            "Size: %lu MiB\n",
            d->name,
            (unsigned long) (d->size/1024/1024));

    if (d->identify_data_valid) {
        const SkIdentifyParsedData *ipd;

        if ((ret = sk_disk_identify_parse(d, &ipd)) < 0)
            return ret;

        g_print("Model: [%s]\n"
                "Serial: [%s]\n"
                "Firmware: [%s]\n"
                "SMART Available: %s\n",
                ipd->model,
                ipd->serial,
                ipd->firmware,
                yes_no(disk_smart_is_available(d)));
    }

    ret = sk_disk_check_power_mode(d, &powered);
    g_print("Spin-up: %s\n",
            ret >= 0 ? yes_no(powered) : "unknown");

    if (disk_smart_is_available(d)) {
        const SkSmartParsedData *spd;

        if ((ret = sk_disk_smart_read_data(d)) < 0)
            return ret;

        if ((ret = sk_disk_smart_parse(d, &spd)) < 0)
            return ret;

        g_print("Off-line Collection Status: %s\n"
                "Percent Self-Test Remaining: %u%%\n"
                "Total Time To Complete Off-Line Data Collection: %u s\n"
                "Conveyance Self-Test Available: %s\n"
                "Short/Extended Self-Test Available: %s\n"
                "Start Self-Test Available: %s\n"
                "Abort Self-Test Available: %s\n",
                sk_offline_data_collection_status_to_string(spd->offline_data_collection_status),
                spd->selftest_execution_percent_remaining,
                spd->total_offline_data_collection_seconds,
                yes_no(spd->conveyance_test_available),
                yes_no(spd->short_and_extended_test_available),
                yes_no(spd->start_test_available),
                yes_no(spd->abort_test_available));

        if (spd->short_and_extended_test_available)
            g_print("Short Self-Test Polling Time: %u min\n"
                    "Extended Self-Test Polling Time: %u min\n",
                    spd->short_test_polling_minutes,
                    spd->extended_test_polling_minutes);

        if (spd->conveyance_test_available)
            g_print("Conveyance Self-Test Polling Time: %u min\n",
                    spd->conveyance_test_polling_minutes);

        g_print("%3s %-27s %5s %5s %5s %-11s %-7s %-7s %-3s\n",
                "ID#",
                "Name",
                "Value",
                "Worst",
                "Thres",
                "Pretty",
                "Type",
                "Updates",
                "Good");

        if ((ret = sk_disk_smart_parse_attributes(d, disk_dump_attributes, NULL)) < 0)
            return ret;
    }

    return 0;
}

int sk_disk_get_size(SkDevice *d, guint64 *bytes) {

    *bytes = d->size;
    return 0;
}

int sk_disk_open(const gchar *name, SkDevice **_d) {
    SkDevice *d;
    int ret = -1;

    g_assert(name);
    g_assert(_d);

    d = g_new0(SkDevice, 1);
    d->name = g_strdup(name);

    if ((d->fd = open(name, O_RDWR|O_NOCTTY)) < 0) {
        ret = d->fd;
        goto fail;
    }

    if ((ret = ioctl(d->fd, BLKGETSIZE64, &d->size)) < 0)
        goto fail;

    if (d->size <= 0 || d->size == (guint64) -1) {
        errno = EINVAL;
        goto fail;
    }

    /* Find a way to identify the device */
    for (d->type = 0; d->type != SK_DEVICE_TYPE_UNKNOWN; d->type++)
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

void sk_disk_free(SkDevice *d) {
    g_assert(d);

    if (d->fd >= 0)
        close(d->fd);

    g_free(d->name);
    g_free(d);
}
