#!/usr/bin/env bash

set -euo pipefail

# Wrapper around the Python implementation (avoids curl/rm portability issues).
ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

if command -v uv >/dev/null 2>&1; then
  exec uv run python "$ROOT_DIR/scripts/toolchain/fetch_jwasm.py" --dest "$ROOT_DIR/tasm"
else
  exec python3 "$ROOT_DIR/scripts/toolchain/fetch_jwasm.py" --dest "$ROOT_DIR/tasm"
fi
