#!/usr/bin/env python3
"""Source-level guardrails for Rocket Lake, Alchemist, and Battlemage.

Run from the Haiku source root:
  python3 src/add-ons/accelerants/intel_extreme/tests/validate_rkl_alchemist_battlemage.py

This is not a build or hardware test. It ensures Rocket Lake's existing allowlist
entries remain present, the unimplemented discrete families remain unclaimed, and
the CPU-only 2D hooks stay disabled.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[5]
DRIVER = ROOT / "src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp"
HOOKS = ROOT / "src/add-ons/accelerants/intel_extreme/hooks.cpp"
MODE_SOURCE = ROOT / "src/add-ons/accelerants/intel_extreme/mode.cpp"
CURSOR_SOURCE = ROOT / "src/add-ons/accelerants/intel_extreme/cursor.cpp"
PLL_SOURCE = ROOT / "src/add-ons/accelerants/intel_extreme/TigerLakePLL.cpp"
PORT_SOURCE = ROOT / "src/add-ons/accelerants/intel_extreme/Ports.cpp"

def fail(message: str) -> None:
    print(f"FAIL: {message}")
    sys.exit(1)

driver = DRIVER.read_text(encoding="utf-8")
hooks = HOOKS.read_text(encoding="utf-8")
mode_source = MODE_SOURCE.read_text(encoding="utf-8")
cursor_source = CURSOR_SOURCE.read_text(encoding="utf-8")
pll_source = PLL_SOURCE.read_text(encoding="utf-8")
port_source = PORT_SOURCE.read_text(encoding="utf-8")

# Catch duplicate active PCI IDs in the supported-device table. Entries inside
# #if 0 blocks are intentionally excluded because they are not compiled.
table_match = re.search(
    r"kSupportedDevices\s*\[\]\s*=\s*\{(.*?)^\};",
    driver,
    re.DOTALL | re.MULTILINE,
)
if table_match is None:
    fail("supported-device table was not found")
active_table = re.sub(
    r"^\s*#if\s+0\b.*?^\s*#endif\b[^\n]*",
    "",
    table_match.group(1),
    flags=re.DOTALL | re.MULTILINE,
)
device_ids = re.findall(r"^\s*\{\s*(0x[0-9a-fA-F]+)\s*,", active_table, re.MULTILINE)
seen_ids = set()
duplicate_ids = set()
for device_id in device_ids:
    normalized = device_id.lower()
    if normalized in seen_ids:
        duplicate_ids.add(normalized)
    seen_ids.add(normalized)
if duplicate_ids:
    fail("duplicate active PCI device IDs: " + ", ".join(sorted(duplicate_ids)))


for device_id in ("0x4c8a", "0x4c8b", "0x4c90", "0x4c9a"):
    if not re.search(
        r"\{\s*" + re.escape(device_id) + r"\s*,\s*INTEL_MODEL_RKL\s*,",
        active_table,
        re.IGNORECASE,
    ):
        fail(f"Rocket Lake allowlist entry {device_id} is missing or not mapped to INTEL_MODEL_RKL")

# Keep exact known Alchemist/Battlemage IDs unclaimed until their display-engine
# model and register paths are implemented and reviewed.
for device_id in (
    # DG2 IDs from the current upstream Linux Xe PCI-ID macro groups.
    "0x56a0", "0x56a1", "0x56a2", "0x56a3", "0x56a4", "0x56a5", "0x56a6",
    "0x5690", "0x5691", "0x5692", "0x5693", "0x5694", "0x5695", "0x5696", "0x5697",
    "0x56b0", "0x56b1", "0x56b2", "0x56b3",
    "0x56ba", "0x56bb", "0x56bc", "0x56bd", "0x56be", "0x56bf",
    # Battlemage IDs from INTEL_BMG_G21_IDS and INTEL_BMG_IDS.
    "0xe202", "0xe209", "0xe20b", "0xe20c", "0xe20d", "0xe210",
    "0xe211", "0xe212", "0xe216", "0xe220", "0xe221", "0xe222", "0xe223",
):
    if re.search(r"\{\s*" + re.escape(device_id) + r"\s*,", driver, re.IGNORECASE):
        fail(f"unvalidated Alchemist/Battlemage device {device_id} was allowlisted")

for feature in (
    "B_SCREEN_TO_SCREEN_BLIT",
    "B_FILL_RECTANGLE",
    "B_INVERT_RECTANGLE",
    "B_FILL_SPAN",
):
    match = re.search(
        r"case\s+" + feature + r"\s*:(.*?)(?=\n\s*case\s+[A-Z_]+|\n\s*default\s*:)",
        hooks,
        re.DOTALL,
    )
    if match is None or not re.search(r"return\s+NULL\s*;", match.group(1)):
        fail(f"{feature} is not visibly fail-closed")

# Engine and synchronization hooks must also remain unavailable in CPU-only mode.
engine_features = (
    "B_ACCELERANT_ENGINE_COUNT",
    "B_ACQUIRE_ENGINE",
    "B_RELEASE_ENGINE",
    "B_WAIT_ENGINE_IDLE",
    "B_GET_SYNC_TOKEN",
    "B_SYNC_TO_TOKEN",
)
engine_block = re.search(
    r"case\s+B_ACCELERANT_ENGINE_COUNT\s*:(.*?)(?=\n\s*/\* 2D acceleration \*/)",
    hooks,
    re.DOTALL,
)
if engine_block is None:
    fail("engine/synchronization hook block was not found")
for feature in engine_features:
    if not re.search(r"case\s+" + feature + r"\s*:", engine_block.group(0)):
        fail(f"{feature} hook case is missing")
if not re.search(r"return\s+NULL\s*;", engine_block.group(1)):
    fail("engine/synchronization hooks are not visibly fail-closed")

# Gen12's legacy cursor register path is not yet validated; keep the hooks gated.
cursor_hooks = re.search(
    r"/\* cursor management \*/(.*?)(?=/\* engine/synchronization \*/)",
    hooks,
    re.DOTALL | re.IGNORECASE,
)
if cursor_hooks is None or cursor_hooks.group(1).count("Generation() < 12") < 3:
    fail("hardware cursor hooks are not gated away from unvalidated Gen12")
for required in ("width == 0", "height == 0", "andMask == NULL", "xorMask == NULL"):
    if required not in cursor_source:
        fail("cursor input validation is missing: " + required)

# Gen6+ overlay hooks must stay fail-closed until a modern plane path exists.
overlay_gate = re.search(
    r"if \(gInfo->shared_info->device_type.Generation\(\) >= 6\)\s*\{(.*?)\n\s*\}",
    hooks,
    re.DOTALL,
)
if overlay_gate is None or "B_CONFIGURE_OVERLAY" not in overlay_gate.group(1) or "return NULL;" not in overlay_gate.group(1):
    fail("legacy overlay hooks are not visibly disabled on Gen6+")

# PLL waits must be bounded and caller-visible failures must stop the modeset.
if "while ((read32(DPLL_ENABLE)" in pll_source:
    fail("unbounded Gen12 PLL polling loop remains")
for required in ("wait_for_register(DPLL_ENABLE, TGL_DPLL_LOCK, false, 100000)",
                 "wait_for_register(DPLL_ENABLE, TGL_DPLL_POWER_STATE, true, 100000)",
                 "wait_for_register(DPLL_ENABLE, TGL_DPLL_LOCK, true, 100000)"):
    if required not in pll_source:
        fail("bounded PLL transition wait is missing: " + required)
if "Undo the power request if hardware did not acknowledge it" not in pll_source:
    fail("PLL power-up timeout must roll back the power request")
if "Do not leave an unlocked PLL enabled" not in pll_source:
    fail("PLL lock timeout must attempt bounded shutdown")
if not re.search(r"status_t\s+pllStatus\s*=\s*ProgramPLL\(", port_source) or "return pllStatus;" not in port_source:
    fail("Gen12 PLL programming errors are not propagated to the modeset caller")

# Gen12 DPLL programming must validate arithmetic inputs and normalize a
# rounded fractional carry before writing the DCO register.
for required in (
    "Pdiv != 2 && Pdiv != 3 && Pdiv != 5 && Pdiv != 7",
    "ref_khz != 19200 && ref_khz != 24000",
    "if (dco_frac >= (1 << 15))",
    "dco_int++;",
    "dco_frac = 0;",
    "!(dco >= 7998.0f && dco <= 10000.0f)",
    "float afeclk = (5.0f * freq) / 1000.0f;",
):
    if required not in pll_source:
        fail("Gen12 PLL parameter validation is missing: " + required)
if "TGL_DPLL_DCO_INTEGER_SHIFT" in pll_source:
    fail("PLL code references an undefined DCO integer shift macro")

# The current Gen12 combo-PHY implementation is intentionally limited to
# DDI A/B; ports using TC/MG PHYs must not be assigned DPLL0 by heuristic.
ddi_start = port_source.find("DigitalDisplayInterface::SetDisplayMode(")
if ddi_start < 0:
    fail("DigitalDisplayInterface::SetDisplayMode body was not found")
# SetDisplayMode is the final DigitalDisplayInterface method in Ports.cpp.
ddi_mode = port_source[ddi_start:]
if not re.search(
    r"Generation\(\) >= 12.*?PortIndex\(\) != INTEL_PORT_A"
    r".*?PortIndex\(\) != INTEL_PORT_B.*?return B_NOT_SUPPORTED;",
    ddi_mode,
    re.DOTALL,
):
    fail("Gen12 DDI modeset must fail closed for unimplemented PHY ports")
if "fPipe->Index() != INTEL_PIPE_A" not in ddi_mode or "fPipe->Index() != INTEL_PIPE_B" not in ddi_mode:
    fail("Gen12 modeset must stay on the pipes supported by framebuffer-base programming")
if "PortIndex() == INTEL_PORT_B ? 1 : 0" not in ddi_mode:
    fail("Gen12 DDI A/B must select their matching combo DPLL")
if "validate_gen12_dp_bandwidth(fPipe, hardwareTarget)" not in ddi_mode:
    fail("Gen12 DP bandwidth must be checked before modeset register writes")
if "_ValidateGen12DpLink()" not in ddi_mode or "return dpLinkStatus;" not in ddi_mode:
    fail("Gen12 DP modeset must propagate trained-link preflight errors")
for required in ("message.address = 0x100", "message.address = 0x202",
                 "linkConfig[0] != 0x0a", "laneStatus & 0x07) != 0x07",
                 "linkStatus[2] & 0x01"):
    if required not in port_source:
        fail("fixed-rate Gen12 DP trained-link validation is missing: " + required)
if "mode != PIPE_DDI_MODE_DP_SST && mode != PIPE_DDI_MODE_DP_MST" not in port_source:
    fail("reserved DDI mode encodings must be rejected")

# Keep Gen12-specific checks out of the legacy DisplayPort Gen4 routine, which
# has no DDI mode selector variable.
legacy_dp_start = port_source.find("DisplayPort::_SetPortLinkGen4(")
legacy_dp_end = port_source.find("DisplayPort::_SetPortLinkGen6(", legacy_dp_start)
if legacy_dp_start < 0 or legacy_dp_end <= legacy_dp_start:
    fail("could not isolate legacy DP Gen4 link helper")
if "mode == PIPE_DDI_MODE_DP_SST" in port_source[legacy_dp_start:legacy_dp_end]:
    fail("Gen12 DDI mode logic was inserted into the legacy DP Gen4 helper")
ddi_link_start = port_source.find("DigitalDisplayInterface::_SetPortLinkGen8(")
ddi_mode_start = port_source.find("DigitalDisplayInterface::SetDisplayMode(", ddi_link_start)
if ddi_link_start < 0 or ddi_mode_start <= ddi_link_start:
    fail("could not isolate DDI Gen8+ link M/N helper")
ddi_link = port_source[ddi_link_start:ddi_mode_start]
if "Mode exceeds fixed Gen12 DP link capacity" not in ddi_link:
    fail("DDI link helper must reject over-capacity Gen12 DP modes")
if "status_t linkStatus = _SetPortLinkGen8(" not in ddi_mode or "return linkStatus;" not in ddi_mode:
    fail("DDI link M/N programming errors must propagate")

# The DDI DP width field encodes 1/2/4 lanes, not a linear count.
if not all(token in port_source for token in (
    "case PIPE_DDI_DP_WIDTH_1:",
    "case PIPE_DDI_DP_WIDTH_2:",
    "case PIPE_DDI_DP_WIDTH_4:",
    "Mode exceeds fixed Gen12 DP link capacity",
)):
    fail("DDI lane encodings and fixed-rate Gen12 DP bandwidth must be validated")

# DDI clock selection updates must clear only the selected port's field, not
# mask away unrelated clock selections and clock-off bits.
for unsafe in ("config &= TGL_DPCLKA_DDIA_CLOCK_SELECT;",
               "config &= TGL_DPCLKA_DDIB_CLOCK_SELECT;"):
    if unsafe in port_source:
        fail("DDI clock routing update destroys unrelated CFGCR0 fields: " + unsafe)
for required in ("config &= ~TGL_DPCLKA_DDIA_CLOCK_SELECT;",
                 "config &= ~TGL_DPCLKA_DDIB_CLOCK_SELECT;"):
    if required not in port_source:
        fail("DDI clock routing must preserve unrelated CFGCR0 fields: " + required)
# A failed per-port modeset must not be silently reported as success.
set_mode_match = re.search(
    r"status_t\s+intel_set_display_mode\s*\(.*?(?=\nstatus_t\s+intel_get_display_mode\s*\()",
    mode_source,
    re.DOTALL,
)
if set_mode_match is None:
    fail("intel_set_display_mode function was not found")
set_mode_block = set_mode_match.group(0)
for required in ("status_t portStatus = B_OK;", "portStatus = status;", "return portStatus;"):
    if required not in set_mode_block:
        fail("display mode failure propagation is missing: " + required)

print("PASS: Rocket Lake IDs retained; Alchemist/Battlemage remain unclaimed; CPU-only 2D and engine/sync hooks fail closed; cursor/overlay Gen12 gates retained; PLL waits bounded; DCO arithmetic, DDI A/B routing, link errors, lane encoding and DP bandwidth guarded; cursor/overlay restrictions and modeset error propagation retained.")
print("NOTE: source checks only; build and physical display validation are still required.")
