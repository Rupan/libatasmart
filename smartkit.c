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
#include <glib.h>

#define SK_TIMEOUT 2000

typedef enum SkDirection {
    SK_DIRECTION_NONE,
    SK_DIRECTION_IN,
    SK_DIRECTION_OUT,
    _SK_DIRECTION_MAX
} SkDirection;

typedef enum SkDeviceType {
    SK_DEVICE_TYPE_ATA,
    SK_DEVICE_TYPE_SCSI,
    SK_DEVICE_TYPE_ATA_PASSTHROUGH, /* ATA passthrough over SCSI transport */
    _SK_DEVICE_TYPE_MAX
} SkDeviceType;

typedef struct SkDevice {
    gchar *name;
    int fd;
    SkDeviceType type;
    guint8 identify[512];
    guint8 smart_data[512];
    gboolean smart_data_valid;
} SkDevice;

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

static int disk_scsi_command(SkDevice *d, SkAtaCommand command, SkDirection direction, gpointer cmd_data, gpointer data, size_t *len) {
    g_assert(d->type == SK_DEVICE_TYPE_SCSI);

    g_warning("SCSI disks not yet supported because Lennart doesn't have any to test this with.");

    errno = ENOTSUP;
    return -1;
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

int sk_disk_command(SkDevice *d, SkAtaCommand command, SkDirection direction, gpointer cmd_data, gpointer data, size_t *len) {

    static int (* const disk_command[_SK_DEVICE_TYPE_MAX]) (SkDevice *d, SkAtaCommand command, SkDirection direction, gpointer cmd_data, gpointer data, size_t *len) = {
        [SK_DEVICE_TYPE_ATA] = disk_ata_command,
        [SK_DEVICE_TYPE_SCSI] = disk_scsi_command,
        [SK_DEVICE_TYPE_ATA_PASSTHROUGH] = disk_passthrough_command,
    };

    g_assert(d);
    g_assert(d->type <= _SK_DEVICE_TYPE_MAX);
    g_assert(direction <= _SK_DIRECTION_MAX);

    g_assert(direction == SK_DIRECTION_NONE || (data && len && *len > 0));
    g_assert(direction != SK_DIRECTION_NONE || (!data && !len));

    return disk_command[d->type](d, command, direction, cmd_data, data, len);
}

int sk_disk_identify_device(SkDevice *d) {
    guint16 cmd[6];
    int ret;
    size_t len = 512;

    memset(cmd, 0, sizeof(cmd));

    cmd[1] = GUINT16_TO_BE(1);

    if ((ret = sk_disk_command(d, SK_ATA_COMMAND_IDENTIFY_DEVICE, SK_DIRECTION_IN, cmd, d->identify, &len)) < 0)
        return ret;

    if (len != 512) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int sk_disk_check_power_mode(SkDevice *d, gboolean *mode) {
    int ret;
    guint16 cmd[6];

    memset(cmd, 0, sizeof(cmd));

    if ((ret = sk_disk_command(d, SK_ATA_COMMAND_CHECK_POWER_MODE, SK_DIRECTION_NONE, cmd, NULL, 0)) < 0)
        return ret;

    if (cmd[0] != 0 || (GUINT16_FROM_BE(cmd[5]) & 1) != 0) {
        errno = EIO;
        return -1;
    }

    *mode = GUINT16_FROM_BE(cmd[1]) == 0xFF;

    return 0;
}

int sk_disk_smart_enable(SkDevice *d, gboolean b) {
    guint16 cmd[6];

    memset(cmd, 0, sizeof(cmd));

    cmd[0] = GUINT16_TO_BE(b ? SK_SMART_COMMAND_ENABLE_OPERATIONS : SK_SMART_COMMAND_DISABLE_OPERATIONS);
    cmd[2] = GUINT16_TO_BE(0x0000U);
    cmd[3] = GUINT16_TO_BE(0x00C2U);
    cmd[4] = GUINT16_TO_BE(0x4F00U);

    return sk_disk_command(d, SK_ATA_COMMAND_SMART, SK_DIRECTION_NONE, cmd, NULL, 0);
}

int sk_disk_smart_read_data(SkDevice *d) {
    guint16 cmd[6];
    int ret;
    size_t len = 512;

    memset(cmd, 0, sizeof(cmd));

    cmd[0] = GUINT16_TO_BE(SK_SMART_COMMAND_READ_DATA);
    cmd[1] = GUINT16_TO_BE(1);
    cmd[2] = GUINT16_TO_BE(0x0000U);
    cmd[3] = GUINT16_TO_BE(0x00C2U);
    cmd[4] = GUINT16_TO_BE(0x4F00U);

    if ((ret = sk_disk_command(d, SK_ATA_COMMAND_SMART, SK_DIRECTION_IN, cmd, d->smart_data, &len)) < 0)
        return ret;

    d->smart_data_valid = TRUE;

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

void sk_disk_free(SkDevice *d);

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

static void parse_identify(guint8 identify[512], gchar serial[21], gchar firmware[9], gchar model[41]) {
    read_string(serial, identify+20, 20);
    read_string(firmware, identify+46, 8);
    read_string(model, identify+54, 40);
}

gboolean sk_disk_smart_is_available(SkDevice *d) {
    return !!(d->identify[164] & 1);
}

gboolean sk_disk_smart_is_enabled(SkDevice *d) {
    return !!(d->identify[170] & 1);
}

typedef enum SkOfflineDataCollectionStatus {
    SK_OFFLINE_NEVER,
    SK_OFFLINE_SUCCESS,
    SK_OFFLINE_INPROGRESS,
    SK_OFFLINE_SUSPENDED,
    SK_OFFLINE_ABORTED,
    SK_OFFLINE_FATAL,
    SK_OFFLINE_UNKNOWN
} SkOfflineDataCollectionStatus;

const char* const offline_data_collection_status_map[] = {
    [SK_OFFLINE_NEVER] = "Off-line data collection activity was never started.",
    [SK_OFFLINE_SUCCESS] = "Off-line data collection activity was completed without error.",
    [SK_OFFLINE_INPROGRESS] = "Off-line activity in progress.",
    [SK_OFFLINE_SUSPENDED] = "Off-line data collection activity was suspended by an interrupting command from host.",
    [SK_OFFLINE_ABORTED] = "Off-line data collection activity was aborted by an interrupting command from host.",
    [SK_OFFLINE_FATAL] = "Off-line data collection activity was aborted by the device with a fatal error.",
    [SK_OFFLINE_UNKNOWN] = "Unknown status"
};

typedef struct SkParsedSmartData {
    SkOfflineDataCollectionStatus offline_data_collection_status;
    unsigned selftest_execution_percent_remaining;
    unsigned total_offline_data_collection_seconds;

    gboolean conveyance_test_available:1;
    gboolean short_and_extended_test_available:1;
    gboolean start_test_available:1;
    gboolean abort_test_available:1;

    unsigned short_test_polling_minutes;
    unsigned extended_test_polling_minutes;
    unsigned conveyance_test_polling_minutes;
} SkParsedSmartData;

void sk_disk_smart_parse(SkDevice *d, SkParsedSmartData *data) {
    g_assert(d->smart_data_valid);

    switch (d->smart_data[362]) {
        case 0x00:
        case 0x80:
            data->offline_data_collection_status = SK_OFFLINE_NEVER;
            break;

        case 0x02:
        case 0x82:
            data->offline_data_collection_status = SK_OFFLINE_SUCCESS;
            break;

        case 0x03:
            data->offline_data_collection_status = SK_OFFLINE_INPROGRESS;
            break;

        case 0x04:
        case 0x84:
            data->offline_data_collection_status = SK_OFFLINE_SUSPENDED;
            break;

        case 0x05:
        case 0x85:
            data->offline_data_collection_status = SK_OFFLINE_ABORTED;
            break;

        case 0x06:
        case 0x86:
            data->offline_data_collection_status = SK_OFFLINE_FATAL;
            break;

        default:
            data->offline_data_collection_status = SK_OFFLINE_UNKNOWN;
            break;
    }

    data->selftest_execution_percent_remaining = d->smart_data[363] & 0xF;

    data->total_offline_data_collection_seconds = (guint16) d->smart_data[364] | ((guint16) d->smart_data[365] << 8);

    data->conveyance_test_available = !!(d->smart_data[367] & 32);
    data->short_and_extended_test_available = !!(d->smart_data[367] & 16);
    data->start_test_available = !!(d->smart_data[367] & 1);
    data->abort_test_available = !!(d->smart_data[367] & 41);

    data->short_test_polling_minutes = d->smart_data[372];
    data->extended_test_polling_minutes = d->smart_data[373] != 0xFF ? d->smart_data[373] : ((guint16) d->smart_data[376] << 8 | (guint16) d->smart_data[375]);
    data->conveyance_test_polling_minutes = d->smart_data[374];
}

static const char *yes_no(gboolean b) {
    return  b ? "yes" : "no";
}

static void smart_dump(SkParsedSmartData *d) {
    g_printerr("Off-line Collection Status: %s\n"
               "Percent Self-Test Remaining: %u%%\n"
               "Total Time To Complete Off-Line Data Collection: %u s\n"
               "Conveyance Self-Test Available: %s\n"
               "Short/Extended Self-Test Available: %s\n"
               "Start Self-Test Available: %s\n"
               "Abort Self-Test Available: %s\n",
               offline_data_collection_status_map[d->offline_data_collection_status],
               d->selftest_execution_percent_remaining,
               d->total_offline_data_collection_seconds,
               yes_no(d->conveyance_test_available),
               yes_no(d->short_and_extended_test_available),
               yes_no(d->start_test_available),
               yes_no(d->abort_test_available));

    if (d->short_and_extended_test_available)
        g_printerr("Short Self-Test Polling Time: %u min\n"
                   "Extended Self-Test Polling Time: %u min\n",
                   d->short_test_polling_minutes,
                   d->extended_test_polling_minutes);

    if (d->conveyance_test_available)
        g_printerr("Conveyance Self-Test Polling Time: %u min\n",
                   d->conveyance_test_polling_minutes);
}

int sk_disk_open(const gchar *name, SkDevice **_d) {
    SkDevice *d;
    gboolean powered = FALSE;

    gchar serial[21];
    gchar firmware[9];
    gchar model[41];

    g_assert(name);
    g_assert(_d);

    d = g_new0(SkDevice, 1);
    d->name = g_strdup(name);

    if ((d->fd = open(name, O_RDWR|O_NOCTTY)) < 0)
        goto fail;

    d->type = SK_DEVICE_TYPE_ATA_PASSTHROUGH;
    if (sk_disk_identify_device(d) < 0) {

        d->type = SK_DEVICE_TYPE_ATA;
        if (sk_disk_identify_device(d) < 0) {

            d->type = SK_DEVICE_TYPE_SCSI;
            if (sk_disk_identify_device(d) < 0)
                goto fail;
        }
    }

    parse_identify(d->identify, serial, firmware, model);

    g_printerr("Serial: [%s]\n", serial);
    g_printerr("Firmware: [%s]\n", firmware);
    g_printerr("Model: [%s]\n", model);

    g_printerr("SMART Available: %i\n", sk_disk_smart_is_available(d));
    g_printerr("SMART Enabled: %i\n", sk_disk_smart_is_enabled(d));

    /* Check if driver can do SMART */
    if (!sk_disk_smart_is_available(d)) {
        errno = ENOTSUP;
        goto fail;
    }

    /* Enable SMART */
    if (!sk_disk_smart_is_enabled(d)) {
        if (sk_disk_smart_enable(d, TRUE) < 0)
            goto fail;
        g_printerr("SMART sucessfully enabled.\n");
    }

    if (sk_disk_check_power_mode(d, &powered) < 0)
        g_printerr("Failed to check power mode: %s", strerror(errno));
    else
        g_printerr("Powered up: %i\n", (int) powered);

    if (sk_disk_smart_read_data(d) < 0)
        g_printerr("Failed to read SMART data: %s", strerror(errno));
    else {
        SkParsedSmartData parsed_smart_data;
        sk_disk_smart_parse(d, &parsed_smart_data);
        smart_dump(&parsed_smart_data);
    }

    *_d = d;

    return 0;

fail:

    if (d)
        sk_disk_free(d);

    return -1;
}

void sk_disk_free(SkDevice *d) {
    g_assert(d);

    if (d->fd >= 0)
        close(d->fd);

    g_free(d->name);
    g_free(d);
}

int main(int argc, char *argv[]) {
    int ret;
    SkDevice *d;

    if ((ret = sk_disk_open(argc >= 2 ? argv[1] : "/dev/sda", &d)) < 0) {
        g_printerr("Failed to open disk %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    sk_disk_free(d);
    return 0;
}
