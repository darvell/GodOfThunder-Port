#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

OW2_SRC_DEFAULT="$ROOT_DIR/build/toolchain/open-watcom-v2"
OW2_SRC="${OW2_SRC:-$OW2_SRC_DEFAULT}"

if ! command -v git >/dev/null 2>&1; then
  echo "error: git is required to fetch OpenWatcom v2"
  exit 1
fi
if ! command -v clang >/dev/null 2>&1; then
  echo "error: clang is required (on macOS: install Xcode Command Line Tools)"
  echo "hint: xcode-select --install"
  exit 1
fi
if ! command -v make >/dev/null 2>&1; then
  echo "error: make is required"
  exit 1
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  # Prefer Apple toolchain shims over Homebrew binutils.
  export PATH="/usr/bin:/bin:/usr/sbin:/sbin:$PATH"
fi

mkdir -p "$(dirname "$OW2_SRC")"
if [[ ! -d "$OW2_SRC/.git" ]]; then
  echo "[toolchain] cloning open-watcom-v2 into: $OW2_SRC"
  git clone --depth 1 https://github.com/open-watcom/open-watcom-v2 "$OW2_SRC"
else
  echo "[toolchain] open-watcom-v2 already present at: $OW2_SRC"
fi

cd "$OW2_SRC"

export OWROOT="$OW2_SRC"
export OWTOOLS="${OWTOOLS:-CLANG}"
export OWDOCBUILD=0
export OWDISTRBUILD=0
export OWGUINOBUILD=1
export OWOBJDIR="${OWOBJDIR:-binbuild}"

set +u
. "$OWROOT/cmnvars.sh"
set -u

echo "[toolchain] building OpenWatcom v2 bootstrap host tools..."
./build.sh boot

if [[ ! -x "$OW2_SRC/build/binbuild/bwcc" || ! -x "$OW2_SRC/build/binbuild/bwasm" || ! -x "$OW2_SRC/build/binbuild/bwlink" ]]; then
  echo "error: expected bootstrap tools under:"
  echo "  $OW2_SRC/build/binbuild"
  echo "expected at least: bwcc, bwasm, bwlink"
  exit 1
fi

echo
echo "[toolchain] done"
echo "Bootstrap tools:"
echo "  $OW2_SRC/build/binbuild"
echo
echo "Next:"
echo "  ./build.sh host"

