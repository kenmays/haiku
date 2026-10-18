/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MIDI2_MIDI1_CODEC_H
#define _MIDI2_MIDI1_CODEC_H
#include <SupportDefs.h>
#include <stddef.h>

namespace BPrivate {
struct midi1_message {
	uint8 status;
	uint8 data1;
	uint8 data2;
	uint8 size;
};

class Midi1Receiver {
public:
	class Listener {
	public:
		virtual ~Listener() {}
		virtual void MessageReceived(const midi1_message& message) = 0;
		virtual void SysExStarted() {}
		virtual void SysExData(const uint8* data, size_t size) = 0;
		virtual void SysExEnded() {}
		virtual void ParseError(status_t error) {}
	};
	explicit Midi1Receiver(Listener* listener);
	void Reset();
	status_t Feed(const uint8* bytes, size_t size);
private:
	void _Dispatch(uint8 status, uint8 data1, uint8 data2, uint8 length);
	void _HandleByte(uint8 byte);
	void _HandleStatus(uint8 status);
	Listener* fListener;
	uint8 fRunningStatus, fPendingStatus, fData[2], fDataCount, fRequiredData;
	bool fInSysEx;
};

class Midi1Codec {
public:
	static status_t EncodeChannelMessage(uint8 status, uint8 data1,
		uint8 data2, uint8* output, size_t capacity, size_t* written);
	static uint8 ChannelMessageLength(uint8 status);
	static uint8 SystemMessageLength(uint8 status);
};
}
#endif
