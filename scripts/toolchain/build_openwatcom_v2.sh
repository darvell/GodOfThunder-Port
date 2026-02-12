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

# On macOS it's common to have GNU binutils installed via Homebrew
# (e.g. /opt/homebrew/opt/binutils/bin/ar) earlier in PATH. OpenWatcom's build
# uses `ar` directly, and Apple `ld` expects archives produced by Apple's `/usr/bin/ar`.
# Ensure the system toolchain shims are preferred.
if [[ "$(uname -s)" == "Darwin" ]]; then
  export PATH="/usr/bin:/bin:/usr/sbin:/sbin:$PATH"
fi
if ! command -v make >/dev/null 2>&1; then
  echo "error: make is required"
  exit 1
fi

mkdir -p "$(dirname "$OW2_SRC")"
if [[ ! -d "$OW2_SRC/.git" ]]; then
  echo "[toolchain] cloning open-watcom-v2 into: $OW2_SRC"
  git clone --depth 1 https://github.com/open-watcom/open-watcom-v2 "$OW2_SRC"
else
  echo "[toolchain] open-watcom-v2 already present at: $OW2_SRC"
fi

cd "$OW2_SRC"

# Bypass setvars.sh (it hard-codes OWTOOLS=GCC). We set the relevant vars
# directly and then load the common environment.
export OWROOT="$OW2_SRC"
export OWTOOLS="${OWTOOLS:-CLANG}"
export OWDOCBUILD="${OWDOCBUILD:-0}"
export OWDISTRBUILD="${OWDISTRBUILD:-0}"
export OWGUINOBUILD="${OWGUINOBUILD:-1}"
export OWOBJDIR="${OWOBJDIR:-binbuild}"

# cmnvars.sh isn't written with `set -u` (nounset) in mind.
# Temporarily disable nounset while sourcing it so we don't accidentally
# "define empty" env vars like OWCLANG, which would break the build.
set +u
. "$OWROOT/cmnvars.sh"
set -u

echo "[toolchain] building OpenWatcom v2 (this is a one-time, long build)..."
./build.sh

echo "[toolchain] staging rel/ tree..."
./build.sh rel

if [[ ! -d "$OW2_SRC/rel" ]]; then
  echo "error: expected $OW2_SRC/rel to exist after build"
  exit 1
fi

echo
echo "[toolchain] done"
echo "OpenWatcom v2 rel tree:"
echo "  $OW2_SRC/rel"
echo
echo "Next:"
echo "  GOT_TOOLCHAIN=ow2 ./build.sh"
