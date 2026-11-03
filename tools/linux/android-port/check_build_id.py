#!/usr/bin/env python3
"""Is this dump's build ID one the address tables cover?

runtime/game_addresses/versions.h is explicit that an unknown build ID is refused with no fallback table, so
checking first turns a 20-minute failure into a one-second answer. The ID lives at 0x40..0x53 of exefs/main.
"""
import pathlib, re, sys

exefs = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted/exefs")
repo = pathlib.Path(__file__).resolve().parents[3]

bid = (exefs / "main").read_bytes()[0x40:0x54].hex().upper()
versions = (repo / "runtime/game_addresses/versions.h").read_text()
known = re.findall(r'GA_VERSION\((\w+),\s*"([^"]+)",\s*"([0-9A-F]+)"\)', versions)

print(f"exefs/main build ID: {bid}\n")
hit = None
for key, label, want in known:
    match = bid.startswith(want)
    if match:
        hit = label
    print(f"  {key:8} {label:18} {want}  {'<== MATCH' if match else ''}")

print()
if hit:
    print(f"SUPPORTED: {hit}")
else:
    print("NOT SUPPORTED: no address table for this build - the generator would refuse it")
    sys.exit(1)
