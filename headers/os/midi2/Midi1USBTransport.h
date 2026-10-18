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
 * USB-MIDI 1.0 event packet codec. Each event packet is four bytes: cable/CIN,
 * followed by up to three MIDI bytes. USB event packets do not use MIDI
 * running status.
 *
 * DecodeEventPacket() decodes exactly one four-byte event packet. On success,
 * written is the number of reconstructed MIDI bytes and cable is the packet's
 * cable number. It validates the packet before modifying output.
 *
 * EncodeMessage() encodes one complete MIDI message. For SysEx, output holds
 * a sequence of four-byte event packets and must have room for the entire
 * encoded message; insufficient capacity returns B_BUFFER_OVERFLOW without
 * writing a partial SysEx stream. For non-SysEx messages, capacity must be at
 * least four bytes. On failure, written is zero.
 *
 * This codec does not perform USB endpoint I/O, transfer scheduling, device
 * discovery, or cable lifecycle management.
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
