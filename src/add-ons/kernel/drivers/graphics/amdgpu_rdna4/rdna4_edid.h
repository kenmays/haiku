#ifndef RDNA4_EDID_H
#define RDNA4_EDID_H

#include <SupportDefs.h>

/* EDID validation is connector-independent.  AUX/DDC transport remains in
 * the DCN4 backend, so this code can be tested without touching hardware. */
status_t rdna4_edid_validate(const uint8* data, size_t size);
status_t rdna4_edid_dimensions(const uint8* data, size_t size,
	uint32* _width, uint32* _height);

#endif
