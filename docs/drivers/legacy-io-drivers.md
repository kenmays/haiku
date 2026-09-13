# Legacy I/O driver suite

This branch assembles the legacy input/port stack requested for Haiku.

## Components

- `ports/parallel`: new SPP-compatible ISA parallel-port driver. It exposes the
  standard data/status/control registers and a small ioctl ABI, without probing
  by destructive writes.
- `ports/pc_serial`: existing 16450/16550-family PC serial driver. It supports
  the standard ISA COM ports plus supported PCI serial controllers.
- `ports/usb_serial`: existing USB serial framework.
- `joystick`: existing generic gameport path, including the EMU10K1/EMU10K2
  gameport adapter.
- `input/ps2_hid`: existing PS/2 keyboard and mouse driver.
- `input/usb_hid`: existing USB keyboard, mouse, joystick/gamepad and HID input
  driver.
- `bus/pcmcia`: existing PCMCIA/Card Services device-services driver. A complete
  controller backend remains hardware-specific; this layer is retained as the
  Card Services interface rather than pretending every controller is identical.

## Important architecture note

Keyboard, mouse, joystick, serial, parallel and PCMCIA are not one common
hardware class. Haiku therefore keeps them in their native driver layers:
input devices use the input/PS2/USB stacks, serial and parallel ports use the
ports stack, and PCMCIA remains a bus/Card Services layer.

The next hardware-specific work for PowerPC machines is to add the platform
backends (for example, Apple KeyLargo/UniNorth-style I/O) instead of applying
x86 ISA port assumptions to Power Mac hardware.
