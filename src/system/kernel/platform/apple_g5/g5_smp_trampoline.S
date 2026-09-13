/*
 * PowerMac G5 Open Firmware secondary CPU trampoline.
 *
 * Open Firmware starts the CPU in real mode with the release-record physical
 * address in r3. The record layout is:
 *   +0  entry
 *   +8  stack
 *   +16 ready
 *   +24 go
 *
 * The record and all addresses consumed here must be physical addresses.
 */

.text
.align 5
.globl apple_g5_secondary_trampoline
.type apple_g5_secondary_trampoline,@function
apple_g5_secondary_trampoline:
	/* r3 = physical release record */
	ld	1,8(3)
	ld	12,0(3)

	/* Publish that the CPU reached the trampoline. */
	li	0,1
	std	0,16(3)
	sync

	/* Wait for the primary CPU to release us. */
1:
	ld	0,24(3)
	cmpdi	0,0
	beq	1b
	isync

	/* Keep the record pointer as the bootstrap argument in r3. */
	mtctr	12
	bctrl

2:
	b	2b
.size apple_g5_secondary_trampoline,.-apple_g5_secondary_trampoline
