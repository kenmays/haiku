#ifndef RDNA4_FRONTEND_EDID_H
#define RDNA4_FRONTEND_EDID_H

#include <Accelerant.h>
#include <SupportDefs.h>

struct rdna4_edid_mode_info {
	display_mode mode;
	bool preferred;
};

status_t rdna4_frontend_parse_edid(const uint8* data, size_t size,
	rdna4_edid_mode_info* modes, uint32 capacity, uint32* _count);

#endif
