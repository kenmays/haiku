#ifndef RDNA4_IP_H
#define RDNA4_IP_H

#include <SupportDefs.h>

#include "rdna4_backend.h"

/* Discovery result for one IP block. No register access occurs until the
 * block has been positively identified. */
struct rdna4_ip_descriptor {
	rdna4_ip_block block;
	uint16 major;
	uint16 minor;
	uint16 revision;
	uint32 feature_flags;
	bool supported;
};

static inline void
rdna4_ip_set(rdna4_ip_version& version, uint16 major, uint16 minor,
	uint16 revision)
{
	version.major = major;
	version.minor = minor;
	version.revision = revision;
	version.present = true;
}

static inline bool
rdna4_ip_matches(const rdna4_ip_version& version, uint16 major, uint16 minor)
{
	return version.present && version.major == major && version.minor == minor;
}

#endif
