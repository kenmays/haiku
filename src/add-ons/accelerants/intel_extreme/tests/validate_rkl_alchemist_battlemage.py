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

def fail(message: str) -> None:
    print(f"FAIL: {message}")
    sys.exit(1)

driver = DRIVER.read_text(encoding="utf-8")
hooks = HOOKS.read_text(encoding="utf-8")

for device_id in ("0x4c8a", "0x4c8b", "0x4c90", "0x4c9a"):
    if device_id not in driver.lower():
        fail(f"Rocket Lake allowlist entry {device_id} disappeared")

# Keep exact known Alchemist/Battlemage IDs unclaimed until their display-engine
# model and register paths are implemented and reviewed.
for device_id in (
    "0x56a0", "0x56a1", "0x56a2", "0x56a5", "0x56a6",
    "0x5690", "0x5691", "0x5692", "0x5693", "0x5694",
    "0x56b0", "0x56b1", "0x56b2", "0x56b3",
    "0xe20b", "0xe20c", "0xe211", "0xe212", "0xe223", "0xe222",
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

print("PASS: Rocket Lake IDs retained; Alchemist/Battlemage remain unclaimed; CPU-only 2D hooks fail closed.")
print("NOTE: source checks only; build and physical display validation are still required.")
