#include <string.h>
#include <errno.h>

#include "smart.h"

int main(int argc, char *argv[]) {
    int ret;
    const char *device;
    SkDevice *d;

    device = argc >= 2 ? argv[1] : "/dev/sda";

    if ((ret = sk_disk_open(device, &d)) < 0) {
        g_printerr("Failed to open disk %s: %s\n", device, strerror(errno));
        return 1;
    }

    sk_disk_dump(d);

    sk_disk_free(d);

    return 0;
}
