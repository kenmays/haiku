#ifndef _FLOPPY_PROFILES_H
#define _FLOPPY_PROFILES_H

#include <SupportDefs.h>

struct floppy_geometry {
	uint32 sectors_per_track;
	uint32 heads;
	uint32 cylinders;
	uint32 bytes_per_sector;
	uint32 data_rate_kbps;
	uint32 rpm;
};

static const floppy_geometry kFloppy1440 = { 18, 2, 80, 512, 500, 300 };
static const floppy_geometry kFloppy1200 = { 15, 2, 80, 512, 500, 360 };
static const floppy_geometry kFloppy360 = { 9, 2, 40, 512, 250, 300 };

#endif
