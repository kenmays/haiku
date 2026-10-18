MIDI 1.0 byte-stream API for libmidi2
=====================================

Midi1Receiver incrementally parses MIDI 1.0 bytes independently of transport.
Include <midi2/Midi1Codec.h>; feed UART/USB bytes in arrival order and route
Listener callbacks to libmidi2 endpoints/consumers.

Implemented protocol behavior:
* Correct channel message lengths and channel running status.
* System Common cancels running status.
* System Real-Time messages are dispatched immediately and do not disturb
  running status or SysEx.
* SysEx uses start/data/end callbacks and does not buffer the entire payload.
* Reserved status bytes and malformed data report B_BAD_DATA.
* EncodeChannelMessage validates channel status, data values and capacity.

This codec is a protocol component, not an end-to-end certification claim.
Before claiming complete MIDI 1.0 compliance, add and run conformance tests
covering every status, split input chunks, running status, real-time
interleaving, SysEx termination/errors, and transport integration.
