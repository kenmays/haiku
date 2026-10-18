/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <Errors.h>
#include <midi2/Midi1Codec.h>

#include <stdio.h>
#include <string.h>

using namespace BPrivate;

struct TestListener : Midi1Receiver::Listener {
	midi1_message messages[32];
	int32 messageCount;
	int32 errorCount;
	int32 sysexStarts;
	int32 sysexEnds;
	int32 sysexBytes;
	uint8 sysex[32];

	TestListener()
		: messageCount(0), errorCount(0), sysexStarts(0), sysexEnds(0),
		  sysexBytes(0)
	{
		memset(messages, 0, sizeof(messages));
		memset(sysex, 0, sizeof(sysex));
	}

	void MessageReceived(const midi1_message& message)
	{
		if (messageCount < 32)
			messages[messageCount++] = message;
	}

	void SysExStarted() { sysexStarts++; }

	void SysExData(const uint8* data, size_t size)
	{
		for (size_t i = 0; i < size; i++) {
			if (sysexBytes < (int32)sizeof(sysex))
				sysex[sysexBytes++] = data[i];
		}
	}

	void SysExEnded() { sysexEnds++; }
	void ParseError(status_t) { errorCount++; }
};

static int
Fail(const char* test, const char* reason)
{
	fprintf(stderr, "FAIL: %s: %s\n", test, reason);
	return 1;
}

#define CHECK(test, condition, reason) \
	do { if (!(condition)) return Fail(test, reason); } while (0)

static int
TestChannelMessages()
{
	const char* name = "channel message lengths";
	CHECK(name, Midi1Codec::ChannelMessageLength(0x90) == 3, "Note On length");
	CHECK(name, Midi1Codec::ChannelMessageLength(0xc0) == 2, "Program Change length");
	CHECK(name, Midi1Codec::ChannelMessageLength(0xd0) == 2, "Channel Pressure length");
	CHECK(name, Midi1Codec::ChannelMessageLength(0xf0) == 0, "reject system status");
	return 0;
}

static int
TestSplitAndRunningStatus()
{
	const char* name = "split input and running status";
	TestListener listener;
	Midi1Receiver receiver(&listener);
	const uint8 first[] = {0x90, 60};
	const uint8 second[] = {100, 61, 0};
	CHECK(name, receiver.Feed(first, sizeof(first)) == B_OK, "first feed");
	CHECK(name, listener.messageCount == 0, "must wait for complete message");
	CHECK(name, receiver.Feed(second, sizeof(second)) == B_OK, "second feed");
	CHECK(name, listener.messageCount == 2, "two running-status messages");
	CHECK(name, listener.messages[0].status == 0x90
		&& listener.messages[0].data1 == 60
		&& listener.messages[0].data2 == 100
		&& listener.messages[0].size == 3, "first Note On");
	CHECK(name, listener.messages[1].status == 0x90
		&& listener.messages[1].data1 == 61
		&& listener.messages[1].data2 == 0, "second Note On");
	return 0;
}

static int
TestSystemCommonLengths()
{
	const char* name = "System Common lengths";
	TestListener listener;
	Midi1Receiver receiver(&listener);
	const uint8 bytes[] = {0xf1, 0x01, 0xf2, 0x02, 0x03, 0xf3, 0x04, 0xf6};
	receiver.Feed(bytes, sizeof(bytes));
	CHECK(name, listener.messageCount == 4, "four System Common messages");
	CHECK(name, listener.messages[0].status == 0xf1
		&& listener.messages[0].size == 2 && listener.messages[0].data1 == 1,
		"Quarter Frame");
	CHECK(name, listener.messages[1].status == 0xf2
		&& listener.messages[1].size == 3 && listener.messages[1].data2 == 3,
		"Song Position");
	CHECK(name, listener.messages[2].status == 0xf3
		&& listener.messages[2].size == 2, "Song Select");
	CHECK(name, listener.messages[3].status == 0xf6
		&& listener.messages[3].size == 1, "Tune Request");
	return 0;
}

static int
TestSystemCommonCancelsRunningStatus()
{
	const char* name = "System Common cancels running status";
	TestListener listener;
	Midi1Receiver receiver(&listener);
	const uint8 bytes[] = {0x90, 60, 100, 0xf6, 61, 0};
	receiver.Feed(bytes, sizeof(bytes));
	CHECK(name, listener.messageCount == 2, "Note On and Tune Request only");
	CHECK(name, listener.messages[0].status == 0x90
		&& listener.messages[1].status == 0xf6, "status sequence");
	CHECK(name, listener.errorCount == 2, "orphan data bytes rejected");
	return 0;
}

static int
TestRealtimeInterleaving()
{
	const char* name = "Real-Time interleaving";
	TestListener listener;
	Midi1Receiver receiver(&listener);
	const uint8 bytes[] = {0x90, 60, 0xf8, 100, 61, 0xfa, 0};
	receiver.Feed(bytes, sizeof(bytes));
	CHECK(name, listener.messageCount == 4, "two notes and two realtime messages");
	CHECK(name, listener.messages[0].status == 0xf8, "clock dispatched immediately");
	CHECK(name, listener.messages[1].status == 0x90
		&& listener.messages[1].data2 == 100, "first note preserved");
	CHECK(name, listener.messages[2].status == 0xfa, "Start dispatched");
	CHECK(name, listener.messages[3].status == 0x90
		&& listener.messages[3].data1 == 61, "running status preserved");
	return 0;
}

static int
TestSysEx()
{
	const char* name = "SysEx streaming";
	TestListener listener;
	Midi1Receiver receiver(&listener);
	const uint8 bytes[] = {0xf0, 0x01, 0xf8, 0x02, 0xf7};
	receiver.Feed(bytes, sizeof(bytes));
	CHECK(name, listener.sysexStarts == 1, "start callback");
	CHECK(name, listener.sysexEnds == 1, "end callback");
	CHECK(name, listener.sysexBytes == 2
		&& listener.sysex[0] == 1 && listener.sysex[1] == 2,
		"data bytes excluding realtime");
	CHECK(name, listener.messageCount == 1
		&& listener.messages[0].status == 0xf8, "realtime inside SysEx");
	return 0;
}

static int
TestMalformedAndReset()
{
	const char* name = "malformed data and reset";
	TestListener listener;
	Midi1Receiver receiver(&listener);
	const uint8 bytes[] = {0x01, 0xf9, 0xfd, 0xf7};
	receiver.Feed(bytes, sizeof(bytes));
	CHECK(name, listener.errorCount == 4, "invalid bytes/statuses reported");
	receiver.Reset();
	const uint8 valid[] = {0xc0, 0x07};
	receiver.Feed(valid, sizeof(valid));
	CHECK(name, listener.messageCount == 1
		&& listener.messages[0].status == 0xc0
		&& listener.messages[0].data1 == 7, "parser recovers after reset");
	return 0;
}

static int
TestEncoder()
{
	const char* name = "channel encoder";
	uint8 bytes[3] = {0, 0, 0};
	size_t written = 99;
	CHECK(name, Midi1Codec::EncodeChannelMessage(0xc2, 5, 0, bytes,
		sizeof(bytes), &written) == B_OK, "encode Program Change");
	CHECK(name, written == 2 && bytes[0] == 0xc2 && bytes[1] == 5,
		"Program Change bytes");
	CHECK(name, Midi1Codec::EncodeChannelMessage(0x90, 128, 0, bytes,
		sizeof(bytes), &written) == B_BAD_VALUE, "reject 8-bit data byte");
	CHECK(name, Midi1Codec::EncodeChannelMessage(0xf8, 0, 0, bytes,
		sizeof(bytes), &written) == B_BAD_VALUE, "reject system status");
	return 0;
}

int
main()
{
	int failures = 0;
	failures += TestChannelMessages();
	failures += TestSplitAndRunningStatus();
	failures += TestSystemCommonLengths();
	failures += TestSystemCommonCancelsRunningStatus();
	failures += TestRealtimeInterleaving();
	failures += TestSysEx();
	failures += TestMalformedAndReset();
	failures += TestEncoder();
	if (failures == 0)
		printf("MIDI 1.0 codec tests passed.\n");
	return failures == 0 ? 0 : 1;
}
