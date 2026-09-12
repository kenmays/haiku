/* ELF64 PowerPC init/fini glue. */
#define FUNCTION(x) .global x; .type x,@function; x
.section .init
FUNCTION(_init):
.section .fini
FUNCTION(_fini):
