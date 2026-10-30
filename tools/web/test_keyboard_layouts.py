"""keyboard_layouts.py against its committed dump and generated module.

usage: test_keyboard_layouts.py <keyboard_layouts.py> <keyboard_layouts.json> <key_layouts.mjs>

- web/key_layouts.mjs is the module generated from the dump, byte for byte;
- the dump's text is what dump_text writes for its own content;
- on Windows: each layout DLL's KbdLayerDescriptor tables answer as the dump (skipped for a DLL
  whose hash differs from the dump's), and each layout of the session answers through the API
  (MapVirtualKeyExW, ToAsciiEx, ToUnicodeEx) as the dump; no layout is loaded or activated.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

SCRIPT, DUMP, MJS = (Path(p).resolve() for p in sys.argv[1:4])
spec = importlib.util.spec_from_file_location("keyboard_layouts", SCRIPT)
kl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kl)

failures = 0
data = json.loads(DUMP.read_text(encoding="utf-8"))
if DUMP.read_bytes() != kl.dump_text(data).encode():
    print("FAIL the dump is not in dump_text's form")
    failures += 1
if sorted(data["layouts"]) != ["de", "fr", "us"]:
    print("FAIL the dump's layouts")
    failures += 1
for name, entry in data["layouts"].items():
    if entry["source"] == "api" and not entry.get("tables_agree"):
        print("FAIL %s: the API dump disagrees with the tables" % name)
        failures += 1
if kl.check(argparse.Namespace(dump=str(DUMP), mjs=str(MJS))):
    print("FAIL check")
    failures += 1
print("keyboard_layouts: %d failed" % failures)
sys.exit(1 if failures else 0)
