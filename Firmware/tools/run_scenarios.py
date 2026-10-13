#!/usr/bin/env python3
"""Run every tests/scenarios/*.scn through the simulator, one process per scenario.

usage: run_scenarios.py SIM_BIN SCENARIO_DIR GOLDEN_DIR OUT_DIR [--update-golden]

Writes OUT_DIR/scenarios.xml (JUnit) and converts failing frames to PNG.
"""
import os
import subprocess
import sys
import time
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))


def to_png(pbm):
    png = pbm[:-4] + ".png"
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "pbm2png.py"), pbm, png, "--scale", "2"]
                   + (["--diff"] if pbm.endswith(".diff.pbm") else []), check=False)
    return png


def main():
    args = sys.argv[1:]
    update = "--update-golden" in args
    args = [a for a in args if a != "--update-golden"]
    if len(args) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    sim, scen_dir, golden_dir, out_dir = args
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(golden_dir, exist_ok=True)

    results = []
    for name in sorted(f for f in os.listdir(scen_dir) if f.endswith(".scn")):
        path = os.path.join(scen_dir, name)
        cmd = [sim, "--scenario", path, "--golden", golden_dir, "--out", out_dir]
        if update:
            cmd.append("--update-golden")
        t0 = time.time()
        proc = subprocess.run(cmd, capture_output=True, text=True)
        dt = time.time() - t0
        ok = proc.returncode == 0
        results.append((name, ok, dt, proc.stdout + proc.stderr))
        print(f"{'PASS' if ok else 'FAIL'}  {name:28s} {dt:6.2f}s")
        if not ok:
            for line in (proc.stdout + proc.stderr).splitlines():
                if "FAILED" in line or "error" in line.lower():
                    print("      " + line)

    for f in os.listdir(out_dir):
        if f.endswith((".actual.pbm", ".diff.pbm")):
            to_png(os.path.join(out_dir, f))

    failed = [r for r in results if not r[1]]
    with open(os.path.join(out_dir, "scenarios.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write(f'<testsuite name="inktime-scenarios" tests="{len(results)}" failures="{len(failed)}">\n')
        for name, ok, dt, out in results:
            f.write(f'  <testcase classname="scenarios" name="{escape(name)}" time="{dt:.3f}"')
            if ok:
                f.write("/>\n")
            else:
                f.write(f'>\n    <failure message="scenario failed">{escape(out)}</failure>\n  </testcase>\n')
        f.write("</testsuite>\n")

    print(f"\n{len(results)} scenarios, {len(results) - len(failed)} passed, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
