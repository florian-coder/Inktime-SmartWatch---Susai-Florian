#!/usr/bin/env python3
"""Fail the build if the firmware outgrows its flash / RAM budget.

usage: check_size.py arm-none-eabi-size firmware.elf
"""
import subprocess
import sys

# Budgets leave room for a BLE stack (SoftDevice S140 needs ~150 KiB flash, ~20 KiB RAM).
FLASH_BUDGET = 256 * 1024
RAM_BUDGET = 64 * 1024


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    out = subprocess.run([sys.argv[1], "-A", sys.argv[2]], capture_output=True, text=True, check=True).stdout
    sections = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0].startswith("."):
            sections[parts[0]] = (int(parts[1]), int(parts[2]))
    flash = sum(size for name, (size, _) in sections.items()
                if name in (".isr_vector", ".text", ".ARM.exidx", ".data"))
    ram = sum(size for name, (size, _) in sections.items() if name in (".data", ".bss"))
    ok = flash <= FLASH_BUDGET and ram <= RAM_BUDGET
    print(f"flash {flash:7d} / {FLASH_BUDGET} bytes ({100 * flash / FLASH_BUDGET:5.1f}% of budget)")
    print(f"ram   {ram:7d} / {RAM_BUDGET} bytes ({100 * ram / RAM_BUDGET:5.1f}% of budget, + 8 KiB stack)")
    print("size: OK" if ok else "size: OVER BUDGET")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
