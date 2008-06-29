#ifndef foosmarthfoo
#define foosmarthfoo

#include <glib.h>

typedef struct SkDevice SkDevice;

typedef struct SkIdentifyParsedData {
    gchar serial[21];
    gchar firmware[9];
    gchar model[41];
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

typedef struct SkSmartParsedData {
    SkSmartOfflineDataCollectionStatus offline_data_collection_status;
    unsigned selftest_execution_percent_remaining;
    unsigned total_offline_data_collection_seconds;

    gboolean conveyance_test_available:1;
    gboolean short_and_extended_test_available:1;
    gboolean start_test_available:1;
    gboolean abort_test_available:1;

    unsigned short_test_polling_minutes;
    unsigned extended_test_polling_minutes;
    unsigned conveyance_test_polling_minutes;

    /* This structure may be extended at any time without being
     * considered an ABI change. So take care when you copy it.  */
} SkSmartParsedData;

typedef enum SkSmartAttributeUnit {
    SK_SMART_ATTRIBUTE_UNIT_UNKNOWN,
    SK_SMART_ATTRIBUTE_UNIT_NONE,
    SK_SMART_ATTRIBUTE_UNIT_MSECONDS,
    SK_SMART_ATTRIBUTE_UNIT_SECTORS,
    SK_SMART_ATTRIBUTE_UNIT_KELVIN,
    _SK_SMART_ATTRIBUTE_UNIT_MAX
} SkSmartAttributeUnit;

typedef struct SkSmartAttribute {
    /* Static data */
    guint8 id;
    const char *name;
    SkSmartAttributeUnit pretty_unit; /* for pretty value */

    guint8 threshold;
    gboolean threshold_valid:1;

    gboolean online:1;
    gboolean prefailure:1;

    guint8 flag;

    /* Volatile data */
    gboolean bad:1;
    guint8 current_value, worst_value;
    guint64 pretty_value;
    guint8 raw[6];

    /* This structure may be extended at any time without being
     * considered an ABI change. So take care when you copy it. */
} SkSmartAttribute;

typedef void (*SkSmartAttributeCallback)(SkDevice *d, const SkSmartAttribute *a, gpointer userdata);

int sk_disk_open(const gchar *name, SkDevice **d);

int sk_disk_check_power_mode(SkDevice *d, gboolean *mode);

int sk_disk_identify_is_available(SkDevice *d, gboolean *b);
int sk_disk_identify_parse(SkDevice *d, const SkIdentifyParsedData **data);

int sk_disk_smart_is_available(SkDevice *d, gboolean *b);
int sk_disk_smart_read_data(SkDevice *d);
int sk_disk_smart_parse(SkDevice *d, const SkSmartParsedData **data);
int sk_disk_smart_parse_attributes(SkDevice *d, SkSmartAttributeCallback cb, gpointer userdata);

int sk_disk_get_size(SkDevice *d, guint64 *bytes);

int sk_disk_dump(SkDevice *d);

void sk_disk_free(SkDevice *d);

const char* sk_smart_offline_data_collection_status_to_string(SkSmartOfflineDataCollectionStatus status);
const char* sk_smart_attribute_unit_to_string(SkSmartAttributeUnit unit);

#endif
