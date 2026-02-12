#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

EP="${1:-g1}"
shift || true

case "$EP" in
  g1|1) EXE_DOS="src\\_g1\\_G1.EXE" ;;
  g2|2) EXE_DOS="src\\_g2\\_G2.EXE" ;;
  g3|3) EXE_DOS="src\\_g3\\_G3.EXE" ;;
  *)
    echo "usage: ./run.sh {g1|g2|g3} [game args...]"
    echo "examples:"
    echo "  ./run.sh g1"
    echo "  ./run.sh g1 /NOAL"
    echo "  ./run.sh g2 /S:23"
    exit 2
    ;;
esac

if [[ ! -f "$ROOT_DIR/GOTRES.DAT" ]]; then
  echo "error: missing GOTRES.DAT in repo root"
  exit 1
fi

# Some tools hard-code \\GOT\\GOTRES.DAT. Make that path exist when the repo
# root is mounted as C: in DOSBox.
mkdir -p "$ROOT_DIR/got"
if [[ ! -e "$ROOT_DIR/got/GOTRES.DAT" ]]; then
  ln -s ../GOTRES.DAT "$ROOT_DIR/got/GOTRES.DAT" 2>/dev/null || true
fi

DOSBOX_BIN="${DOSBOX_BIN:-}"
if [[ -z "$DOSBOX_BIN" ]]; then
  if [[ -x "/Applications/dosbox.app/Contents/MacOS/DOSBox" ]]; then
    DOSBOX_BIN="/Applications/dosbox.app/Contents/MacOS/DOSBox"
  elif command -v dosbox-x >/dev/null 2>&1; then
    DOSBOX_BIN="$(command -v dosbox-x)"
  elif command -v dosbox >/dev/null 2>&1; then
    DOSBOX_BIN="$(command -v dosbox)"
  fi
fi

if [[ -z "$DOSBOX_BIN" ]]; then
  echo "error: no DOSBox binary found (checked: dosbox-x, /Applications/dosbox.app, dosbox)"
  echo "hint: install via Homebrew: brew install --cask dosbox (or brew install dosbox-x)"
  exit 1
fi

CONF="${DOSBOX_CONF:-$ROOT_DIR/run.conf}"
if [[ ! -f "$CONF" ]]; then
  echo "error: DOSBox config not found: $CONF"
  exit 1
fi

# Run from C:\ so GOTRES.DAT is found via relative path.
ARGS_DOS=""
if [[ $# -gt 0 ]]; then
  # DOSBox passes args verbatim; keep it simple and don't attempt complex quoting.
  ARGS_DOS=" $*"
fi

exec "$DOSBOX_BIN" -conf "$CONF" \
  -c "mount c \"$ROOT_DIR\"" \
  -c "c:" \
  -c "cd \\\\" \
  -c "$EXE_DOS$ARGS_DOS" \
  -c "exit"
