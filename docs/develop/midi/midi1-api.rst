MIDI 1.0 protocol and transport API
====================================

Normative references
--------------------

The protocol review uses the MIDI Association's MIDI 1.0 Detailed
Specification (version 4.2.1 / 96-1-4) and the MIDI 1.0 Addenda:

* https://midi.org/midi-1-0-detailed-specification
* https://midi.org/midi-1-0-addenda
* https://midi.org/specs

Protocol parser
---------------

Include <midi2/Midi1Codec.h>. Midi1Receiver incrementally parses a byte stream
and emits Listener callbacks. It supports channel voice messages, channel
running status, System Common messages, Real-Time messages, and streaming
SysEx callbacks.

Channel Voice message lengths include the status byte: Program Change and
Channel Pressure are two bytes; the other Channel Voice message families are
three bytes. System Common cancels channel running status. F1 and F3 have one
data byte; F2 has two data bytes; F6 is a single-byte message. Real-Time
messages may be interleaved between bytes without changing the in-progress
message or running status. SysEx begins with F0 and ends with F7.

Transport boundary
------------------

Include <midi2/Midi1Transport.h> to implement a Midi1ByteTransport backend and
use Midi1TransportWriter for complete Channel Voice messages or SendRawBytes()
for System Common, Real-Time, and chunked SysEx byte sequences. The transport
implementation owns device discovery/lifecycle, write scheduling, and
transport-specific framing. Midi1Receiver::Feed() accepts reconstructed MIDI
bytes from the receive side.

A DIN/UART backend carries MIDI bytes over the serial link and is responsible
for the electrical interface and serial configuration. USB-MIDI 1.0 uses four-byte event packets carrying a cable number, Code Index
Number (CIN), and up to three MIDI bytes. Midi1USBPacketCodec encodes one
complete MIDI message into event packets and decodes individual event packets
back into MIDI bytes. USB-MIDI does not use MIDI running status on the USB
packet stream. The codec does not perform USB device discovery, endpoint I/O,
transfer scheduling, or cable lifecycle management; those belong to a USB
backend. Bluetooth MIDI has its own packetization and timing rules and needs
a distinct backend.

Scope and validation status
---------------------------

The codec and writer are protocol/adapter components. This branch does not yet
provide physical DIN, USB-MIDI, or Bluetooth device drivers, nor does it claim
end-to-end transport compliance. The code has not been built or run against a
conformance suite. Before claiming complete compliance, add automated tests
for every status family, System Common lengths, running status transitions,
Real-Time interleaving, malformed/reserved statuses, split input chunks,
SysEx termination, partial writes, and each concrete transport backend.
