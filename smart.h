/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef foosmarthfoo
#define foosmarthfoo

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

#include <glib.h>

/* ATA SMART test type (ATA8 7.52.5.2) */
typedef enum SkSmartSelfTest {
        SK_SMART_SELF_TEST_SHORT = 1,
        SK_SMART_SELF_TEST_EXTENDED = 2,
        SK_SMART_SELF_TEST_CONVEYANCE = 3,
        SK_SMART_SELF_TEST_ABORT = 127
} SkSmartSelfTest;

const char* sk_smart_self_test_to_string(SkSmartSelfTest test);

typedef struct SkIdentifyParsedData {
        gchar serial[21];
        gchar firmware[9];
        gchar model[41];

        /* This structure may be extended at any time without this being
         * considered an ABI change. So take care when you copy it. */
} SkIdentifyParsedData;

typedef enum SkSmartOfflineDataCollectionStatus {
        SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_NEVER,
        SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_SUCCESS,
        SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_INPROGRESS,
        SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_SUSPENDED,
        SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_ABORTED,
        SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_FATAL,
        SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_UNKNOWN,
        _SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_MAX
} SkSmartOfflineDataCollectionStatus;

const char* sk_smart_offline_data_collection_status_to_string(SkSmartOfflineDataCollectionStatus status);

typedef enum SkSmartSelfTestExecutionStatus {
        SK_SMART_SELF_TEST_EXECUTION_STATUS_SUCCESS_OR_NEVER = 0,
        SK_SMART_SELF_TEST_EXECUTION_STATUS_ABORTED = 1,
        SK_SMART_SELF_TEST_EXECUTION_STATUS_INTERRUPTED = 2,
        SK_SMART_SELF_TEST_EXECUTION_STATUS_FATAL = 3,
        SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_UNKNOWN = 4,
        SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_ELECTRICAL = 5,
        SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_SERVO = 6,
        SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_READ = 7,
        SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_HANDLING = 8,
        SK_SMART_SELF_TEST_EXECUTION_STATUS_INPROGRESS = 15,
        _SK_SMART_SELF_TEST_EXECUTION_STATUS_MAX
} SkSmartSelfTestExecutionStatus;

const char *sk_smart_self_test_execution_status_to_string(SkSmartSelfTestExecutionStatus status);

typedef struct SkSmartParsedData {
        /* Volatile data */
        SkSmartOfflineDataCollectionStatus offline_data_collection_status;
        unsigned total_offline_data_collection_seconds;
        SkSmartSelfTestExecutionStatus self_test_execution_status;
        unsigned self_test_execution_percent_remaining;

        /* Fixed data */
        gboolean short_and_extended_test_available:1;
        gboolean conveyance_test_available:1;
        gboolean start_test_available:1;
        gboolean abort_test_available:1;

        unsigned short_test_polling_minutes;
        unsigned extended_test_polling_minutes;
        unsigned conveyance_test_polling_minutes;

        /* This structure may be extended at any time without this being
         * considered an ABI change. So take care when you copy it.  */
} SkSmartParsedData;

gboolean sk_smart_self_test_available(const SkSmartParsedData *d, SkSmartSelfTest test);
unsigned sk_smart_self_test_polling_minutes(const SkSmartParsedData *d, SkSmartSelfTest test);

typedef enum SkSmartAttributeUnit {
        SK_SMART_ATTRIBUTE_UNIT_UNKNOWN,
        SK_SMART_ATTRIBUTE_UNIT_NONE,
        SK_SMART_ATTRIBUTE_UNIT_MSECONDS,
        SK_SMART_ATTRIBUTE_UNIT_SECTORS,
        SK_SMART_ATTRIBUTE_UNIT_KELVIN,
        _SK_SMART_ATTRIBUTE_UNIT_MAX
} SkSmartAttributeUnit;

const char* sk_smart_attribute_unit_to_string(SkSmartAttributeUnit unit);

typedef struct SkSmartAttributeParsedData {
        /* Fixed data */
        guint8 id;
        const char *name;
        SkSmartAttributeUnit pretty_unit; /* for pretty_value */

        guint16 flags;

        guint8 threshold;
        gboolean threshold_valid:1;

        gboolean online:1;
        gboolean prefailure:1;

        /* Volatile data */
        gboolean good:1;
        guint8 current_value, worst_value;
        guint64 pretty_value;
        guint8 raw[6];

        /* This structure may be extended at any time without this being
         * considered an ABI change. So take care when you copy it. */
} SkSmartAttributeParsedData;

typedef struct SkDisk SkDisk;

int sk_disk_open(const gchar *name, SkDisk **d);

int sk_disk_get_size(SkDisk *d, guint64 *bytes);

int sk_disk_check_sleep_mode(SkDisk *d, gboolean *awake);

int sk_disk_identify_is_available(SkDisk *d, gboolean *available);
int sk_disk_identify_parse(SkDisk *d, const SkIdentifyParsedData **data);

typedef void (*SkSmartAttributeParseCallback)(SkDisk *d, const SkSmartAttributeParsedData *a, gpointer userdata);

int sk_disk_smart_is_available(SkDisk *d, gboolean *available);
int sk_disk_smart_status(SkDisk *d, gboolean *good);
/* Reading SMART data might cause the disk to wake up from
 * sleep. Hence from monitoring daemons make sure to call
 * sk_disk_check_power_mode() to check wether the disk is sleeping and
 * skip the read if so. */
int sk_disk_smart_read_data(SkDisk *d);
int sk_disk_smart_parse(SkDisk *d, const SkSmartParsedData **data);
int sk_disk_smart_parse_attributes(SkDisk *d, SkSmartAttributeParseCallback cb, gpointer userdata);
int sk_disk_smart_self_test(SkDisk *d, SkSmartSelfTest test);

int sk_disk_dump(SkDisk *d);

void sk_disk_free(SkDisk *d);

#endif
