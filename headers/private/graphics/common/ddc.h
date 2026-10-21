/*
 * Copyright (c) 2003, Thomas Kurschel
 * Distributed under the terms of the MIT License.
*/
#ifndef _DDC_H
#define _DDC_H


#include "i2c.h"
#include "edid.h"


#ifdef __cplusplus
extern "C" {
#endif

void ddc2_init_timing(i2c_bus *bus);

// Read and checksum-validate the EDID base block and all declared extension
// blocks via DDC2/E-DDC. The decoded structure exposes legacy EDID fields and
// limited CTA/DisplayID summaries; it does not retain arbitrary raw extensions.
// (currently, *vdif and *vdif_len are always set to null)
status_t ddc2_read_edid1(const i2c_bus *bus, edid1_info *edid, 
	void **vdif, size_t *vdifLength);

#ifdef __cplusplus
}
#endif

#endif	/* _DDC_H */
