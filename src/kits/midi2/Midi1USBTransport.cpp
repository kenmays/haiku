/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <midi2/Midi1USBTransport.h>
#include <midi2/Midi1Codec.h>
#include <Errors.h>
#include <string.h>

namespace BPrivate {

static uint8
_CINLength(uint8 cin)
{
	switch (cin) {
		case 0x2: case 0x6: case 0xc: case 0xd: return 2;
		case 0x3: case 0x4: case 0x7:
		case 0x8: case 0x9: case 0xa: case 0xb: case 0xe: return 3;
		case 0x5: case 0xf: return 1;
		default: return 0;
	}
}

status_t
Midi1USBPacketCodec::DecodeEventPacket(const uint8 packet[4], uint8* output,
	size_t capacity, size_t* written, uint8* cable)
{
	if (written == NULL)
		return B_BAD_VALUE;
	*written = 0;
	if (packet == NULL || output == NULL || cable == NULL)
		return B_BAD_VALUE;

	uint8 cin = packet[0] & 0x0f;
	uint8 length = _CINLength(cin);
	if (length == 0 || capacity < length)
		return length == 0 ? B_BAD_DATA : B_BUFFER_OVERFLOW;

	const uint8* midi = packet + 1;
	switch (cin) {
		case 0x2:
			if (midi[0] != 0xf1 && midi[0] != 0xf3)
				return B_BAD_DATA;
			break;
		case 0x3:
			if (midi[0] != 0xf2)
				return B_BAD_DATA;
			break;
		case 0x4:
			if (midi[0] != 0xf0 && (midi[0] & 0x80) != 0)
				return B_BAD_DATA;
			break;
		case 0x5:
			if (midi[0] != 0xf6 && midi[0] != 0xf7)
				return B_BAD_DATA;
			break;
		case 0x6:
		case 0x7:
			// SysEx ending packets must terminate with F7.
			if (midi[length - 1] != 0xf7)
				return B_BAD_DATA;
			break;
		case 0xf:
			if (midi[0] < 0xf8 || midi[0] == 0xf9 || midi[0] == 0xfd)
				return B_BAD_DATA;
			break;
		default:
			if (cin >= 0x8 && cin <= 0xe) {
				if ((midi[0] >> 4) != cin || midi[0] < 0x80
					|| midi[0] > 0xef)
					return B_BAD_DATA;
				uint8 expected = (cin == 0xc || cin == 0xd) ? 2 : 3;
				if (length != expected)
					return B_BAD_DATA;
			}
			break;
	}

	// Validate the complete packet before touching caller output, so malformed
	// packets never leave a partially decoded MIDI message behind.
	for (uint8 i = 0; i < length; i++) {
		bool isStatus = i == 0 && (cin == 0x2 || cin == 0x3
			|| (cin >= 0x8 && cin <= 0xe) || cin == 0xf
			|| (cin == 0x5 && midi[i] == 0xf6));
		bool isSysExStart = (cin == 0x4 || cin == 0x6 || cin == 0x7)
			&& i == 0 && midi[i] == 0xf0;
		bool isEOX = (cin == 0x5 || cin == 0x6 || cin == 0x7)
			&& i == length - 1 && midi[i] == 0xf7;
		if (!isStatus && !isSysExStart && midi[i] > 0x7f && !isEOX)
			return B_BAD_DATA;
	}
	memcpy(output, midi, length);
	*cable = packet[0] >> 4;
	*written = length;
	return B_OK;
}

status_t
Midi1USBPacketCodec::EncodeMessage(const uint8* message, size_t messageSize,
	uint8 cable, uint8* output, size_t capacity, size_t* written)
{
	if (written == NULL)
		return B_BAD_VALUE;
	*written = 0;
	if (message == NULL || messageSize == 0 || output == NULL || cable > 15)
		return B_BAD_VALUE;

	uint8 cin;
	size_t consumed = 0;
	uint8 status = message[0];
	if (status >= 0x80 && status <= 0xef) {
		uint8 expected = Midi1Codec::ChannelMessageLength(status);
		if (messageSize != expected)
			return B_BAD_DATA;
		cin = status >> 4;
		for (size_t i = 1; i < messageSize; i++) {
			if (message[i] > 0x7f)
				return B_BAD_DATA;
		}
		consumed = expected;
	} else if (status == 0xf0) {
		if (messageSize < 2 || message[messageSize - 1] != 0xf7)
			return B_BAD_DATA;
		// Fail atomically when the caller's packet buffer cannot hold the
		// complete SysEx message. Do not leave a truncated stream in output.
		size_t packetCount = messageSize / 3 + (messageSize % 3 != 0 ? 1 : 0);
		if (packetCount > capacity / 4)
			return B_BUFFER_OVERFLOW;
		for (size_t i = 0; i < messageSize; i++) {
			if (i != 0 && i != messageSize - 1 && message[i] > 0x7f)
				return B_BAD_DATA;
		}
		size_t offset = 0;
		while (messageSize - offset > 3) {
			if (capacity - *written < 4)
				return B_BUFFER_OVERFLOW;
			output[*written] = (cable << 4) | 0x4;
			memcpy(output + *written + 1, message + offset, 3);
			*written += 4;
			offset += 3;
		}
		size_t tail = messageSize - offset;
		if (capacity - *written < 4)
			return B_BUFFER_OVERFLOW;
		cin = tail == 1 ? 0x5 : tail == 2 ? 0x6 : 0x7;
		output[*written] = (cable << 4) | cin;
		memcpy(output + *written + 1, message + offset, tail);
		for (size_t i = tail; i < 3; i++)
			output[*written + 1 + i] = 0;
		*written += 4;
		return B_OK;
	} else {
		// EOX is a valid one-byte USB-MIDI CIN 5 event even when used as a
		// standalone message rather than as the final byte of a SysEx buffer.
		uint8 expected = status == 0xf7 ? 1
			: Midi1Codec::SystemMessageLength(status);
		if (expected == 0 || messageSize != expected)
			return B_BAD_DATA;
		for (size_t i = 1; i < messageSize; i++) {
			if (message[i] > 0x7f)
				return B_BAD_DATA;
		}
		if (status == 0xf1 || status == 0xf3)
			cin = 0x2;
		else if (status == 0xf2)
			cin = 0x3;
		else if (status == 0xf6 || status == 0xf7)
			cin = 0x5;
		else
			cin = 0xf;
		consumed = messageSize;
	}

	if (capacity < 4)
		return B_BUFFER_OVERFLOW;
	output[0] = (cable << 4) | cin;
	for (size_t i = 0; i < 3; i++)
		output[i + 1] = i < consumed ? message[i] : 0;
	*written = 4;
	return B_OK;
}

} // namespace BPrivate
