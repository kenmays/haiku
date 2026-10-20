# Intel HD Graphics 530 / Skylake Gen9 driver audit

This is an implementation audit for Haiku's `intel_extreme` driver, using Intel's Skylake-specific programmer documentation as the normative source. It is not a claim of complete hardware support. No Haiku build, emulator validation, or physical Skylake GPU test has been run as part of this audit.

## Target and scope

Intel identifies the desktop Core i5-6500's integrated GPU as Intel HD Graphics 530, device ID `0x1912`, with eDP/DP/HDMI/DVI output capabilities and support for up to three displays. These are platform-level capabilities, not a promise that every motherboard exposes every connector or mode:
- [Intel Core i5-6500 specifications](https://www.intel.com/content/www/us/en/products/sku/88184/intel-core-i56500-processor-6m-cache-up-to-3-60-ghz/specifications.html)
- [Intel Skylake S platform documentation](https://www.intel.com/content/www/us/en/products/platforms/details/skylake-s/docs.html)

The driver must distinguish the GPU's capabilities from board routing, PCH generation, VBT firmware data, PHY wiring, connector type, and monitor capabilities. Mini DisplayPort is a connector form factor for DisplayPort signaling; it is not a separate GPU display protocol.

## Primary references to use

1. **Skylake Gen9 PRM (normative for GPU command/register behavior):** [Intel's official Skylake PRM index](https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/skylake.html). Review all relevant volumes, especially:
   - Vol. 2a/2b/2c/2d: command opcodes, enumerations, registers, command structures.
   - Vol. 3/4: GPU overview and supported configurations.
   - Vol. 5/6: memory views and command-stream programming.
   - Vol. 11: blitter engine.
   - Vol. 12: display engine.
   - Vol. 13: MMIO.
   - Vol. 16: workarounds.
2. **Intel graphics document index:** [Intel Processor Graphics developer documentation](https://www.intel.com/content/www/us/en/developer/articles/guide/intel-graphics-developers-guides.html). Use the exact Skylake generation, not a later generation's register descriptions.
3. **Processor datasheet and platform EDS/addenda:** use the documents linked from the exact SKU/platform page above. These establish platform routing, power, PCI/device and interface constraints; they do not replace the graphics PRM.
4. **Specification updates / errata:** use the specification update that matches the actual processor family, package/platform, and stepping. Intel's [6th-generation processor EDS addendum](https://www.intel.com/content/www/us/en/content-details/334408/6th-generation-intel-core-processor-intel-pentium-processor-intel-celeron-processor-and-intel-xeon-processor-e3-v5-family-formerly-known-as-skylake-desktop-workstation-and-mobile-platforms-external-design-specification-addendum.html) is a useful platform reference, but it is not a substitute for the correct processor specification update or PRM Vol. 16.
5. **Cross-checks only:** Linux's i915 source and Intel's Gen8/Gen9 PRMs can help identify intended sequencing and known workarounds. They are comparison material, not permission to copy a later-generation register sequence into Skylake.

## Findings from the current Haiku source

### 1. Ring-buffer timeout was not fail-safe — hardened in this branch

In `src/add-ons/accelerants/intel_extreme/engine.cpp`, `QueueCommands::MakeSpace()` previously logged a one-second stall and then continued as though the request had enough room. That could allow writes to overwrite unconsumed ring-buffer commands after a timeout.

The current branch changes `MakeSpace()` to return success/failure and makes `Put()`, `PutFlush()`, `PutWaitFor()`, and `PutOverlayFlip()` stop when space cannot be acquired. This is a defensive integrity fix, not a GPU recovery/reset implementation. A timed-out command can still be dropped, so callers and synchronization semantics need further work.

### 2. 2D acceleration is not yet proven usable on Skylake

- `sEngineToken` is initialized with capabilities `0 /*B_2D_ACCELERATION*/`. This means the driver is not advertising the 2D acceleration capability, despite individual hook implementations existing. Do not simply flip this bit until command submission, sync, and recovery are validated.
- `intel_wait_engine_idle()` explicitly returns early for `INTEL_FAMILY_LAKE` and `INTEL_FAMILY_SOC0` because the acceleration engine was observed to stall. This is a serious blocker. It needs a register/command-level root-cause investigation against Skylake PRM Vols. 6, 11, 13 and 16 before removing the guard.
- The existing rectangle and screen-to-screen operations use legacy XY blit command structures. Confirm command encodings, supported pixel formats, pitch/address units, clipping, alignment, fence/cache requirements, and valid framebuffer placement against the Skylake blitter PRM before enabling them.
- The `B_FILL_SPAN` hook and its one-row color-blit implementation are present, but must not be treated as proof that hardware acceleration is operational.

### 3. Display detection and EDID/DDC exist, but require targeted verification

`Ports.cpp` already contains EDID/DDC handling, DP AUX/I2C support, VBT-dependent I2C selection/fallback, and DDI port handling. The right next step is to audit sequencing and error reporting against Skylake display-engine documentation rather than replacing these mechanisms wholesale.

Priority checks:
- Verify AUX transactions, I2C-over-AUX, native AUX, retries, timeout cleanup, and link-training state transitions against PRM Vol. 12 and Vol. 16.
- Verify HDMI/DVI DDI mode selection, port/pipe/transcoder routing, DPLL selection, clock programming, lane/PHY setup, and supported color depth. HDMI audio and HDMI 2.0/HDCP features are separate capability paths and must not be inferred from basic video output.
- Verify miniDP as DP signaling on the board's actual DDI routing. A passive adapter does not create support for an otherwise unavailable signal/protocol.
- Preserve the distinction between “EDID read failed” and “monitor disconnected.” VBT and internal-panel fallback behavior should be tested on laptop and desktop configurations separately.
- In `DigitalDisplayInterface::IsConnected()`, review the fallback after EDID detection: its pipe scan appears to return based on the first pipe's enable state rather than establishing that an enabled pipe is routed to the current port. Confirm and correct this logic with a focused test before changing behavior.

### 4. Display modes and multiple monitors

The accelerant creates three pipes for generations 7–11, but pipe allocation alone does not guarantee correct multi-monitor support. Validate pipe/transcoder/port constraints, shared DPLL allocation, bandwidth, link training, and rollback on failure. Test clone/extended configurations, hotplug, suspend/resume, and failed modesets.

### 5. Required test gates before advertising acceleration

- Build the accelerant and kernel driver with Haiku's supported build system and treat warnings/errors as blockers.
- Static checks: command size/alignment, ring bounds, framebuffer pitch and base units, integer overflow, MMIO readback, and every timeout/error path.
- Instrumented hardware tests: register dumps before/after initialization, ring head/tail progress, engine idle behavior, and failure recovery.
- 2D correctness: fills, spans, inversion, overlapping/non-overlapping blits, all supported pixel formats, clipping/edge coordinates, and comparison against software rendering.
- Display matrix: EDID base/extensions/checksums, DDC over I2C and AUX, HDMI, DP/mDP, internal eDP, multiple monitors, hotplug, mode changes, DPMS, and resume.
- Only enable `B_2D_ACCELERATION` after the command stream is shown to complete reliably and software fallback is available for unsupported or failed operations.

## Current status

This branch includes the fill-span hook work and a ring-buffer fail-closed fix. It does **not** yet provide full HD Graphics 530 feature parity, complete errata closure, or verified HDMI/DP/mDP/EDID support. Those require the exact applicable Intel documentation revision plus builds and physical-hardware tests.
