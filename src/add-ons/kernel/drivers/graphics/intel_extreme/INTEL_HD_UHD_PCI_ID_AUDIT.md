# Intel HD/UHD Graphics PCI-ID source audit

Branch: `work/intel-extreme-hd530-edid-2d`

Purpose: review Intel-published sources for PCI device IDs associated with Intel HD Graphics and Intel UHD Graphics, and compare that evidence with this Haiku driver's explicit PCI allowlist. This is an **ID research inventory**, not a statement that every listed GPU is supported by this driver.

## Findings

1. **Intel does not appear to publish one single, exhaustive, current master table for every historical and current HD/UHD-branded integrated GPU.** Intel's public materials are split across the Processor Graphics developer guide, a legacy-GPU table, a current i915-supported-GPU table, and the older GPU Detect sample.
2. The most useful broad Intel-published source for older HD/UHD generations is Intel's **Legacy GPUs** table. It lists PCI IDs, product labels, architecture, codename, and kernel floor. Intel explicitly describes it as legacy GPUs commonly enabled in Linux distributions, not as a complete catalogue of every Intel GPU ever sold.
3. Intel's **i915 KMD Driver GPUs** table supplements that older list with newer UHD-branded integrated graphics (Rocket Lake through Raptor Lake and several Alder Lake variants). It is a Linux driver-support table, not a guarantee of Haiku display-engine compatibility.
4. Intel's Processor Graphics developer guide points readers to the **GPU Detect** sample, whose `IntelGfx.cfg` contains many IDs and product labels. It is useful historical corroboration, but it is older and is not comprehensive for modern UHD products.
5. **PCI ID is not enough to enable a device in Haiku.** The allowlist model selects generation, DDI, PCH/PLL and display paths. IDs need platform mapping and display-path validation separately. Do not turn this inventory into a bulk allowlist patch.

## Intel-published sources reviewed

- Intel, [Processor Graphics developer documents](https://www.intel.com/content/www/us/en/developer/articles/guide/intel-graphics-developers-guides.html) — maps product series/codenames to graphics generations and explicitly points to GPU Detect for device-ID examples.
- Intel, [Legacy GPUs hardware table](https://dgpu-docs.intel.com/overview/supported-hardware/legacy-gpus.html) — broad legacy PCI-ID/product-name table.
- Intel, [i915 KMD Driver GPUs hardware table](https://dgpu-docs.intel.com/overview/supported-hardware/i915-driver-gpus.html) — current supported-hardware table, including newer UHD products.
- Intel-linked sample, [GameTechDev/gpudetect IntelGfx.cfg](https://raw.githubusercontent.com/GameTechDev/gpudetect/master/IntelGfx.cfg) — older example ID/name list.
- Intel, [Graphics for Linux PRM index](https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/overview.html) — architectural programming references; useful for display programming, but not a universal PCI-ID catalogue.
- Cross-check reference (not itself an Intel-authored product catalogue): [Linux i915 PCI device table](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/i915_pci.c). This matters where product/codename labels in broad hardware tables are ambiguous.

All IDs below are 16-bit PCI device IDs; Intel vendor ID is `8086`. These are the HD/UHD-branded or generically labelled HD/UHD entries visible in the cited Intel hardware tables. Rows explicitly branded Iris are intentionally omitted from this HD/UHD-only inventory.

## IDs in Intel's legacy-GPU table

| Platform / codename | Intel table's HD/UHD PCI IDs | Names as labelled in the Intel table |
|---|---|---|
| Ice Lake, Gen11 | `8A70`, `8A71`, `8A56`, `8A58`, `8A5B`, `8A5D`, `8A57`, `8A59`, `8A50` | HD Graphics; UHD Graphics |
| Coffee Lake, Gen9 | `3EA7`, `3EA2`, `3E90`, `3E93`, `3E99`, `3E9C`, `3EA1`, `9BA5`, `9BA8`, `3EA4`, `9B21`, `9BA0`, `9BA2`, `9BA4`, `9BAA`, `9BAB`, `9BAC`, `87CA`, `3EA3`, `9B41`, `9BC0`, `9BC2`, `9BC4`, `9BCA`, `9BCB`, `9BCC`, `3E91`, `3E92`, `3E98`, `3E9B`, `9BC5`, `9BC8`, `3EA9`, `3EA0` | HD Graphics; UHD Graphics; UHD 610; UHD 620; UHD 630 |
| Kaby Lake, Gen9 | `593B`, `5923`, `5917`, `5912`, `591B`, `5916`, `5921`, `591A`, `591D`, `591E`, `591C`, `87C0`, `5913`, `5915`, `5902`, `5906`, `590B`, `590A`, `5908`, `590E` | HD Graphics; HD 635/630/620/615/610; UHD Graphics 620/615/617 |
| Gemini Lake, Gen9 | `3185`, `3184` | UHD Graphics 600; UHD Graphics 605 |
| Apollo Lake, Gen9 | `1A85`, `5A85`, `0A84`, `1A84`, `5A84` | HD Graphics; HD Graphics 500; HD Graphics 505 |
| Skylake, Gen9 | `192A`, `1923`, `1912`, `191B`, `1913`, `1915`, `1917`, `191A`, `1916`, `1921`, `191D`, `191E`, `1902`, `1906`, `190B`, `190A`, `190E` | HD Graphics; HD 535/530/520/P530/515/510 |
| Broadwell, Gen8 | `163D`, `163A`, `1632`, `163E`, `163B`, `1636`, `1626`, `162D`, `162E`, `1612`, `1616`, `161B`, `161D`, `161E`, `1602`, `1606`, `160A`, `160B`, `160D`, `160E` | HD Graphics; HD 6000/5600/5500/5300 |
| Cherryview, Gen8 | `22B0`, `22B1`, `22B2`, `22B3` | HD Graphics (the source labels `22B1` as “HD Graphics XXX”) |
| Bay Trail, Gen7 | `0F30`, `0F31`, `0F32`, `0F33`, `0157`, `0155` | HD Graphics |
| Haswell, Gen7 | `0422`, `0426`, `042A`, `042B`, `042E`, `0C22`, `0C26`, `0C2A`, `0C2B`, `0C2E`, `0A22`, `0A2A`, `0A2B`, `0D2A`, `0D2B`, `0D2E`, `0A26`, `0D22`, `0D26`, `0412`, `0416`, `0D12`, `041A`, `041B`, `0C12`, `0C16`, `0C1A`, `0C1B`, `0C1E`, `0A12`, `0A1A`, `0A1B`, `0D16`, `0D1A`, `0D1B`, `0D1E`, `041E`, `0A16`, `0A1E`, `0402`, `0406`, `040A`, `040B`, `040E`, `0C02`, `0C06`, `0C0A`, `0C0B`, `0C0E`, `0A02`, `0A06`, `0A0A`, `0A0B`, `0A0E`, `0D02`, `0D06`, `0D0A`, `0D0B`, `0D0E` | HD Graphics; HD 5000/4600/P4600-P4700/4400/4200 |
| Ivy Bridge, Gen7 | `0162`, `0166`, `016A`, `0152`, `0156`, `015A` | HD Graphics 4000/P4000/2500; HD Graphics |
| Sandy Bridge, Gen6 | `0112`, `0122`, `0116`, `0126`, `0102`, `0106`, `010A` | HD Graphics 3000; HD Graphics 2000 (Intel table groups `010A` under this label) |
| Ironlake, Gen5 | `0042`, `0046` | HD Graphics |

The Intel legacy table also includes older GMA/integrated chipsets predating the HD Graphics branding. They are outside the strict HD/UHD-branded inventory above, but some are already present in the Haiku driver's allowlist.

## Additional UHD entries in Intel's current i915 table

| Platform | PCI IDs | Intel table's product label |
|---|---|---|
| Raptor Lake-S | `A780`, `A781`, `A788`, `A789`, `A78A`, `A782`, `A78B`, `A783` | UHD Graphics 770; UHD Graphics; UHD Graphics 730; UHD Graphics 710 |
| Raptor Lake-P/U | `A7A8`, `A7A9`, `A721` | UHD Graphics |
| Alder Lake-S | `4680`, `4690`, `4688`, `468A`, `468B`, `4682`, `4692`, `4693` | UHD Graphics 770/730/710 |
| Alder Lake-N | `46D0`, `46D1`, `46D2` | UHD Graphics |
| Alder Lake-P | `4626`, `4628`, `462A`, `46A2`, `46B3`, `46C2`, `46A3`, `46B2`, `46C3` | UHD Graphics |
| Rocket Lake | `4C8A`, `4C8B`, `4C90`, `4C9A` | UHD Graphics 750/730/P750 |
| Jasper Lake | `4E71`, `4E61`, `4E57`, `4E55`, `4E51` | UHD Graphics |
| Elkhart Lake | `4557`, `4555`, `4571`, `4551`, `4541` | UHD Graphics |
| Tiger Lake | `9A59`, `9A78`, `9A60`, `9A70`, `9A68` | UHD Graphics |

The same current i915 table lists some newer products as simply “Intel Graphics” rather than HD/UHD (for example some Arrow Lake and Twin Lake IDs). They are not included above because the requested scope is the HD/UHD branding; they would need a separate forward-looking Intel integrated-graphics audit.

## Important ambiguity: Whiskey Lake / Coffee Lake IDs

The Intel legacy-GPU table groups `3EA0` under “Coffee Lake / UHD Graphics 620” and `3EA1` under “Coffee Lake / UHD Graphics 610”. Linux i915 PCI-ID history has identified `8086:3EA0` and `8086:3EA1` with Whiskey Lake-U SKUs in shared WHL/CFL-U ID groupings. This is an example of why a product-name table must not be treated as definitive platform-classification evidence. For Haiku, verify the actual PCI ID and platform/PCH before choosing a model group.

## Comparison with the Haiku branch allowlist

The branch's `src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp` uses an explicit allowlist. It contains a useful subset of HD/UHD-era IDs, but it is **not an exhaustive list of all HD/UHD IDs in the Intel tables**. Examples from the reviewed source inventory that are absent from the current allowlist include:

- Coffee Lake / UHD: `3EA0`, `3EA1`, `3EA2`, `3EA3`, `3EA4`, `3EA7`, `3EA8`, plus several `9Bxx` and `9BAx` variants.
- Kaby Lake / HD-UHD: `5908`, `590B`, `590E`, `5913`, `5915`, `591C`, `591E`, `5923`, `593B`, `87C0`.
- Broadwell / HD: multiple `160x`, `161x`, and `163x` IDs.
- Skylake / HD: several `190x`, `191x`, `192x`, and `193x` IDs beyond the subset already listed.
- Later UHD platforms: additional Tiger Lake, Alder Lake, Rocket Lake, Jasper Lake, and Elkhart Lake IDs appear in Intel's table but are not all represented in the Haiku allowlist.

This comparison is deliberately a **research gap list**, not a recommendation to add those IDs. Some variants are mobile, server, workstation, low-power SoC, fused-off, or shared/reclassified SKUs; their display resources and platform setup must be mapped correctly.

## Recommended driver policy

1. Keep the explicit allowlist; do not switch to a wildcard or accept all Intel `8086` display IDs.
2. Maintain separate fields for the **PCI ID evidence**, **intended Haiku model/group**, **generation**, **PCH/platform assumptions**, and **validation status**.
3. Add IDs in small platform batches only after tracing the exact model mapping through `DeviceType::Generation()`, `HasDDI()`, PCH detection, PLL selection, AUX/DDC routing, and pipe/plane setup.
4. For the current CPU-only 2D target, test display scanout and CPU framebuffer writes. BCS/GPU command submission is a separate feature and is not a prerequisite for CPU drawing.
5. Treat HD 530 (`8086:1912`) as the user-reported tested baseline for this branch; do not infer that other Gen9 IDs are thereby validated.

## Validation boundary

This document records public Intel-source evidence and a static comparison with the branch's allowlist. It does not claim a new build, hardware test, or working display path for any newly listed PCI ID.
