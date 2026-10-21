/*
 * Copyright 2006-2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "driver.h"
#include "device.h"
#include "lock.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <AGP.h>
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <SupportDefs.h>


#define TRACE_DRIVER
#ifdef TRACE_DRIVER
#	define TRACE(x...) dprintf("intel_extreme: " x)
#else
#	define TRACE(x) ;
#endif

#define ERROR(x...) dprintf("intel_extreme: " x)
#define CALLED(x...) TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define MAX_CARDS 4


// list of supported devices
const struct supported_device {
	uint32		device_id;
	int32		type;
	const char*	name;
} kSupportedDevices[] = {
	{0x3577, INTEL_GROUP_83x, "i830GM"},
	{0x2562, INTEL_GROUP_83x, "i845G"},

	{0x2572, INTEL_GROUP_85x, "i865G"},
	{0x3582, INTEL_GROUP_85x, "i855G"},
	{0x358e, INTEL_GROUP_85x, "i855G"},

	{0x2582, INTEL_MODEL_915, "i915G"},
	{0x258a, INTEL_MODEL_915, "i915"},
	{0x2592, INTEL_MODEL_915M, "i915GM"},
	{0x2792, INTEL_MODEL_915, "i910"},
	{0x2772, INTEL_MODEL_945, "i945G"},
	{0x27a2, INTEL_MODEL_945M, "i945GM"},
	{0x27ae, INTEL_MODEL_945M, "i945GME"},
	{0x2972, INTEL_MODEL_965, "i946G"},
	{0x2982, INTEL_MODEL_965, "G35"},
	{0x2992, INTEL_MODEL_965, "i965Q"},
	{0x29a2, INTEL_MODEL_965, "i965G"},
	{0x2a02, INTEL_MODEL_965M, "i965GM"},
	{0x2a12, INTEL_MODEL_965M, "i965GME"},
	{0x29b2, INTEL_MODEL_G33, "G33G"},
	{0x29c2, INTEL_MODEL_G33, "Q35G"},
	{0x29d2, INTEL_MODEL_G33, "Q33G"},

	{0x2a42, INTEL_MODEL_GM45, "GM45"},
	{0x2e02, INTEL_MODEL_G45, "IGD"},
	{0x2e12, INTEL_MODEL_G45, "Q45"},
	{0x2e22, INTEL_MODEL_G45, "G45"},
	{0x2e32, INTEL_MODEL_G45, "G41"},
	{0x2e42, INTEL_MODEL_G45, "B43"},
	{0x2e92, INTEL_MODEL_G45, "B43"},

	{0xa001, INTEL_MODEL_PINE, "Atom D4xx"},
	{0xa002, INTEL_MODEL_PINE, "Atom D5xx"},
	{0xa011, INTEL_MODEL_PINEM, "Atom N4xx"},
	{0xa012, INTEL_MODEL_PINEM, "Atom N5xx"},

	{0x0042, INTEL_MODEL_ILKG, "IronLake Desktop"},
	{0x0046, INTEL_MODEL_ILKGM, "IronLake Mobile"},

	{0x0102, INTEL_MODEL_SNBG, "SandyBridge Desktop GT1"},
	{0x0112, INTEL_MODEL_SNBG, "SandyBridge Desktop GT2"},
	{0x0122, INTEL_MODEL_SNBG, "SandyBridge Desktop GT2+"},
	{0x0106, INTEL_MODEL_SNBGM, "SandyBridge Mobile GT1"},
	{0x0116, INTEL_MODEL_SNBGM, "SandyBridge Mobile GT2"},
	{0x0126, INTEL_MODEL_SNBGM, "SandyBridge Mobile GT2+"},
	{0x010a, INTEL_MODEL_SNBGS, "SandyBridge Server"},

	{0x0152, INTEL_MODEL_IVBG, "IvyBridge Desktop GT1"},
	{0x0162, INTEL_MODEL_IVBG, "IvyBridge Desktop GT2"},
	{0x0156, INTEL_MODEL_IVBGM, "IvyBridge Mobile GT1"},
	{0x0166, INTEL_MODEL_IVBGM, "IvyBridge Mobile GT2"},
	{0x015a, INTEL_MODEL_IVBGS, "IvyBridge Server GT1"},
	{0x016a, INTEL_MODEL_IVBGS, "IvyBridge Server GT2"},

	{0x0a06, INTEL_MODEL_HASM, "Haswell ULT GT1 Mobile"},
	{0x0412, INTEL_MODEL_HAS, "Haswell GT2 Desktop"},
	{0x0416, INTEL_MODEL_HASM, "Haswell GT2 Mobile"},
	{0x0a16, INTEL_MODEL_HASM, "Haswell ULT GT2 Mobile"},
	{0x0a2e, INTEL_MODEL_HASM, "Haswell ULT GT3 Mobile"},
	{0x0d26, INTEL_MODEL_HASM, "Haswell CRW GT3 Mobile"},
	// Additional Haswell IDs present in upstream Linux i915.
	{0x0a02, INTEL_MODEL_HAS,  "Haswell ULT GT1 Desktop"},
	{0x0a0b, INTEL_MODEL_HAS,  "Haswell ULT GT1 Reserved"},
	{0x0a0a, INTEL_MODEL_HAS,  "Haswell ULT GT1 Server"},
	{0x0a0e, INTEL_MODEL_HASM, "Haswell ULX GT1 Mobile"},
	{0x0402, INTEL_MODEL_HAS,  "Haswell GT1 Desktop"},
	{0x0406, INTEL_MODEL_HASM, "Haswell GT1 Mobile"},
	{0x040a, INTEL_MODEL_HAS,  "Haswell GT1 Server"},
	{0x040b, INTEL_MODEL_HAS,  "Haswell GT1 Reserved"},
	{0x040e, INTEL_MODEL_HASM, "Haswell GT1 Reserved Mobile"},
	{0x0c02, INTEL_MODEL_HAS,  "Haswell SDV GT1 Desktop"},
	{0x0c06, INTEL_MODEL_HASM, "Haswell SDV GT1 Mobile"},
	{0x0c0a, INTEL_MODEL_HAS,  "Haswell SDV GT1 Server"},
	{0x0c0b, INTEL_MODEL_HAS,  "Haswell SDV GT1 Reserved"},
	{0x0c0e, INTEL_MODEL_HASM, "Haswell SDV GT1 Reserved Mobile"},
	{0x0d02, INTEL_MODEL_HAS,  "Haswell CRW GT1 Desktop"},
	{0x0d06, INTEL_MODEL_HASM, "Haswell CRW GT1 Mobile"},
	{0x0d0a, INTEL_MODEL_HAS,  "Haswell CRW GT1 Server"},
	{0x0d0b, INTEL_MODEL_HAS,  "Haswell CRW GT1 Reserved"},
	{0x0d0e, INTEL_MODEL_HASM, "Haswell CRW GT1 Reserved Mobile"},
	{0x0a12, INTEL_MODEL_HAS,  "Haswell ULT GT2 Desktop"},
	{0x0a1a, INTEL_MODEL_HAS,  "Haswell ULT GT2 Server"},
	{0x0a1b, INTEL_MODEL_HAS,  "Haswell ULT GT2 Reserved"},
	{0x0a1e, INTEL_MODEL_HASM, "Haswell ULX GT2 Mobile"},
	{0x041a, INTEL_MODEL_HAS,  "Haswell GT2 Server"},
	{0x041b, INTEL_MODEL_HAS,  "Haswell GT2 Reserved"},
	{0x041e, INTEL_MODEL_HASM, "Haswell GT2 Reserved Mobile"},
	{0x0c12, INTEL_MODEL_HAS,  "Haswell SDV GT2 Desktop"},
	{0x0c16, INTEL_MODEL_HASM, "Haswell SDV GT2 Mobile"},
	{0x0c1a, INTEL_MODEL_HAS,  "Haswell SDV GT2 Server"},
	{0x0c1b, INTEL_MODEL_HAS,  "Haswell SDV GT2 Reserved"},
	{0x0c1e, INTEL_MODEL_HASM, "Haswell SDV GT2 Reserved Mobile"},
	{0x0d12, INTEL_MODEL_HAS,  "Haswell CRW GT2 Desktop"},
	{0x0d16, INTEL_MODEL_HASM, "Haswell CRW GT2 Mobile"},
	{0x0d1a, INTEL_MODEL_HAS,  "Haswell CRW GT2 Server"},
	{0x0d1b, INTEL_MODEL_HAS,  "Haswell CRW GT2 Reserved"},
	{0x0d1e, INTEL_MODEL_HASM, "Haswell CRW GT2 Reserved Mobile"},
	{0x0a22, INTEL_MODEL_HAS,  "Haswell ULT GT3 Desktop"},
	{0x0a26, INTEL_MODEL_HASM, "Haswell ULT GT3 Mobile"},
	{0x0a2a, INTEL_MODEL_HAS,  "Haswell ULT GT3 Server"},
	{0x0a2b, INTEL_MODEL_HAS,  "Haswell ULT GT3 Reserved"},
	{0x0422, INTEL_MODEL_HAS,  "Haswell GT3 Desktop"},
	{0x0426, INTEL_MODEL_HASM, "Haswell GT3 Mobile"},
	{0x042a, INTEL_MODEL_HAS,  "Haswell GT3 Server"},
	{0x042b, INTEL_MODEL_HAS,  "Haswell GT3 Reserved"},
	{0x042e, INTEL_MODEL_HASM, "Haswell GT3 Reserved Mobile"},
	{0x0c22, INTEL_MODEL_HAS,  "Haswell SDV GT3 Desktop"},
	{0x0c26, INTEL_MODEL_HASM, "Haswell SDV GT3 Mobile"},
	{0x0c2a, INTEL_MODEL_HAS,  "Haswell SDV GT3 Server"},
	{0x0c2b, INTEL_MODEL_HAS,  "Haswell SDV GT3 Reserved"},
	{0x0c2e, INTEL_MODEL_HASM, "Haswell SDV GT3 Reserved Mobile"},
	{0x0d22, INTEL_MODEL_HAS,  "Haswell CRW GT3 Desktop"},
	{0x0d2a, INTEL_MODEL_HAS,  "Haswell CRW GT3 Server"},
	{0x0d2b, INTEL_MODEL_HAS,  "Haswell CRW GT3 Reserved"},
	{0x0d2e, INTEL_MODEL_HASM, "Haswell CRW GT3 Reserved Mobile"},

	{0x0f30, INTEL_MODEL_VLVM, "ValleyView Mobile"},
	{0x0f31, INTEL_MODEL_VLVM, "ValleyView Mobile"},
	{0x0f32, INTEL_MODEL_VLVM, "ValleyView Mobile"},
	{0x0f33, INTEL_MODEL_VLVM, "ValleyView Mobile"},

	{0x22b0, INTEL_MODEL_CHVM, "Cherryview"},
	{0x22b1, INTEL_MODEL_CHVM, "Braswell GT1"},
	{0x22b2, INTEL_MODEL_CHVM, "Cherryview"},
	{0x22b3, INTEL_MODEL_CHVM, "Cherryview"},


	{0x1606, INTEL_MODEL_BDWM, "Broadwell GT1 ULT"},
	{0x160b, INTEL_MODEL_BDWM, "Broadwell GT1 Iris"},
	{0x160e, INTEL_MODEL_BDWM, "Broadwell GT1 ULX"},
	{0x1602, INTEL_MODEL_BDWM, "Broadwell GT1 ULT"},
	{0x160a, INTEL_MODEL_BDWS, "Broadwell GT1 Server"},
	{0x160d, INTEL_MODEL_BDW,  "Broadwell GT1 Workstation"},
	{0x1616, INTEL_MODEL_BDWM, "Broadwell GT2 ULT"},
	{0x161b, INTEL_MODEL_BDWM, "Broadwell GT2 ULT"},
	{0x161e, INTEL_MODEL_BDWM, "Broadwell GT2 ULX"},
	{0x1612, INTEL_MODEL_BDWM, "Broadwell GT2 Halo"},
	{0x161a, INTEL_MODEL_BDWS, "Broadwell GT2 Server"},
	{0x161d, INTEL_MODEL_BDW,  "Broadwell GT2 Workstation"},
	{0x1626, INTEL_MODEL_BDWM, "Broadwell GT3 ULT"},
	{0x162b, INTEL_MODEL_BDWM, "Broadwell GT3 Iris"},
	{0x162e, INTEL_MODEL_BDWM, "Broadwell GT3 ULX"},
	{0x1622, INTEL_MODEL_BDWM, "Broadwell GT3 ULT"},
	{0x162a, INTEL_MODEL_BDWS, "Broadwell GT3 Server"},
	{0x162d, INTEL_MODEL_BDW,  "Broadwell GT3 Workstation"},
	// Reserved Broadwell IDs also listed by upstream Linux i915.
	{0x1636, INTEL_MODEL_BDWM, "Broadwell Reserved 0x1636"},
	{0x163b, INTEL_MODEL_BDWM, "Broadwell Reserved 0x163b"},
	{0x163e, INTEL_MODEL_BDWM, "Broadwell Reserved 0x163e"},
	{0x1632, INTEL_MODEL_BDWM, "Broadwell Reserved 0x1632"},
	{0x163a, INTEL_MODEL_BDWS, "Broadwell Reserved 0x163a"},
	{0x163d, INTEL_MODEL_BDW,  "Broadwell Reserved 0x163d"},

	{0x1902, INTEL_MODEL_SKY,  "Skylake GT1"},
	{0x1906, INTEL_MODEL_SKYM, "Skylake GT1"},
	{0x190a, INTEL_MODEL_SKYS, "Skylake GT1"},
	{0x190b, INTEL_MODEL_SKY,  "Skylake GT1"},
	{0x190e, INTEL_MODEL_SKYM, "Skylake GT1"},
	{0x1912, INTEL_MODEL_SKY,  "Skylake GT2"}, //confirmed OK
	{0x1916, INTEL_MODEL_SKYM, "Skylake GT2"}, //confirmed native mode panel OK
	{0x191a, INTEL_MODEL_SKYS, "Skylake GT2"},
	{0x191b, INTEL_MODEL_SKY,  "Skylake GT2"},
	{0x191d, INTEL_MODEL_SKY,  "Skylake GT2"},
	{0x191e, INTEL_MODEL_SKYM, "Skylake GT2"},
	{0x1921, INTEL_MODEL_SKYM, "Skylake GT2F"},
	{0x1926, INTEL_MODEL_SKYM, "Skylake GT3"},
	{0x192a, INTEL_MODEL_SKYS, "Skylake GT3"},
	{0x192b, INTEL_MODEL_SKY,  "Skylake GT3"},
	{0x1913, INTEL_MODEL_SKYM, "Skylake ULT GT1.5"},
	{0x1915, INTEL_MODEL_SKYM, "Skylake ULX GT1.5"},
	{0x1917, INTEL_MODEL_SKY,  "Skylake Desktop GT1.5"},
	{0x1923, INTEL_MODEL_SKYM, "Skylake ULT GT3"},
	{0x1927, INTEL_MODEL_SKYM, "Skylake ULT GT3e"},
	{0x192d, INTEL_MODEL_SKYS, "Skylake Server GT3e"},
	{0x1932, INTEL_MODEL_SKY,  "Skylake Desktop GT4"},
	{0x193a, INTEL_MODEL_SKYS, "Skylake Server GT4e"},
	{0x193b, INTEL_MODEL_SKY,  "Skylake Halo GT4e"},
	{0x193d, INTEL_MODEL_SKY,  "Skylake Workstation GT4e"},

	{0x5a84, INTEL_MODEL_KBYM, "Apollo Lake GT1.5"},
	{0x5a85, INTEL_MODEL_KBYM, "Apollo Lake GT1"},
	{0x0a84, INTEL_MODEL_KBYM, "Apollo Lake GT1.5"},
	{0x1a84, INTEL_MODEL_KBYM, "Apollo Lake GT1.5"},
	{0x1a85, INTEL_MODEL_KBYM, "Apollo Lake GT1"},

	{0x5906, INTEL_MODEL_KBY,  "Kabylake ULT GT1"},
	{0x5902, INTEL_MODEL_KBY,  "Kabylake DT GT1"},
	{0x5916, INTEL_MODEL_KBYM, "Kabylake ULT GT2"},
	{0x5921, INTEL_MODEL_KBYM, "Kabylake ULT GT2F"},
	{0x591c, INTEL_MODEL_KBY,  "Kabylake ULX GT2"},
	{0x591e, INTEL_MODEL_KBY,  "Kabylake ULX GT2"},
	{0x5912, INTEL_MODEL_KBY,  "Kabylake DT GT2"},
	{0x5917, INTEL_MODEL_KBYM, "Kabylake Mobile GT2"},
	{0x591b, INTEL_MODEL_KBYM, "Kabylake Halo GT2"},
	{0x591d, INTEL_MODEL_KBY,  "Kabylake WKS GT2"},
	{0x5926, INTEL_MODEL_KBY,  "Kabylake ULT GT3"},
	{0x5927, INTEL_MODEL_KBY,  "Kabylake ULT GT3"},
	{0x5908, INTEL_MODEL_KBY,  "Kabylake Halo GT1"},
	{0x590a, INTEL_MODEL_KBY,  "Kabylake Server GT1"},
	{0x590b, INTEL_MODEL_KBY,  "Kabylake Halo GT1"},
	{0x590e, INTEL_MODEL_KBYM, "Kabylake ULX GT1"},
	{0x5913, INTEL_MODEL_KBYM, "Kabylake ULT GT1.5"},
	{0x5915, INTEL_MODEL_KBYM, "Kabylake ULX GT1.5"},
	{0x591a, INTEL_MODEL_KBY,  "Kabylake Server GT2"},
	{0x5923, INTEL_MODEL_KBYM, "Kabylake ULT GT3"},
	{0x593b, INTEL_MODEL_KBY,  "Kabylake Halo GT4"},
	{0x87c0, INTEL_MODEL_KBYM, "Kabylake Y GT2"},

	{0x3185, INTEL_MODEL_KBYM, "GeminiLake GT1"},	// Same device id for desktop and mobile.
	{0x3184, INTEL_MODEL_KBYM, "GeminiLake GT1.5"},	// Same device id for desktop and mobile.

	{0x3e90, INTEL_MODEL_CFL,  "CoffeeLake GT1"},
	{0x3e93, INTEL_MODEL_CFL,  "CoffeeLake GT1"},
	{0x3e91, INTEL_MODEL_CFL,  "CoffeeLake GT2"},
	{0x3e92, INTEL_MODEL_CFL,  "CoffeeLake GT2"},
	{0x3e96, INTEL_MODEL_CFL,  "CoffeeLake GT2"},
	{0x3e98, INTEL_MODEL_CFL,  "CoffeeLake GT2"},
	{0x3e9a, INTEL_MODEL_CFL,  "CoffeeLake GT2"},
	{0x3e9b, INTEL_MODEL_CFLM, "CoffeeLake Halo GT2"},
	{0x3eab, INTEL_MODEL_CFLM, "CoffeeLake Halo GT2"},
	{0x3ea5, INTEL_MODEL_CFL,  "CoffeeLake GT3"},
	{0x3ea6, INTEL_MODEL_CFL,  "CoffeeLake GT3"},
	{0x3e99, INTEL_MODEL_CFL,  "CoffeeLake Server GT1"},
	{0x3e9c, INTEL_MODEL_CFLM, "CoffeeLake Halo GT1"},
	{0x3e94, INTEL_MODEL_CFLM, "CoffeeLake Halo GT2"},
	{0x3ea7, INTEL_MODEL_CFL,  "CoffeeLake U GT3"},
	{0x3ea8, INTEL_MODEL_CFL,  "CoffeeLake U GT3"},
	{0x3ea9, INTEL_MODEL_CFLM, "CoffeeLake U GT2"},
	{0x87ca, INTEL_MODEL_CFLM, "CoffeeLake Y GT2"},

	// Whiskey Lake (Gen9.5; shares the Coffee Lake display family)
	{0x3ea0, INTEL_MODEL_CFLM, "WhiskeyLake U GT2"},
	{0x3ea1, INTEL_MODEL_CFLM, "WhiskeyLake U GT1"},
	{0x3ea2, INTEL_MODEL_CFLM, "WhiskeyLake U GT3"},
	{0x3ea3, INTEL_MODEL_CFLM, "WhiskeyLake U GT2"},
	{0x3ea4, INTEL_MODEL_CFLM, "WhiskeyLake U GT1"},

	{0x8a56, INTEL_MODEL_CFLM, "IceLake GT1"},
	{0x8a5c, INTEL_MODEL_CFLM, "IceLake GT1.5"},
	{0x8a5a, INTEL_MODEL_CFLM, "IceLake GT1.5"},
	{0x8a51, INTEL_MODEL_CFLM, "IceLake GT2"},
	{0x8a52, INTEL_MODEL_CFLM, "IceLake GT2"},
	{0x8a53, INTEL_MODEL_CFLM, "IceLake GT2"},

	{0x9ba4, INTEL_MODEL_CML,	"CometLake GT1"},
	{0x9ba8, INTEL_MODEL_CML,	"CometLake GT1"},
	{0x9b21, INTEL_MODEL_CMLM,	"CometLake U GT1"},
	{0x9baa, INTEL_MODEL_CMLM,	"CometLake U GT1"},
	{0x9bc4, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9bc5, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9bc6, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9bc8, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9be6, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9bf6, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9b41, INTEL_MODEL_CMLM,	"CometLake U GT2"},
	{0x9bca, INTEL_MODEL_CMLM,	"CometLake U GT2"},
	{0x9bcc, INTEL_MODEL_CMLM,	"CometLake U GT2"},
	{0x9ba2, INTEL_MODEL_CML,  "CometLake GT1"},
	{0x9ba5, INTEL_MODEL_CML,  "CometLake GT1"},
	{0x9bac, INTEL_MODEL_CMLM, "CometLake U GT1"},
	{0x9bc2, INTEL_MODEL_CML,  "CometLake GT2"},

	{0x4e55, INTEL_MODEL_JSL,	"JasperLake"},
	{0x4e61, INTEL_MODEL_JSL,	"JasperLake"},
	{0x4e71, INTEL_MODEL_JSLM,	"JasperLake"},

	{0x9a49, INTEL_MODEL_TGLM,	"TigerLake"},
	{0x9a78, INTEL_MODEL_TGLM,	"TigerLake"},
	{0x9a40, INTEL_MODEL_TGLM,	"TigerLake"},
	{0x9a60, INTEL_MODEL_TGLM,	"TigerLake"},
	{0x9a68, INTEL_MODEL_TGLM,	"TigerLake"},
	{0x9a70, INTEL_MODEL_TGLM,	"TigerLake"},

	{0x46a6, INTEL_MODEL_ALDM,  "Alder Lake-P GT2"},
	{0x46d1, INTEL_MODEL_ALDM,  "Alder Lake-N"},
	{0x46a8, INTEL_MODEL_ALDM,  "Alder Lake-UP3 GT2"},



	// Remaining upstream Linux i915/xe IDs. ID import is not hardware-validation.

	// Ice Lake IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0x8a50, INTEL_MODEL_CFLM, "Ice Lake PCI 0x8a50"},
	{0x8a54, INTEL_MODEL_CFLM, "Ice Lake PCI 0x8a54"},
	{0x8a57, INTEL_MODEL_CFLM, "Ice Lake PCI 0x8a57"},
	{0x8a58, INTEL_MODEL_CFLM, "Ice Lake PCI 0x8a58"},
	{0x8a59, INTEL_MODEL_CFLM, "Ice Lake PCI 0x8a59"},
	{0x8a5b, INTEL_MODEL_CFLM, "Ice Lake PCI 0x8a5b"},
	{0x8a5d, INTEL_MODEL_CFLM, "Ice Lake PCI 0x8a5d"},
	{0x8a70, INTEL_MODEL_CFLM, "Ice Lake PCI 0x8a70"},
	{0x8a71, INTEL_MODEL_CFLM, "Ice Lake PCI 0x8a71"},

	// Elkhart Lake IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0x4541, INTEL_MODEL_JSL, "Elkhart Lake PCI 0x4541"},
	{0x4551, INTEL_MODEL_JSL, "Elkhart Lake PCI 0x4551"},
	{0x4555, INTEL_MODEL_JSL, "Elkhart Lake PCI 0x4555"},
	{0x4557, INTEL_MODEL_JSL, "Elkhart Lake PCI 0x4557"},
	{0x4570, INTEL_MODEL_JSL, "Elkhart Lake PCI 0x4570"},
	{0x4571, INTEL_MODEL_JSL, "Elkhart Lake PCI 0x4571"},

	// Jasper Lake IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0x4e51, INTEL_MODEL_JSL, "Jasper Lake PCI 0x4e51"},
	{0x4e57, INTEL_MODEL_JSL, "Jasper Lake PCI 0x4e57"},

	// Tiger Lake IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0x9a59, INTEL_MODEL_TGLM, "Tiger Lake PCI 0x9a59"},
	{0x9ac0, INTEL_MODEL_TGLM, "Tiger Lake PCI 0x9ac0"},
	{0x9ac9, INTEL_MODEL_TGLM, "Tiger Lake PCI 0x9ac9"},
	{0x9ad9, INTEL_MODEL_TGLM, "Tiger Lake PCI 0x9ad9"},
	{0x9af8, INTEL_MODEL_TGLM, "Tiger Lake PCI 0x9af8"},

	// Rocket Lake IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0x4c80, INTEL_MODEL_TGLM, "Rocket Lake PCI 0x4c80"},
	{0x4c8a, INTEL_MODEL_TGLM, "Rocket Lake PCI 0x4c8a"},
	{0x4c8b, INTEL_MODEL_TGLM, "Rocket Lake PCI 0x4c8b"},
	{0x4c8c, INTEL_MODEL_TGLM, "Rocket Lake PCI 0x4c8c"},
	{0x4c90, INTEL_MODEL_TGLM, "Rocket Lake PCI 0x4c90"},
	{0x4c9a, INTEL_MODEL_TGLM, "Rocket Lake PCI 0x4c9a"},

	// Alder Lake-S IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0x4680, INTEL_MODEL_ALDM, "Alder Lake-S PCI 0x4680"},
	{0x4682, INTEL_MODEL_ALDM, "Alder Lake-S PCI 0x4682"},
	{0x4688, INTEL_MODEL_ALDM, "Alder Lake-S PCI 0x4688"},
	{0x468a, INTEL_MODEL_ALDM, "Alder Lake-S PCI 0x468a"},
	{0x468b, INTEL_MODEL_ALDM, "Alder Lake-S PCI 0x468b"},
	{0x4690, INTEL_MODEL_ALDM, "Alder Lake-S PCI 0x4690"},
	{0x4692, INTEL_MODEL_ALDM, "Alder Lake-S PCI 0x4692"},
	{0x4693, INTEL_MODEL_ALDM, "Alder Lake-S PCI 0x4693"},

	// Alder Lake-P IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0x4626, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x4626"},
	{0x4628, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x4628"},
	{0x462a, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x462a"},
	{0x46a0, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46a0"},
	{0x46a1, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46a1"},
	{0x46a2, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46a2"},
	{0x46a3, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46a3"},
	{0x46aa, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46aa"},
	{0x46b0, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46b0"},
	{0x46b1, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46b1"},
	{0x46b2, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46b2"},
	{0x46b3, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46b3"},
	{0x46c0, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46c0"},
	{0x46c1, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46c1"},
	{0x46c2, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46c2"},
	{0x46c3, INTEL_MODEL_ALDM, "Alder Lake-P PCI 0x46c3"},

	// Alder Lake-N IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0x46d0, INTEL_MODEL_ALDM, "Alder Lake-N PCI 0x46d0"},
	{0x46d2, INTEL_MODEL_ALDM, "Alder Lake-N PCI 0x46d2"},
	{0x46d3, INTEL_MODEL_ALDM, "Alder Lake-N PCI 0x46d3"},
	{0x46d4, INTEL_MODEL_ALDM, "Alder Lake-N PCI 0x46d4"},

	// DG1 IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0x4905, INTEL_MODEL_TGLM, "DG1 PCI 0x4905"},
	{0x4906, INTEL_MODEL_TGLM, "DG1 PCI 0x4906"},
	{0x4907, INTEL_MODEL_TGLM, "DG1 PCI 0x4907"},
	{0x4908, INTEL_MODEL_TGLM, "DG1 PCI 0x4908"},
	{0x4909, INTEL_MODEL_TGLM, "DG1 PCI 0x4909"},

	// Raptor Lake-S IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0xa780, INTEL_MODEL_ALDM, "Raptor Lake-S PCI 0xa780"},
	{0xa781, INTEL_MODEL_ALDM, "Raptor Lake-S PCI 0xa781"},
	{0xa782, INTEL_MODEL_ALDM, "Raptor Lake-S PCI 0xa782"},
	{0xa783, INTEL_MODEL_ALDM, "Raptor Lake-S PCI 0xa783"},
	{0xa788, INTEL_MODEL_ALDM, "Raptor Lake-S PCI 0xa788"},
	{0xa789, INTEL_MODEL_ALDM, "Raptor Lake-S PCI 0xa789"},
	{0xa78a, INTEL_MODEL_ALDM, "Raptor Lake-S PCI 0xa78a"},
	{0xa78b, INTEL_MODEL_ALDM, "Raptor Lake-S PCI 0xa78b"},

	// Raptor Lake-U IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0xa721, INTEL_MODEL_ALDM, "Raptor Lake-U PCI 0xa721"},
	{0xa7a1, INTEL_MODEL_ALDM, "Raptor Lake-U PCI 0xa7a1"},
	{0xa7a9, INTEL_MODEL_ALDM, "Raptor Lake-U PCI 0xa7a9"},
	{0xa7ac, INTEL_MODEL_ALDM, "Raptor Lake-U PCI 0xa7ac"},
	{0xa7ad, INTEL_MODEL_ALDM, "Raptor Lake-U PCI 0xa7ad"},

	// Raptor Lake-P IDs imported from Linux i915/xe PCI table; closest existing Haiku model path.
	{0xa720, INTEL_MODEL_ALDM, "Raptor Lake-P PCI 0xa720"},
	{0xa7a0, INTEL_MODEL_ALDM, "Raptor Lake-P PCI 0xa7a0"},
	{0xa7a8, INTEL_MODEL_ALDM, "Raptor Lake-P PCI 0xa7a8"},
	{0xa7aa, INTEL_MODEL_ALDM, "Raptor Lake-P PCI 0xa7aa"},
	{0xa7ab, INTEL_MODEL_ALDM, "Raptor Lake-P PCI 0xa7ab"},

	// DG2 IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0x5690, INTEL_MODEL_TGLM, "DG2 PCI 0x5690"},
	{0x5691, INTEL_MODEL_TGLM, "DG2 PCI 0x5691"},
	{0x5692, INTEL_MODEL_TGLM, "DG2 PCI 0x5692"},
	{0x5693, INTEL_MODEL_TGLM, "DG2 PCI 0x5693"},
	{0x5694, INTEL_MODEL_TGLM, "DG2 PCI 0x5694"},
	{0x5695, INTEL_MODEL_TGLM, "DG2 PCI 0x5695"},
	{0x5696, INTEL_MODEL_TGLM, "DG2 PCI 0x5696"},
	{0x5697, INTEL_MODEL_TGLM, "DG2 PCI 0x5697"},
	{0x56a0, INTEL_MODEL_TGLM, "DG2 PCI 0x56a0"},
	{0x56a1, INTEL_MODEL_TGLM, "DG2 PCI 0x56a1"},
	{0x56a2, INTEL_MODEL_TGLM, "DG2 PCI 0x56a2"},
	{0x56a3, INTEL_MODEL_TGLM, "DG2 PCI 0x56a3"},
	{0x56a4, INTEL_MODEL_TGLM, "DG2 PCI 0x56a4"},
	{0x56a5, INTEL_MODEL_TGLM, "DG2 PCI 0x56a5"},
	{0x56a6, INTEL_MODEL_TGLM, "DG2 PCI 0x56a6"},
	{0x56b0, INTEL_MODEL_TGLM, "DG2 PCI 0x56b0"},
	{0x56b1, INTEL_MODEL_TGLM, "DG2 PCI 0x56b1"},
	{0x56b2, INTEL_MODEL_TGLM, "DG2 PCI 0x56b2"},
	{0x56b3, INTEL_MODEL_TGLM, "DG2 PCI 0x56b3"},
	{0x56ba, INTEL_MODEL_TGLM, "DG2 PCI 0x56ba"},
	{0x56bb, INTEL_MODEL_TGLM, "DG2 PCI 0x56bb"},
	{0x56bc, INTEL_MODEL_TGLM, "DG2 PCI 0x56bc"},
	{0x56bd, INTEL_MODEL_TGLM, "DG2 PCI 0x56bd"},
	{0x56be, INTEL_MODEL_TGLM, "DG2 PCI 0x56be"},
	{0x56bf, INTEL_MODEL_TGLM, "DG2 PCI 0x56bf"},

	// Arctic Sound-M IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0x56c0, INTEL_MODEL_TGLM, "Arctic Sound-M PCI 0x56c0"},
	{0x56c1, INTEL_MODEL_TGLM, "Arctic Sound-M PCI 0x56c1"},
	{0x56c2, INTEL_MODEL_TGLM, "Arctic Sound-M PCI 0x56c2"},

	// Arrow Lake IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0x7d41, INTEL_MODEL_TGLM, "Arrow Lake PCI 0x7d41"},
	{0x7d51, INTEL_MODEL_TGLM, "Arrow Lake PCI 0x7d51"},
	{0x7d67, INTEL_MODEL_TGLM, "Arrow Lake PCI 0x7d67"},
	{0x7dd1, INTEL_MODEL_TGLM, "Arrow Lake PCI 0x7dd1"},
	{0xb640, INTEL_MODEL_TGLM, "Arrow Lake PCI 0xb640"},

	// Meteor Lake IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0x7d40, INTEL_MODEL_TGLM, "Meteor Lake PCI 0x7d40"},
	{0x7d45, INTEL_MODEL_TGLM, "Meteor Lake PCI 0x7d45"},
	{0x7d55, INTEL_MODEL_TGLM, "Meteor Lake PCI 0x7d55"},
	{0x7d60, INTEL_MODEL_TGLM, "Meteor Lake PCI 0x7d60"},
	{0x7dd5, INTEL_MODEL_TGLM, "Meteor Lake PCI 0x7dd5"},

	// Lunar Lake IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0x6420, INTEL_MODEL_TGLM, "Lunar Lake PCI 0x6420"},
	{0x64a0, INTEL_MODEL_TGLM, "Lunar Lake PCI 0x64a0"},
	{0x64b0, INTEL_MODEL_TGLM, "Lunar Lake PCI 0x64b0"},

	// Battlemage IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0xe202, INTEL_MODEL_TGLM, "Battlemage PCI 0xe202"},
	{0xe209, INTEL_MODEL_TGLM, "Battlemage PCI 0xe209"},
	{0xe20b, INTEL_MODEL_TGLM, "Battlemage PCI 0xe20b"},
	{0xe20c, INTEL_MODEL_TGLM, "Battlemage PCI 0xe20c"},
	{0xe20d, INTEL_MODEL_TGLM, "Battlemage PCI 0xe20d"},
	{0xe210, INTEL_MODEL_TGLM, "Battlemage PCI 0xe210"},
	{0xe211, INTEL_MODEL_TGLM, "Battlemage PCI 0xe211"},
	{0xe212, INTEL_MODEL_TGLM, "Battlemage PCI 0xe212"},
	{0xe216, INTEL_MODEL_TGLM, "Battlemage PCI 0xe216"},
	{0xe220, INTEL_MODEL_TGLM, "Battlemage PCI 0xe220"},
	{0xe221, INTEL_MODEL_TGLM, "Battlemage PCI 0xe221"},
	{0xe222, INTEL_MODEL_TGLM, "Battlemage PCI 0xe222"},
	{0xe223, INTEL_MODEL_TGLM, "Battlemage PCI 0xe223"},

	// Panther Lake IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0xb080, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb080"},
	{0xb081, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb081"},
	{0xb082, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb082"},
	{0xb083, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb083"},
	{0xb084, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb084"},
	{0xb085, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb085"},
	{0xb086, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb086"},
	{0xb087, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb087"},
	{0xb08f, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb08f"},
	{0xb090, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb090"},
	{0xb0a0, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb0a0"},
	{0xb0b0, INTEL_MODEL_TGLM, "Panther Lake PCI 0xb0b0"},

	// Wildcat Lake IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0xfd80, INTEL_MODEL_TGLM, "Wildcat Lake PCI 0xfd80"},
	{0xfd81, INTEL_MODEL_TGLM, "Wildcat Lake PCI 0xfd81"},

	// Nova Lake-S IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0xd740, INTEL_MODEL_TGLM, "Nova Lake-S PCI 0xd740"},
	{0xd741, INTEL_MODEL_TGLM, "Nova Lake-S PCI 0xd741"},
	{0xd742, INTEL_MODEL_TGLM, "Nova Lake-S PCI 0xd742"},
	{0xd743, INTEL_MODEL_TGLM, "Nova Lake-S PCI 0xd743"},
	{0xd745, INTEL_MODEL_TGLM, "Nova Lake-S PCI 0xd745"},
	{0xd74a, INTEL_MODEL_TGLM, "Nova Lake-S PCI 0xd74a"},
	{0xd74b, INTEL_MODEL_TGLM, "Nova Lake-S PCI 0xd74b"},

	// Crescent Island IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0x674c, INTEL_MODEL_TGLM, "Crescent Island PCI 0x674c"},
	{0x674d, INTEL_MODEL_TGLM, "Crescent Island PCI 0x674d"},
	{0x674e, INTEL_MODEL_TGLM, "Crescent Island PCI 0x674e"},
	{0x674f, INTEL_MODEL_TGLM, "Crescent Island PCI 0x674f"},
	{0x6750, INTEL_MODEL_TGLM, "Crescent Island PCI 0x6750"},

	// Nova Lake-P IDs imported from Linux xe PCI table; closest existing Haiku model path.
	{0xd750, INTEL_MODEL_TGLM, "Nova Lake-P PCI 0xd750"},
	{0xd751, INTEL_MODEL_TGLM, "Nova Lake-P PCI 0xd751"},
	{0xd752, INTEL_MODEL_TGLM, "Nova Lake-P PCI 0xd752"},
	{0xd753, INTEL_MODEL_TGLM, "Nova Lake-P PCI 0xd753"},
	{0xd75f, INTEL_MODEL_TGLM, "Nova Lake-P PCI 0xd75f"},
};

int32 api_version = B_CUR_DRIVER_API_VERSION;

char* gDeviceNames[MAX_CARDS + 1];
intel_info* gDeviceInfo[MAX_CARDS];
pci_module_info* gPCI;
agp_gart_module_info* gGART;
mutex gLock;


static status_t
get_next_intel_extreme(int32* _cookie, pci_info &info, uint32 &type)
{
	int32 index = *_cookie;

	// find devices

	for (; gPCI->get_nth_pci_info(index, &info) == B_OK; index++) {
		// check vendor
		if (info.vendor_id != VENDOR_ID_INTEL
			|| info.class_base != PCI_display
			|| (info.class_sub != PCI_vga && info.class_sub != PCI_display_other))
			continue;

		// check device
		for (uint32 i = 0; i < sizeof(kSupportedDevices)
				/ sizeof(kSupportedDevices[0]); i++) {
			if (info.device_id == kSupportedDevices[i].device_id) {
				type = i;
				*_cookie = index + 1;
				ERROR("%s: Intel gfx deviceID: 0x%04x\n", __func__, info.device_id);
				return B_OK;
			}
		}
	}

	return B_ENTRY_NOT_FOUND;
}


static enum pch_info
detect_intel_pch()
{
	pci_info info;

	// find devices
	for (int32 i = 0; gPCI->get_nth_pci_info(i, &info) == B_OK; i++) {
		// check vendor
		if (info.vendor_id != VENDOR_ID_INTEL
			|| info.class_base != PCI_bridge
			|| info.class_sub != PCI_isa) {
			continue;
		}

		// check device
		unsigned short id = info.device_id & INTEL_PCH_DEVICE_ID_MASK;
		ERROR("%s: Intel PCH deviceID: 0x%04x\n", __func__, info.device_id);
		switch(id) {
			case INTEL_PCH_IBX_DEVICE_ID:
				ERROR("%s: Found Ibex Peak PCH\n", __func__);
				return INTEL_PCH_IBX;
			case INTEL_PCH_CPT_DEVICE_ID:
				ERROR("%s: Found CougarPoint PCH\n", __func__);
				return INTEL_PCH_CPT;
			case INTEL_PCH_PPT_DEVICE_ID:
				ERROR("%s: Found PantherPoint PCH\n", __func__);
				return INTEL_PCH_CPT;
			case INTEL_PCH_LPT_DEVICE_ID:
			case INTEL_PCH_LPT_LP_DEVICE_ID:
				ERROR("%s: Found LynxPoint PCH\n", __func__);
				return INTEL_PCH_LPT;
			case INTEL_PCH_WPT_DEVICE_ID:
			case INTEL_PCH_WPT_LP_DEVICE_ID:
				ERROR("%s: Found WildcatPoint PCH\n", __func__);
				return INTEL_PCH_LPT;
			case INTEL_PCH_SPT_DEVICE_ID:
			case INTEL_PCH_SPT_LP_DEVICE_ID:
				ERROR("%s: Found SunrisePoint PCH\n", __func__);
				return INTEL_PCH_SPT;
			case INTEL_PCH_KBP_DEVICE_ID:
				ERROR("%s: Found Kaby Lake PCH\n", __func__);
				return INTEL_PCH_SPT;
			case INTEL_PCH_GMP_DEVICE_ID:
				ERROR("%s: Found Gemini Lake PCH\n", __func__);
				return INTEL_PCH_CNP;
			case INTEL_PCH_CNP_DEVICE_ID:
			case INTEL_PCH_CNP_LP_DEVICE_ID:
				ERROR("%s: Found Cannon Lake PCH\n", __func__);
				return INTEL_PCH_CNP;
			case INTEL_PCH_APL_LP_DEVICE_ID:
				ERROR("%s: Found Apollo Lake PCH\n", __func__);
				return INTEL_PCH_CNP;
			case INTEL_PCH_CMP_DEVICE_ID:
			case INTEL_PCH_CMP2_DEVICE_ID:
				ERROR("%s: Found Comet Lake PCH\n", __func__);
				return INTEL_PCH_CNP;
			case INTEL_PCH_CMP_V_DEVICE_ID:
				ERROR("%s: Found Comet Lake V PCH\n", __func__);
				return INTEL_PCH_SPT;
			case INTEL_PCH_ICP_DEVICE_ID:
			case INTEL_PCH_ICP2_DEVICE_ID:
				ERROR("%s: Found Ice Lake PCH\n", __func__);
				return INTEL_PCH_ICP;
			case INTEL_PCH_MCC_DEVICE_ID:
				ERROR("%s: Found Mule Creek Canyon PCH\n", __func__);
				return INTEL_PCH_MCC;
			case INTEL_PCH_TGP_DEVICE_ID:
			case INTEL_PCH_TGP2_DEVICE_ID:
				ERROR("%s: Found Tiger Lake PCH\n", __func__);
				return INTEL_PCH_TGP;
			case INTEL_PCH_JSP_DEVICE_ID:
				ERROR("%s: Found Jasper Lake PCH\n", __func__);
				return INTEL_PCH_JSP;
			case INTEL_PCH_ADP_DEVICE_ID:
			case INTEL_PCH_ADP2_DEVICE_ID:
			case INTEL_PCH_ADP3_DEVICE_ID:
			case INTEL_PCH_ADP4_DEVICE_ID:
			case INTEL_PCH_ADP5_DEVICE_ID:
				ERROR("%s: Found Alder Lake PCH\n", __func__);
				return INTEL_PCH_ADP;
		}
	}

	ERROR("%s: No PCH detected.\n", __func__);
	return INTEL_PCH_NONE;
}


extern "C" const char**
publish_devices(void)
{
	CALLED();
	return (const char**)gDeviceNames;
}


extern "C" status_t
init_hardware(void)
{
	CALLED();

	status_t status = get_module(B_PCI_MODULE_NAME,(module_info**)&gPCI);
	if (status != B_OK) {
		ERROR("pci module unavailable\n");
		return status;
	}

	int32 cookie = 0;
	uint32 type;
	pci_info info;
	status = get_next_intel_extreme(&cookie, info, type);

	put_module(B_PCI_MODULE_NAME);
	return status;
}


extern "C" status_t
init_driver(void)
{
	CALLED();

	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI);
	if (status != B_OK) {
		ERROR("pci module unavailable\n");
		return status;
	}

	status = get_module(B_AGP_GART_MODULE_NAME, (module_info**)&gGART);
	if (status != B_OK) {
		ERROR("AGP GART module unavailable\n");
		put_module(B_PCI_MODULE_NAME);
		return status;
	}

	mutex_init(&gLock, "intel extreme ksync");

	// find the PCH device (if any)
	enum pch_info pchInfo = detect_intel_pch();

	// find devices

	int32 found = 0;

	for (int32 cookie = 0; found < MAX_CARDS;) {
		pci_info* info = (pci_info*)malloc(sizeof(pci_info));
		if (info == NULL)
			break;

		uint32 type;
		status = get_next_intel_extreme(&cookie, *info, type);
		if (status < B_OK) {
			free(info);
			break;
		}

		// create device names & allocate device info structure

		char name[64];
		sprintf(name, "graphics/intel_extreme_%02x%02x%02x",
			 info->bus, info->device,
			 info->function);

		gDeviceNames[found] = strdup(name);
		if (gDeviceNames[found] == NULL)
			break;

		gDeviceInfo[found] = (intel_info*)malloc(sizeof(intel_info));
		if (gDeviceInfo[found] == NULL) {
			free(gDeviceNames[found]);
			break;
		}

		// initialize the structure for later use

		memset((void*)gDeviceInfo[found], 0, sizeof(intel_info));
		gDeviceInfo[found]->init_status = B_NO_INIT;
		gDeviceInfo[found]->id = found;
		gDeviceInfo[found]->pci = info;
		gDeviceInfo[found]->registers = info->u.h0.base_registers[0];
		gDeviceInfo[found]->device_identifier = kSupportedDevices[type].name;
		gDeviceInfo[found]->device_type = kSupportedDevices[type].type;
		gDeviceInfo[found]->pch_info = pchInfo;

		dprintf(DEVICE_NAME ": (%" B_PRId32 ") %s, revision = 0x%x\n", found,
			kSupportedDevices[type].name, info->revision);

		found++;
	}

	gDeviceNames[found] = NULL;

	if (found == 0) {
		mutex_destroy(&gLock);
		put_module(B_AGP_GART_MODULE_NAME);
		put_module(B_PCI_MODULE_NAME);
		return ENODEV;
	}

	return B_OK;
}


extern "C" void
uninit_driver(void)
{
	CALLED();

	mutex_destroy(&gLock);

	// free device related structures
	char* name;
	for (int32 index = 0; (name = gDeviceNames[index]) != NULL; index++) {
		free(gDeviceInfo[index]);
		free(name);
	}

	put_module(B_AGP_GART_MODULE_NAME);
	put_module(B_PCI_MODULE_NAME);
}


extern "C" device_hooks*
find_device(const char* name)
{
	CALLED();

	int index;
	for (index = 0; gDeviceNames[index] != NULL; index++) {
		if (!strcmp(name, gDeviceNames[index]))
			return &gDeviceHooks;
	}

	return NULL;
}
