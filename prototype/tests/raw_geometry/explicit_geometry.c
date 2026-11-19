/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Verify explicit OUTBACK geometry survives the implicit-RAW correction. */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "device.h"

int main(int argc, char **argv)
{
    struct Device device;
    char original[512], replacement[512], observed[512];
    const char *error;
    FILE *file;
    int named;
    if (argc != 2) return 2;
    for (named = 0; named < 2; ++named)
    {
        memset(&device, 0, sizeof(device));
        error = Device_open(&device, argv[1], O_RDWR, named ? "raw" : "raw,ibm720");
        if (error) { fprintf(stderr, "%s\n", error); return 1; }
        error = Device_setGeometry(&device, 512, 9, 160, 0, named ? "ibm720" : NULL);
        if (error || device.geom.dg_sidedness != SIDES_OUTBACK ||
            device.geom.dg_cylinders != 80 || device.geom.dg_heads != 2) return 1;
        /* OUTBACK logical track 1 maps to cylinder 1, head 0: RAW track 2. */
        file = fopen(argv[1], "rb");
        if (!file || fseek(file, 18 * 512, SEEK_SET) || fread(original, 1, 512, file) != 512) return 1;
        fclose(file);
        if (Device_readSector(&device, 1, 0, observed) || memcmp(original, observed, 512)) return 1;
        memset(replacement, 0x5a + named, sizeof(replacement));
        if (Device_writeSector(&device, 1, 0, replacement) || Device_close(&device)) return 1;
        file = fopen(argv[1], "rb");
        if (!file || fseek(file, 18 * 512, SEEK_SET) || fread(observed, 1, 512, file) != 512) return 1;
        fclose(file);
        if (memcmp(replacement, observed, 512)) return 1;
        printf("%s OUTBACK geometry and physical read/write offsets passed\n",
               named ? "libdsk:format" : "raw,FORMAT");
    }
    return 0;
}
