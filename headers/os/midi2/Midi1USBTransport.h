/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MIDI2_MIDI1_USB_TRANSPORT_H
#define _MIDI2_MIDI1_USB_TRANSPORT_H

#include <SupportDefs.h>
#include <stddef.h>

namespace BPrivate {

/*
 * USB-MIDI 1.0 event packet codec. Each packet is four bytes: cable/CIN,
 * followed by up to three MIDI bytes. USB packets do not use MIDI running
 * status. This class converts one complete MIDI message to/from event packets;
 * USB endpoint I/O and packet scheduling remain the backend's responsibility.
 */
class Midi1USBPacketCodec {
public:
	static status_t DecodeEventPacket(const uint8 packet[4], uint8* output,
		size_t capacity, size_t* written, uint8* cable);
	static status_t EncodeMessage(const uint8* message, size_t messageSize,
		uint8 cable, uint8* output, size_t capacity, size_t* written);
};

} // namespace BPrivate
#endif
