"""Build the selected pinned firmware SDK without connecting to a device."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", choices=["unleashed", "official"], default="unleashed")
    args = parser.parse_args()
    lock_name = "build-lock-official.json" if args.firmware == "official" else "build-lock.json"
    lock = json.loads((ROOT / lock_name).read_text())
    state = ROOT / (".ufbt-official" if args.firmware == "official" else ".ufbt")
    temp = ROOT / ".cache/tmp"
    temp.mkdir(parents=True, exist_ok=True)
    os.environ["TEMP"] = str(temp)
    os.environ["TMP"] = str(temp)
    # Both locked SDKs use the same compiler; keep one verified local copy.
    toolchain = ROOT / ".ufbt/toolchain/x86_64-windows"
    python = toolchain / "python/python.exe"
    sdk = state / "current"
    if not python.exists() or not (sdk / "components.json").exists():
        raise SystemExit(f"Run python tools/bootstrap.py --firmware {args.firmware} first.")
    if Path(sys.executable).resolve() != python.resolve():
        return subprocess.call([str(python), str(Path(__file__).resolve()),
                                "--firmware", args.firmware], cwd=ROOT)
    components = json.loads((sdk / "components.json").read_text())
    symbols = sdk / "sdk_headers/f7_sdk/targets/f7/api_symbols.csv"
    with symbols.open(newline="") as stream:
        api = next(row[2] for row in csv.reader(stream) if row[0] == "Version")
    if components["meta"]["version"] != lock["firmware"] or api != lock["api"]:
        raise SystemExit(f"SDK differs from {lock_name}. Re-run bootstrap for this firmware.")
    os.environ["PATH"] = str(toolchain / "bin") + os.pathsep + str(toolchain / "python") + os.pathsep + os.environ["PATH"]
    os.environ["UFBT_STATE_DIR"] = str(state)
    os.environ["UFBT_HOME"] = str(state)
    os.environ["FBT_TOOLCHAIN_PATH"] = str(ROOT / ".ufbt")
    os.environ["UFBT_SCRIPT_DIR"] = str(sdk / "scripts/ufbt")
    sys.argv = ["scons", "-Q", "--warn=target-not-built", "-C", os.environ["UFBT_SCRIPT_DIR"],
                "UFBT_APP_DIR=" + str(ROOT)]
    try:
        runpy.run_module("SCons", run_name="__main__")
    except SystemExit as exc:
        if exc.code:
            return int(exc.code)
    built = state / "build/hinata_usb.fap"
    output = ROOT / "dist" / args.firmware
    output.mkdir(parents=True, exist_ok=True)
    artifact = output / "hinata_usb.fap"
    shutil.copyfile(built, artifact)
    release_name = (f"hinata_usb-official-{lock['firmware']}.fap" if args.firmware == "official"
                    else f"hinata_usb-unleashed-{lock['firmware'].removeprefix('unlshd-')}.fap")
    inputs = [ROOT / "application.fam", ROOT / lock_name, ROOT / "tools/build.py",
              ROOT / "tools/bootstrap.py"] + sorted(p for p in (ROOT / "src").rglob("*") if p.suffix in {".c", ".h"})
    manifest = {"app_version": lock["version"], "firmware_flavor": args.firmware,
                "sdk": lock["firmware"], "api": api, "target": lock["hardware"],
                "filename": artifact.name, "release_filename": release_name,
                "size": artifact.stat().st_size, "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
                "hardware_verified": False,
                "source_sha256": {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    (output / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k:v for k,v in manifest.items() if k != "source_sha256"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
