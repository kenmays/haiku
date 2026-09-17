# Legacy I/O drivers

This branch collects legacy peripheral support while keeping hardware-specific
bus backends separate from generic device protocols.

## Parallel ports

`ports/parallel` provides a conservative legacy ISA SPP/LPT interface for the
standard LPT1/LPT2/LPT3 addresses. It exposes data, status and control access
through the Haiku device-driver hooks. It deliberately does not probe by
writing to unknown I/O addresses.

The driver is intentionally an SPP foundation. EPP/ECP negotiation, DMA and
interrupt-driven printer protocols require additional hardware-specific work.
PCI parallel controllers should be handled through the PCI bus manager rather
than assuming ISA I/O.

## Serial

Haiku already provides `ports/pc_serial` for PC-compatible UARTs and
`ports/usb_serial` for USB serial adapters. The existing PC serial layer
supports ISA, PCI and PCMCIA attachment points; this branch does not duplicate
that architecture.

## Keyboard and mouse

The existing input stack contains `ps2_hid` for PS/2 devices and `usb_hid`
for USB HID devices, including keyboards, mice and game controllers. Legacy
PowerPC machines should use a platform-specific transport when their firmware
or ASICs expose a non-PS/2 interface.

## Joystick / gameport

The existing generic gameport module provides the analog gameport interface,
while `emuxkigameport` attaches Creative EMU10K-family PCI gameports to it.
The EMU10K path has been hardened so PCI I/O decoding is enabled without
clobbering unrelated PCI command bits, and the gameport write path no longer
performs an unrelated PCI write.

A separate ISA gameport backend can be added where an ISA bus manager exposes
port 0x201. PCI devices should continue to use PCI BAR information.

## PCMCIA / CardBus

The existing `bus/pcmcia` layer supplies Card Services/device-services logic.
PCMCIA is not a single universal peripheral driver: socket-controller support
and device-specific drivers are required for real hardware. CardBus bridges
should be discovered through PCI and delegated to controller-specific socket
services.

## PowerPC / Power Mac

Do not assume x86 ISA port I/O exists on PowerPC. PCI peripherals can use the
PowerPC PCI bus manager when supported. Apple-specific serial, keyboard,
mouse, parallel or gameport hardware must be attached through the appropriate
UniNorth/K2/U3 or other platform controller rather than by casting an x86 I/O
port into a PowerPC address.

## Status

Implemented in this branch:

- legacy ISA SPP parallel-port driver foundation
- ports Jamfile integration
- EMU10K PCI gameport setup fixes
- documentation of existing serial/input/PCMCIA layers

Still hardware-dependent:

- EPP/ECP and IEEE-1284 parallel protocols
- parallel IRQ/DMA support
- ISA gameport backend
- controller-specific PCMCIA socket services
- Apple PowerPC legacy-I/O backends
- physical-hardware validation
