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

def fail(message: str) -> None:
    print(f"FAIL: {message}")
    sys.exit(1)

driver = DRIVER.read_text(encoding="utf-8")
hooks = HOOKS.read_text(encoding="utf-8")
mode_source = MODE_SOURCE.read_text(encoding="utf-8")

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

# A failed per-port modeset must not be silently reported as success.
set_mode_match = re.search(
    r"status_t\\s+intel_set_display_mode\\s*\\(.*?(?=\\nstatus_t\\s+intel_get_display_mode\\s*\\()",
    mode_source,
    re.DOTALL,
)
if set_mode_match is None:
    fail("intel_set_display_mode function was not found")
set_mode_block = set_mode_match.group(0)
for required in ("status_t portStatus = B_OK;", "portStatus = status;", "return portStatus;"):
    if required not in set_mode_block:
        fail("display mode failure propagation is missing: " + required)

print("PASS: Rocket Lake IDs retained; Alchemist/Battlemage remain unclaimed; CPU-only 2D and engine/sync hooks fail closed; port modeset errors propagate.")
print("NOTE: source checks only; build and physical display validation are still required.")
