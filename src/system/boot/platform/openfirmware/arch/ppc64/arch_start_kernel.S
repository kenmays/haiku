/* PPC64 kernel handoff from the Open Firmware loader. */
#define FUNCTION(x) .global x; .type x,@function; x
.text

/*
 * r3 = kernel_args
 * r4 = kernel entry
 * r5 = kernel stack top
 */
FUNCTION(arch_start_kernel):
	stdu	%r1, -64(%r1)
	mflr	%r0
	std	%r0, 72(%r1)
	std	%r29, 40(%r1)
	mr	%r29, %r1

	/* Establish a 16-byte aligned PPC64 ABI stack. */
	subi	%r1, %r5, 32
	li	%r0, 0
	std	%r0, 0(%r1)

	mtlr	%r4
	li	%r4, 0
	blrl

	mr	%r1, %r29
	ld	%r29, 40(%r1)
	ld	%r0, 72(%r1)
	mtlr	%r0
	addi	%r1, %r1, 64
	blr
