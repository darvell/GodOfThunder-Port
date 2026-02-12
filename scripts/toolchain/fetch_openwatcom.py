#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import tempfile
import urllib.request
import zipfile
from pathlib import Path


DEFAULT_URL = "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/devel/watcomc.zip"


def download(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "got-rebuild/1.0"})
    with urllib.request.urlopen(req) as r:
        return r.read()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default=DEFAULT_URL, help="OpenWatcom (DOS) zip URL")
    ap.add_argument("--dest", default=Path("ow"), type=Path, help="Destination dir (mounted as C:\\OW)")
    ap.add_argument("--clean", action="store_true", help="Remove dest dir before extracting")
    args = ap.parse_args()

    dest: Path = args.dest
    if args.clean and dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True, exist_ok=True)

    data = download(args.url)

    with tempfile.TemporaryDirectory(prefix="got-ow-") as td:
        zpath = Path(td) / "watcomc.zip"
        zpath.write_bytes(data)
        with zipfile.ZipFile(zpath) as zf:
            zf.extractall(dest)

    print(f"Installed OpenWatcom package to: {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
