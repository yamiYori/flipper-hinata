"""Install pinned Windows build tools inside this project; never flash a device."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import urllib.request
import urllib.error
import venv
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def valid_file(path: Path, spec: dict) -> bool:
    if not path.is_file() or path.stat().st_size != spec["size"]:
        return False
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest() == spec["sha256"]


def download(spec: dict) -> Path:
    folder = ROOT / ".research"
    folder.mkdir(exist_ok=True)
    dest = folder / spec["filename"]
    if valid_file(dest, spec):
        print(f"Verified cached {dest.name}", flush=True)
        return dest
    # Prefer the public release asset: anonymous GitHub API downloads can hit
    # shared-runner rate limits. The API URL remains a network fallback.
    urls = list(dict.fromkeys([spec["url"], spec.get("api_url", spec["url"])]))
    staged = dest.with_suffix(dest.suffix + ".part")
    for index, url in enumerate(urls):
        req = urllib.request.Request(url, headers={"Accept": "application/octet-stream",
                                     "User-Agent": "hinata-usb-build/1.0"})
        print(f"Downloading {dest.name}: {spec['size']} bytes", flush=True)
        try:
            with urllib.request.urlopen(req, timeout=45) as response, staged.open("wb") as output:
                while chunk := response.read(1024 * 1024):
                    output.write(chunk)
        except (urllib.error.URLError, TimeoutError):
            if index + 1 == len(urls):
                raise
            print("Download endpoint unavailable; trying the locked alternate URL", flush=True)
            continue
        # A checksum mismatch is fatal, not a reason to install or reuse data.
        if not valid_file(staged, spec):
            raise RuntimeError(f"Size/SHA256 mismatch: {staged}. File has NOT been installed.")
        break
    staged.replace(dest)
    return dest


def extract_toolchain(archive: Path, target: Path):
    target = target.resolve()
    if not target.is_relative_to(ROOT):
        raise ValueError("Toolchain destination must be inside the project")
    with zipfile.ZipFile(archive) as bundle:
        for item in bundle.infolist():
            parts = PurePosixPath(item.filename).parts
            if not parts or parts[0] != "gcc-arm-none-eabi-12.3-x86_64-windows-flipper":
                raise ValueError("Unexpected toolchain archive root")
            if item.is_dir():
                continue
            dest = target.joinpath(*parts[1:]).resolve()
            if not dest.is_relative_to(target):
                raise ValueError("Unsafe archive entry")
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(bundle.read(item))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", choices=["unleashed", "official"], default="unleashed")
    args = parser.parse_args()
    lock_name = "build-lock-official.json" if args.firmware == "official" else "build-lock.json"
    state = ROOT / (".ufbt-official" if args.firmware == "official" else ".ufbt")
    if os.name != "nt":
        raise SystemExit("This bootstrap pins the Windows toolchain. Use uFBT on other platforms.")
    temp = ROOT / ".cache/tmp"
    temp.mkdir(parents=True, exist_ok=True)
    os.environ["TEMP"] = str(temp)
    os.environ["TMP"] = str(temp)
    os.environ["PIP_CACHE_DIR"] = str(ROOT / ".cache/pip")
    lock = json.loads((ROOT / lock_name).read_text())
    python = ROOT / ".venv/Scripts/python.exe"
    if not python.exists():
        venv.EnvBuilder(with_pip=True).create(ROOT / ".venv")
    subprocess.run([str(python), "-m", "pip", "--disable-pip-version-check", "install", "-r",
                    str(ROOT / "requirements-dev.txt")], check=True)
    sdk = download(lock["sdk"])
    toolchain = download(lock["toolchain"])
    sdk_env = dict(os.environ, UFBT_HOME=str(state))
    subprocess.run([str(python), "-m", "ufbt", "update", "--hw-target", "f7", "--local", str(sdk)],
                   cwd=ROOT, env=sdk_env, check=True)
    target = ROOT / ".ufbt/toolchain/x86_64-windows"
    marker = target / "VERSION"
    if not marker.is_file() or marker.read_text().strip() != lock["toolchain"]["version"] or not (target / "bin/arm-none-eabi-gcc.exe").exists():
        extract_toolchain(toolchain, target)
    print(f"Ready. Run: python tools/build.py --firmware {args.firmware}")


if __name__ == "__main__":
    main()
