#!/usr/bin/env python3

from __future__ import annotations

import argparse
import io
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path


# The upstream japheth.de download often blocks automated fetches (HTTP 403).
# Use a FreeDOS repository mirror instead.
DEFAULT_URL = "https://ftp.icm.edu.pl/packages/freedos/repositories/latest/devel/jwasm/20250301.0/jwasm.zip"


def download(url: str) -> bytes:
    with urllib.request.urlopen(url) as r:
        return r.read()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default=DEFAULT_URL, help="JWasm zip URL")
    ap.add_argument(
        "--dest",
        default=Path("tasm"),
        type=Path,
        help="Destination directory (mounted as C:\\TASM inside DOSBox)",
    )
    args = ap.parse_args()

    dest_dir: Path = args.dest
    dest_dir.mkdir(parents=True, exist_ok=True)

    data = download(args.url)

    with tempfile.TemporaryDirectory(prefix="got-jwasm-") as td:
        td_path = Path(td)
        zpath = td_path / "jwasm.zip"
        zpath.write_bytes(data)

        with zipfile.ZipFile(zpath) as zf:
            zf.extractall(td_path / "unz")

        extracted_root = td_path / "unz"
        exe_paths = list(extracted_root.rglob("*.exe"))
        if not exe_paths:
            raise SystemExit("no .exe found in JWasm archive")

        # Copy all EXEs into dest dir (flat), plus a couple of docs if present.
        for p in exe_paths:
            shutil.copy2(p, dest_dir / p.name.lower())

        for doc in extracted_root.rglob("*"):
            if doc.is_file() and doc.suffix.lower() in (".txt", ".md", ".doc"):
                shutil.copy2(doc, dest_dir / doc.name)

    # Normalize expected filename used by the Makefiles.
    jwasm = dest_dir / "jwasm.exe"
    if not jwasm.exists():
        # Pick first exe and copy it as jwasm.exe
        src = sorted(dest_dir.glob("*.exe"))[0]
        shutil.copy2(src, jwasm)

    print(f"Installed JWasm to: {dest_dir}")
    print(f"Expect DOS path: C:\\TASM\\{jwasm.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
