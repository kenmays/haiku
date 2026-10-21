# Intel integrated graphics PCI IDs: Linux upstream and Windows driver cross-check

Branch: `work/intel-extreme-hd530-edid-2d`

## Scope and caveats

This is a source-derived cross-check, not a Haiku support declaration. Linux IDs below are extracted from the upstream Linux kernel's Intel PCI ID macro header. The Windows set is extracted from a published transcription of Intel DCH WHQL driver INF `iigd_dch.inf`, version 25.20.100.6448 (December 2018), and is therefore a **historical Windows INF snapshot**, not a claim about the latest Windows package. Windows INF IDs can change between package branches and OS-specific model sections.

Discrete GPU families (DG1, DG2, ATS-M, PVC, BMG and CRI) are excluded from the Linux integrated-graphics inventory. Integrated families with newer naming (Meteor Lake, Lunar Lake, Panther Lake, Wildcat Lake and Nova Lake) are included where represented in upstream `pciids.h`. Older legacy integrated devices are included too; some predate the HD/UHD branding.

**Do not bulk-add missing IDs to Haiku's allowlist.** Linux/Windows driver match means the vendor or OS driver recognizes the PCI ID; it does not prove Haiku's display initialization path is compatible. The current branch's platform/model mapping, PCH, PLL, DDI, AUX/DDC, panel sequencing, transcoder and plane setup must be audited per family. No allowlist IDs were changed by this report.

## Sources

- Linux upstream, `include/drm/intel/pciids.h`: https://github.com/torvalds/linux/blob/master/include/drm/intel/pciids.h
- Linux upstream registration/platform mapping: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/i915_pci.c
- Windows Intel DCH INF ID transcription (source says IDs are taken from Intel `iigd_dch.inf`, version 25.20.100.6448): https://www.windowsphoneinfo.com/threads/the-following-list-of-hardware-ids-are-supported-by-the-intel-core-generation-graphics.140172/
- Intel's current generic graphics driver download page and package scope (use a matching package/INF when refreshing Windows data): https://www.intel.com/content/www/us/en/download/785597/intel-arc-graphics-windows.html

## Windows INF snapshot: extracted IDs

The source lists these IDs under the following platform sections. IDs are four-digit PCI device IDs; vendor is Intel `8086`.

- **Skylake:** `1906`, `1916`, `191E`, `1912`, `1921`, `1926`, `1923`, `1927`, `1902`, `190B`, `193B`, `192B`, `191B`, `192D`, `193A`, `191D`, `193D`
- **Apollo Lake:** `5A84`, `5A85`
- **Kaby Lake:** `5906`, `5916`, `5921`, `5926`, `5927`, `591E`, `5902`, `5912`, `590B`, `591B`, `591D`, `5917`
- **Amber Lake:** `591C`, `87C0`
- **Coffee Lake:** `3E90`, `3E92`, `3E96`, `3E9B`, `3E94`, `3EA5`, `3EA8`, `3E93`, `3E91`, `3E98`
- **Whiskey Lake:** `3EA0`, `3EA1`
- **Gemini Lake:** `3184`, `3185`

Windows snapshot count: **47 distinct IDs**.

## Linux upstream integrated-graphics IDs: extracted from macro groups

The following device IDs are extracted directly from the current upstream `pciids.h` macros, grouped by macro name. Empty wrapper macros are omitted. Macro grouping is useful for platform research, but the macro name itself is not a complete product-name mapping.

- **`INTEL_I810_IDS` (3):** `7121`, `7123`, `7125`
- **`INTEL_I815_IDS` (1):** `1132`
- **`INTEL_I830_IDS` (1):** `3577`
- **`INTEL_I845G_IDS` (1):** `2562`
- **`INTEL_I85X_IDS` (2):** `3582`, `358E`
- **`INTEL_I865G_IDS` (1):** `2572`
- **`INTEL_I915G_IDS` (2):** `2582`, `258A`
- **`INTEL_I915GM_IDS` (1):** `2592`
- **`INTEL_I945G_IDS` (1):** `2772`
- **`INTEL_I945GM_IDS` (2):** `27A2`, `27AE`
- **`INTEL_I965G_IDS` (4):** `2972`, `2982`, `2992`, `29A2`
- **`INTEL_G33_IDS` (3):** `29B2`, `29C2`, `29D2`
- **`INTEL_I965GM_IDS` (2):** `2A02`, `2A12`
- **`INTEL_GM45_IDS` (1):** `2A42`
- **`INTEL_G45_IDS` (6):** `2E02`, `2E12`, `2E22`, `2E32`, `2E42`, `2E92`
- **`INTEL_PNV_G_IDS` (1):** `A001`
- **`INTEL_PNV_M_IDS` (1):** `A011`
- **`INTEL_ILK_D_IDS` (1):** `0042`
- **`INTEL_ILK_M_IDS` (1):** `0046`
- **`INTEL_SNB_D_GT1_IDS` (2):** `0102`, `010A`
- **`INTEL_SNB_D_GT2_IDS` (2):** `0112`, `0122`
- **`INTEL_SNB_M_GT1_IDS` (1):** `0106`
- **`INTEL_SNB_M_GT2_IDS` (2):** `0116`, `0126`
- **`INTEL_IVB_M_GT1_IDS` (1):** `0156`
- **`INTEL_IVB_M_GT2_IDS` (1):** `0166`
- **`INTEL_IVB_D_GT1_IDS` (2):** `0152`, `015A`
- **`INTEL_IVB_D_GT2_IDS` (2):** `0162`, `016A`
- **`INTEL_HSW_ULT_GT1_IDS` (4):** `0A02`, `0A06`, `0A0A`, `0A0B`
- **`INTEL_HSW_ULX_GT1_IDS` (1):** `0A0E`
- **`INTEL_HSW_GT1_IDS` (15):** `0402`, `0406`, `040A`, `040B`, `040E`, `0C02`, `0C06`, `0C0A`, `0C0B`, `0C0E`, `0D02`, `0D06`, `0D0A`, `0D0B`, `0D0E`
- **`INTEL_HSW_ULT_GT2_IDS` (4):** `0A12`, `0A16`, `0A1A`, `0A1B`
- **`INTEL_HSW_ULX_GT2_IDS` (1):** `0A1E`
- **`INTEL_HSW_GT2_IDS` (15):** `0412`, `0416`, `041A`, `041B`, `041E`, `0C12`, `0C16`, `0C1A`, `0C1B`, `0C1E`, `0D12`, `0D16`, `0D1A`, `0D1B`, `0D1E`
- **`INTEL_HSW_ULT_GT3_IDS` (5):** `0A22`, `0A26`, `0A2A`, `0A2B`, `0A2E`
- **`INTEL_HSW_GT3_IDS` (15):** `0422`, `0426`, `042A`, `042B`, `042E`, `0C22`, `0C26`, `0C2A`, `0C2B`, `0C2E`, `0D22`, `0D26`, `0D2A`, `0D2B`, `0D2E`
- **`INTEL_VLV_IDS` (4):** `0F30`, `0F31`, `0F32`, `0F33`
- **`INTEL_BDW_ULT_GT1_IDS` (2):** `1606`, `160B`
- **`INTEL_BDW_ULX_GT1_IDS` (1):** `160E`
- **`INTEL_BDW_GT1_IDS` (3):** `1602`, `160A`, `160D`
- **`INTEL_BDW_ULT_GT2_IDS` (2):** `1616`, `161B`
- **`INTEL_BDW_ULX_GT2_IDS` (1):** `161E`
- **`INTEL_BDW_GT2_IDS` (3):** `1612`, `161A`, `161D`
- **`INTEL_BDW_ULT_GT3_IDS` (2):** `1626`, `162B`
- **`INTEL_BDW_ULX_GT3_IDS` (1):** `162E`
- **`INTEL_BDW_GT3_IDS` (3):** `1622`, `162A`, `162D`
- **`INTEL_BDW_ULT_RSVD_IDS` (2):** `1636`, `163B`
- **`INTEL_BDW_ULX_RSVD_IDS` (1):** `163E`
- **`INTEL_BDW_RSVD_IDS` (3):** `1632`, `163A`, `163D`
- **`INTEL_CHV_IDS` (4):** `22B0`, `22B1`, `22B2`, `22B3`
- **`INTEL_SKL_ULT_GT1_IDS` (2):** `1906`, `1913`
- **`INTEL_SKL_ULX_GT1_IDS` (2):** `190E`, `1915`
- **`INTEL_SKL_GT1_IDS` (4):** `1902`, `190A`, `190B`, `1917`
- **`INTEL_SKL_ULT_GT2_IDS` (2):** `1916`, `1921`
- **`INTEL_SKL_ULX_GT2_IDS` (1):** `191E`
- **`INTEL_SKL_GT2_IDS` (4):** `1912`, `191A`, `191B`, `191D`
- **`INTEL_SKL_ULT_GT3_IDS` (3):** `1923`, `1926`, `1927`
- **`INTEL_SKL_GT3_IDS` (3):** `192A`, `192B`, `192D`
- **`INTEL_SKL_GT4_IDS` (4):** `1932`, `193A`, `193B`, `193D`
- **`INTEL_BXT_IDS` (5):** `0A84`, `1A84`, `1A85`, `5A84`, `5A85`
- **`INTEL_GLK_IDS` (2):** `3184`, `3185`
- **`INTEL_KBL_ULT_GT1_IDS` (2):** `5906`, `5913`
- **`INTEL_KBL_ULX_GT1_IDS` (2):** `590E`, `5915`
- **`INTEL_KBL_GT1_IDS` (4):** `5902`, `5908`, `590A`, `590B`
- **`INTEL_KBL_ULT_GT2_IDS` (2):** `5916`, `5921`
- **`INTEL_KBL_ULX_GT2_IDS` (1):** `591E`
- **`INTEL_KBL_GT2_IDS` (5):** `5912`, `5917`, `591A`, `591B`, `591D`
- **`INTEL_KBL_ULT_GT3_IDS` (1):** `5926`
- **`INTEL_KBL_GT3_IDS` (2):** `5923`, `5927`
- **`INTEL_KBL_GT4_IDS` (1):** `593B`
- **`INTEL_AML_KBL_GT2_IDS` (2):** `591C`, `87C0`
- **`INTEL_AML_CFL_GT2_IDS` (1):** `87CA`
- **`INTEL_CML_GT1_IDS` (4):** `9BA2`, `9BA4`, `9BA5`, `9BA8`
- **`INTEL_CML_U_GT1_IDS` (3):** `9B21`, `9BAA`, `9BAC`
- **`INTEL_CML_GT2_IDS` (7):** `9BC2`, `9BC4`, `9BC5`, `9BC6`, `9BC8`, `9BE6`, `9BF6`
- **`INTEL_CML_U_GT2_IDS` (3):** `9B41`, `9BCA`, `9BCC`
- **`INTEL_CFL_S_GT1_IDS` (3):** `3E90`, `3E93`, `3E99`
- **`INTEL_CFL_S_GT2_IDS` (5):** `3E91`, `3E92`, `3E96`, `3E98`, `3E9A`
- **`INTEL_CFL_H_GT1_IDS` (1):** `3E9C`
- **`INTEL_CFL_H_GT2_IDS` (2):** `3E94`, `3E9B`
- **`INTEL_CFL_U_GT2_IDS` (1):** `3EA9`
- **`INTEL_CFL_U_GT3_IDS` (4):** `3EA5`, `3EA6`, `3EA7`, `3EA8`
- **`INTEL_WHL_U_GT1_IDS` (2):** `3EA1`, `3EA4`
- **`INTEL_WHL_U_GT2_IDS` (2):** `3EA0`, `3EA3`
- **`INTEL_WHL_U_GT3_IDS` (1):** `3EA2`
- **`INTEL_CNL_PORT_F_IDS` (4):** `5A44`, `5A4C`, `5A54`, `5A5C`
- **`INTEL_CNL_IDS` (10):** `5A40`, `5A41`, `5A42`, `5A49`, `5A4A`, `5A50`, `5A51`, `5A52`, `5A59`, `5A5A`
- **`INTEL_ICL_PORT_F_IDS` (13):** `8A50`, `8A52`, `8A53`, `8A54`, `8A56`, `8A57`, `8A58`, `8A59`, `8A5A`, `8A5B`, `8A5C`, `8A70`, `8A71`
- **`INTEL_ICL_IDS` (2):** `8A51`, `8A5D`
- **`INTEL_EHL_IDS` (6):** `4541`, `4551`, `4555`, `4557`, `4570`, `4571`
- **`INTEL_JSL_IDS` (5):** `4E51`, `4E55`, `4E57`, `4E61`, `4E71`
- **`INTEL_TGL_GT1_IDS` (3):** `9A60`, `9A68`, `9A70`
- **`INTEL_TGL_GT2_IDS` (8):** `9A40`, `9A49`, `9A59`, `9A78`, `9AC0`, `9AC9`, `9AD9`, `9AF8`
- **`INTEL_RKL_IDS` (6):** `4C80`, `4C8A`, `4C8B`, `4C8C`, `4C90`, `4C9A`
- **`INTEL_ADLS_IDS` (8):** `4680`, `4682`, `4688`, `468A`, `468B`, `4690`, `4692`, `4693`
- **`INTEL_ADLP_IDS` (18):** `46A0`, `46A1`, `46A2`, `46A3`, `46A6`, `46A8`, `46AA`, `462A`, `4626`, `4628`, `46B0`, `46B1`, `46B2`, `46B3`, `46C0`, `46C1`, `46C2`, `46C3`
- **`INTEL_ADLN_IDS` (5):** `46D0`, `46D1`, `46D2`, `46D3`, `46D4`
- **`INTEL_RPLS_IDS` (8):** `A780`, `A781`, `A782`, `A783`, `A788`, `A789`, `A78A`, `A78B`
- **`INTEL_RPLU_IDS` (5):** `A721`, `A7A1`, `A7A9`, `A7AC`, `A7AD`
- **`INTEL_RPLP_IDS` (5):** `A720`, `A7A0`, `A7A8`, `A7AA`, `A7AB`
- **`INTEL_ARL_H_IDS` (2):** `7D51`, `7DD1`
- **`INTEL_ARL_U_IDS` (1):** `7D41`
- **`INTEL_ARL_S_IDS` (2):** `7D67`, `B640`
- **`INTEL_MTL_U_IDS` (2):** `7D40`, `7D45`
- **`INTEL_MTL_IDS` (3):** `7D55`, `7D60`, `7DD5`
- **`INTEL_LNL_IDS` (3):** `6420`, `64A0`, `64B0`
- **`INTEL_PTL_IDS` (12):** `B080`, `B081`, `B082`, `B083`, `B084`, `B085`, `B086`, `B087`, `B08F`, `B090`, `B0A0`, `B0B0`
- **`INTEL_WCL_IDS` (2):** `FD80`, `FD81`
- **`INTEL_NVLS_IDS` (7):** `D740`, `D741`, `D742`, `D743`, `D745`, `D74A`, `D74B`
- **`INTEL_NVLP_IDS` (5):** `D750`, `D751`, `D752`, `D753`, `D75F`

Linux integrated-group inventory count: **378 distinct IDs** across **109 non-empty macro groups**.

## Cross-check summary

- Windows snapshot IDs also present in the Linux integrated-group inventory: **47/47**.
- Windows snapshot IDs not found in the selected Linux integrated-group inventory: none.
- Linux integrated-group IDs not currently present in this Haiku branch's explicit allowlist: **176**. This is an audit backlog, **not** a recommendation to enable them.
- Haiku branch allowlist count parsed from `driver.cpp`: **216** entries (device IDs; names/platform mapping are still source-code-specific).

## Special ambiguity: Whiskey Lake / Coffee Lake

The Windows INF snapshot explicitly assigns `3EA0` and `3EA1` to Whiskey Lake model sections. Linux groups `3EA0`–`3EA4` under `INTEL_WHL_U_*` macros, while Intel's legacy GPU table has historically labeled some overlapping IDs as Coffee Lake UHD variants. Preserve source-specific platform classification rather than collapsing by marketing name.

## Refresh procedure

1. Pin a Linux kernel commit and extract the `INTEL_*_IDS` macros from `include/drm/intel/pciids.h`; exclude discrete GPU families if the task is integrated graphics only.
2. Download the intended Intel Windows driver package from Intel, extract its graphics INF (usually `iigd_dch.inf` for DCH packages; older branches may use `igdlh64.inf`), and parse model sections for `PCI\\VEN_8086&DEV_XXXX`.
3. Record package version, date, INF filename and source hash beside the extracted list. Do not merge IDs from unrelated Windows driver branches without preserving provenance.
4. Compare both source inventories to Haiku's allowlist, then audit model/platform mapping and display paths before any enablement.
