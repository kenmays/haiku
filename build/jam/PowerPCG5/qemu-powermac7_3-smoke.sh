#!/bin/sh
# PowerMac7,3 PPC64 smoke-test harness.
#
# Upstream QEMU currently documents g3beige/mac99 PowerMac machines; a
# powermac7_3 machine existed in the PowerMac machine-type development work
# but is not present in current upstream releases. Therefore this script
# deliberately requires a QEMU build which advertises powermac7_3 instead of
# silently falling back to mac99 (which would test the wrong hardware).

set -eu

QEMU=${QEMU:-qemu-system-ppc64}
KERNEL=${1:-}
MACHINE=${QEMU_MACHINE:-powermac7_3}
MEMORY=${QEMU_MEMORY:-2G}
SMP=${QEMU_SMP:-2}

if ! command -v "$QEMU" >/dev/null 2>&1; then
	echo "error: $QEMU was not found" >&2
	exit 127
fi

if ! "$QEMU" -machine help 2>&1 | grep -q "^$MACHINE"; then
	echo "error: $QEMU does not provide machine '$MACHINE'" >&2
	echo "available PowerMac machines:" >&2
	"$QEMU" -machine help 2>&1 | grep -E '(^|,)mac99|powermac|powerbook|g3beige' || true
	exit 2
fi

if [ -z "$KERNEL" ]; then
	echo "usage: $0 /path/to/haiku-ppc64-kernel" >&2
	exit 2
fi

if [ ! -f "$KERNEL" ]; then
	echo "error: kernel '$KERNEL' does not exist" >&2
	exit 2
fi

exec "$QEMU" \
	-machine "$MACHINE" \
	-cpu 970fx \
	-m "$MEMORY" \
	-smp "$SMP" \
	-serial stdio \
	-display none \
	-kernel "$KERNEL" \
	-no-reboot \
	-d guest_errors,cpu_reset \
	-D qemu-powermac7_3.log
