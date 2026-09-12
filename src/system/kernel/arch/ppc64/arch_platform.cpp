/*
 * PPC64 Power Mac uses the same Open Firmware platform service layer as
 * classic PowerPC. The architecture-specific ABI is isolated in this
 * directory; platform device calls remain shared with the existing OF code.
 */
#include "../ppc/arch_platform.cpp"
