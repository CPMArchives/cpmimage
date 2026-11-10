// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"
#include <stdio.h>
#include "device.h"
int main(int argc,char **argv) {
 struct Device dev={0};const char *error;
 if(argc!=2)return 2;
 error=Device_open(&dev,argv[1],O_RDONLY,"raw");if(error){fprintf(stderr,"%s\n",error);return 2;}
 printf("probe cylinders=%u heads=%u\n",dev.geom.dg_cylinders,dev.geom.dg_heads);
 error=Device_setGeometry(&dev,128,26,77,0,NULL);if(error){fprintf(stderr,"%s\n",error);return 2;}
 printf("configured cylinders=%u heads=%u total_tracks=%u\n",dev.geom.dg_cylinders,dev.geom.dg_heads,dev.geom.dg_cylinders*dev.geom.dg_heads);
 Device_close(&dev);return 0;
}
