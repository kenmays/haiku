/*
 * Ext4 feature negotiation for Haiku.
 *
 * This is intentionally separate from the legacy ext2 feature masks.  An
 * ext4 mount must never silently accept an incompatible feature merely
 * because the older driver happens to ignore it.
 */
#ifndef EXT4_FEATURE_SET_H
#define EXT4_FEATURE_SET_H

#include "ext2.h"

class Ext4FeatureSet {
public:
	static status_t Validate(const ext2_super_block& superBlock, bool readOnly);
	static bool IsExt4(const ext2_super_block& superBlock);

	static uint32 SupportedCompat();
	static uint32 SupportedReadOnly();
	static uint32 SupportedIncompat();
};

#endif
