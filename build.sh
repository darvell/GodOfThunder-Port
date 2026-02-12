#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

TOOLCHAIN="dosbox"
DO_CLEAN=0
for arg in "$@"; do
  case "$arg" in
    clean) DO_CLEAN=1 ;;
    host|ow2) TOOLCHAIN="ow2" ;;
    dos|dosbox) TOOLCHAIN="dosbox" ;;
    "") ;;
    *)
      echo "error: unknown arg: $arg"
      echo "usage:"
      echo "  ./build.sh [clean]               # DOSBox + DOS OpenWatcom (existing)"
      echo "  ./build.sh host [clean]          # native OpenWatcom v2 (fast iteration)"
      echo
      echo "toolchain setup (one-time):"
      echo "  scripts/toolchain/build_openwatcom_v2.sh"
      exit 2
      ;;
  esac
done

mkdir -p "$ROOT_DIR/build"

if [[ "$TOOLCHAIN" == "ow2" ]]; then
  #############################################################################
  # Native OpenWatcom v2 host build (fast iteration, no DOSBox required)
  #############################################################################

  cd "$ROOT_DIR"

  # Toolchain layout options:
  # - A full OpenWatcom v2 build produces a `rel/` tree with `wcc/wasm/wlink`.
  # - The much faster "bootstrapped" OpenWatcom v2 build produces host tools
  #   under `build/binbuild/` (bwcc/bwasm/bwlink), which are sufficient for
  #   building this project when paired with a DOS OpenWatcom headers/libs tree.

  OW2_REL="${OW2_REL:-}"
  OW2_BIN_DIR="${OW2_BIN_DIR:-}"
  if [[ -z "$OW2_BIN_DIR" && -d "$ROOT_DIR/build/toolchain/open-watcom-v2/rel" ]]; then
    OW2_REL_DEFAULT="$ROOT_DIR/build/toolchain/open-watcom-v2/rel"
    OW2_BIN_DIR="$(find "$OW2_REL_DEFAULT" -maxdepth 2 -type f -name wcc -perm -111 2>/dev/null | head -n 1 | xargs -I{} dirname "{}")"
    if [[ -z "$OW2_REL" ]]; then OW2_REL="$OW2_REL_DEFAULT"; fi
  fi
  if [[ -z "$OW2_BIN_DIR" && -x "$ROOT_DIR/build/toolchain/open-watcom-v2/build/binbuild/bwcc" ]]; then
    OW2_BIN_DIR="$ROOT_DIR/build/toolchain/open-watcom-v2/build/binbuild"
  fi
  if [[ -z "$OW2_BIN_DIR" ]]; then
    echo "error: OpenWatcom v2 host tools not found."
    echo "expected either:"
    echo "  build/toolchain/open-watcom-v2/rel/.../wcc"
    echo "or (bootstrap):"
    echo "  build/toolchain/open-watcom-v2/build/binbuild/bwcc"
    echo
    echo "hint: run one of:"
    echo "  scripts/toolchain/build_openwatcom_v2.sh          # full toolchain"
    echo "  (or) cd build/toolchain/open-watcom-v2 && ./build.sh boot"
    exit 1
  fi
  export PATH="$OW2_BIN_DIR:$PATH"

  # Default headers/libs:
  # - If OW2_REL is set, use that.
  # - Otherwise reuse the vendored DOS OpenWatcom tree under ow/ (already used by DOSBox builds).
  if [[ -z "${WATCOM:-}" ]]; then
    if [[ -n "$OW2_REL" ]]; then
      export WATCOM="$OW2_REL"
    else
      export WATCOM="$ROOT_DIR/ow/DEVEL/WATCOMC"
    fi
  fi
  if [[ ! -d "$WATCOM" ]]; then
    echo "error: WATCOM tree not found:"
    echo "  $WATCOM"
    echo "hint: fetch the DOS OpenWatcom headers+libs into ./ow first:"
    echo "  uv run python scripts/toolchain/fetch_openwatcom.py --dest ow"
    exit 1
  fi

  # Find headers + DOS target headers in either lowercase or DOS-style uppercase layout.
  WATCOM_H=""
  WATCOM_H_DOS=""
  WATCOM_LIB_DOS=""
  if [[ -d "$WATCOM/h" ]]; then
    WATCOM_H="$WATCOM/h"
    WATCOM_H_DOS="$WATCOM/h/dos"
    WATCOM_LIB_DOS="$WATCOM/lib286/dos"
  elif [[ -d "$WATCOM/H" ]]; then
    WATCOM_H="$WATCOM/H"
    WATCOM_H_DOS="$WATCOM/H/DOS"
    WATCOM_LIB_DOS="$WATCOM/LIB286/DOS"
  else
    echo "error: could not find OpenWatcom headers under WATCOM:"
    echo "  $WATCOM"
    echo "expected either:"
    echo "  $WATCOM/h"
    echo "or:"
    echo "  $WATCOM/H"
    exit 1
  fi

  if [[ -d "$WATCOM/eddat" ]]; then
    export EDPATH="$WATCOM/eddat"
  elif [[ -d "$WATCOM/EDDAT" ]]; then
    export EDPATH="$WATCOM/EDDAT"
  fi

  # OpenWatcom on POSIX uses ':'-separated INCLUDE/LIB.
  # Use absolute project include paths since we `cd` into subdirs for compiling.
  export INCLUDE="$ROOT_DIR/src/owcompat:$ROOT_DIR/src/utility:$ROOT_DIR/src/digisnd:$WATCOM_H:$WATCOM_H_DOS"
  export LIB="$WATCOM_LIB_DOS"

  OWCC_BIN="wcc"
  OWASM_BIN="wasm"
  OWLINK_BIN="wlink"
  if ! command -v "$OWCC_BIN" >/dev/null 2>&1; then OWCC_BIN="bwcc"; fi
  if ! command -v "$OWASM_BIN" >/dev/null 2>&1; then OWASM_BIN="bwasm"; fi
  if ! command -v "$OWLINK_BIN" >/dev/null 2>&1; then OWLINK_BIN="bwlink"; fi
  for tool in "$OWCC_BIN" "$OWASM_BIN" "$OWLINK_BIN"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      echo "error: missing required tool in PATH: $tool"
      echo "PATH includes:"
      echo "  $OW2_BIN_DIR"
      exit 1
    fi
  done

  OWCC=("$OWCC_BIN" -bt=dos -mm -2 -ze -j -q -od -fi=owpre.h)
  OWASM=("$OWASM_BIN" -bt=dos -2 -zcm=tasm -zq)

  HOST_BUILD_LOG="$ROOT_DIR/build/BUILD_HOST.TXT"
  : >"$HOST_BUILD_LOG"

  log() { printf "%s\n" "$*" | tee -a "$HOST_BUILD_LOG" >/dev/null; }

  host_clean_dir() {
    local dir="$1"
    rm -f "$dir"/*.obj "$dir"/*.OBJ "$dir"/*.err "$dir"/*.ERR "$dir"/*.exe "$dir"/*.EXE "$dir"/*.map "$dir"/*.MAP 2>/dev/null || true
  }

  if [[ "$DO_CLEAN" == "1" ]]; then
    log "Cleaning..."
    host_clean_dir "src/digisnd"
    host_clean_dir "src/utility"
    host_clean_dir "reference/src/_g1"
    host_clean_dir "reference/src/_g2"
    host_clean_dir "reference/src/_g3"
    exit 0
  fi

  G1_WLK_HOST="$ROOT_DIR/build/G1_HOST.WLK"
  G2_WLK_HOST="$ROOT_DIR/build/G2_HOST.WLK"
  G3_WLK_HOST="$ROOT_DIR/build/G3_HOST.WLK"

  cat >"$G1_WLK_HOST" <<'EOF'
format dos
option dosseg
option map=reference/src/_g1/_G1.MAP
name reference/src/_g1/_G1.EXE

file reference/src/_g1/1_BACK.OBJ
file reference/src/_g1/1_BOSS1.OBJ
file reference/src/_g1/1_FILE.OBJ
file reference/src/_g1/1_GRP.OBJ
file reference/src/_g1/1_IMAGE.OBJ
file reference/src/_g1/1_INIT.OBJ
file reference/src/_g1/1_MAIN.OBJ
file reference/src/_g1/1_MOVE.OBJ
file reference/src/_g1/1_MOVPAT.OBJ
file reference/src/_g1/1_MUSIC.OBJ
file reference/src/_g1/1_OBJECT.OBJ
file reference/src/_g1/1_PANEL.OBJ
file reference/src/_g1/1_SBFX.OBJ
file reference/src/_g1/1_SCRIPT.OBJ
file reference/src/_g1/1_SHTMOV.OBJ
file reference/src/_g1/1_SHTPAT.OBJ
file reference/src/_g1/1_SOUND.OBJ
file reference/src/_g1/1_SPTILE.OBJ

file src/utility/G_ASM.OBJ
file src/utility/ADLIB.OBJ
file src/utility/FX_MAN.OBJ
file src/utility/JOY.OBJ
file src/utility/LZSS.OBJ
file src/utility/MU_MAN.OBJ
file src/utility/RES_ENCO.OBJ
file src/utility/RES_FALL.OBJ
file src/utility/RES_FIND.OBJ
file src/utility/RES_INIT.OBJ
file src/utility/RES_INT.OBJ
file src/utility/RES_READ.OBJ

file src/digisnd/DIGISND.OBJ
EOF
  echo "libpath $WATCOM_LIB_DOS" >>"$G1_WLK_HOST"

  cat >"$G2_WLK_HOST" <<'EOF'
format dos
option dosseg
option map=reference/src/_g2/_G2.MAP
name reference/src/_g2/_G2.EXE

file reference/src/_g2/2_BACK.OBJ
file reference/src/_g2/2_BOSS.OBJ
file reference/src/_g2/2_FILE.OBJ
file reference/src/_g2/2_GRP.OBJ
file reference/src/_g2/2_IMAGE.OBJ
file reference/src/_g2/2_INIT.OBJ
file reference/src/_g2/2_MAIN.OBJ
file reference/src/_g2/2_MOVE.OBJ
file reference/src/_g2/2_MOVPAT.OBJ
file reference/src/_g2/2_MUSIC.OBJ
file reference/src/_g2/2_OBJECT.OBJ
file reference/src/_g2/2_PANEL.OBJ
file reference/src/_g2/2_SBFX.OBJ
file reference/src/_g2/2_SCRIPT.OBJ
file reference/src/_g2/2_SHTMOV.OBJ
file reference/src/_g2/2_SHTPAT.OBJ
file reference/src/_g2/2_SOUND.OBJ
file reference/src/_g2/2_SPTILE.OBJ

file src/utility/G_ASM.OBJ
file src/utility/ADLIB.OBJ
file src/utility/FX_MAN.OBJ
file src/utility/JOY.OBJ
file src/utility/LZSS.OBJ
file src/utility/MU_MAN.OBJ
file src/utility/RES_ENCO.OBJ
file src/utility/RES_FALL.OBJ
file src/utility/RES_FIND.OBJ
file src/utility/RES_INIT.OBJ
file src/utility/RES_INT.OBJ
file src/utility/RES_READ.OBJ

file src/digisnd/DIGISND.OBJ
EOF
  echo "libpath $WATCOM_LIB_DOS" >>"$G2_WLK_HOST"

  cat >"$G3_WLK_HOST" <<'EOF'
format dos
option dosseg
option map=reference/src/_g3/_G3.MAP
name reference/src/_g3/_G3.EXE

file reference/src/_g3/3_BACK.OBJ
file reference/src/_g3/3_BOSS.OBJ
file reference/src/_g3/3_FILE.OBJ
file reference/src/_g3/3_GRP.OBJ
file reference/src/_g3/3_IMAGE.OBJ
file reference/src/_g3/3_INIT.OBJ
file reference/src/_g3/3_MAIN.OBJ
file reference/src/_g3/3_MOVE.OBJ
file reference/src/_g3/3_MOVPAT.OBJ
file reference/src/_g3/3_MUSIC.OBJ
file reference/src/_g3/3_OBJECT.OBJ
file reference/src/_g3/3_PANEL.OBJ
file reference/src/_g3/3_SBFX.OBJ
file reference/src/_g3/3_SCRIPT.OBJ
file reference/src/_g3/3_SHTMOV.OBJ
file reference/src/_g3/3_SHTPAT.OBJ
file reference/src/_g3/3_SOUND.OBJ
file reference/src/_g3/3_SPTILE.OBJ

file src/utility/G_ASM.OBJ
file src/utility/ADLIB.OBJ
file src/utility/FX_MAN.OBJ
file src/utility/JOY.OBJ
file src/utility/LZSS.OBJ
file src/utility/MU_MAN.OBJ
file src/utility/RES_ENCO.OBJ
file src/utility/RES_FALL.OBJ
file src/utility/RES_FIND.OBJ
file src/utility/RES_INIT.OBJ
file src/utility/RES_INT.OBJ
file src/utility/RES_READ.OBJ

file src/digisnd/DIGISND.OBJ
EOF
  echo "libpath $WATCOM_LIB_DOS" >>"$G3_WLK_HOST"

  log "GOT host build started (OpenWatcom v2)"

  log "[digisnd]"
  ( cd src/digisnd && rm -f DIGISND.ERR && "${OWCC[@]}" -fr=DIGISND.ERR -fo=DIGISND.OBJ DIGISND.C ) >>"$HOST_BUILD_LOG" 2>&1 || {
    if [[ -f src/digisnd/DIGISND.ERR ]]; then cat src/digisnd/DIGISND.ERR | tee -a "$HOST_BUILD_LOG" >/dev/null; fi
    echo "error: digisnd compile failed (see $HOST_BUILD_LOG)"
    exit 1
  }

  log "[utility objs]"
  ( cd src/utility && "${OWASM[@]}" -fo=G_ASM.OBJ G_ASM.ASM ) >>"$HOST_BUILD_LOG" 2>&1
  for c in ADLIB FX_MAN JOY LZSS MU_MAN RES_ENCO RES_FALL RES_FIND RES_INIT RES_INT RES_READ; do
    ( cd src/utility && rm -f "${c}.ERR" && "${OWCC[@]}" -fr="${c}.ERR" -fo="${c}.OBJ" "${c}.C" ) >>"$HOST_BUILD_LOG" 2>&1 || {
      if [[ -f "src/utility/${c}.ERR" ]]; then cat "src/utility/${c}.ERR" | tee -a "$HOST_BUILD_LOG" >/dev/null; fi
      echo "error: utility compile failed for ${c}.C (see $HOST_BUILD_LOG)"
      exit 1
    }
  done

  build_game() {
    local game="$1"
    local prefix="$2"
    local wlk="$3"
    log "[_${game}]"
    local dir="reference/src/_${game}"
    local units=(BACK FILE GRP IMAGE INIT MAIN MOVE MOVPAT MUSIC OBJECT PANEL SBFX SCRIPT SHTMOV SHTPAT SOUND SPTILE)
    if [[ "$game" == "g1" ]]; then units=(BACK BOSS1 FILE GRP IMAGE INIT MAIN MOVE MOVPAT MUSIC OBJECT PANEL SBFX SCRIPT SHTMOV SHTPAT SOUND SPTILE); fi
    if [[ "$game" == "g2" || "$game" == "g3" ]]; then units=(BACK BOSS FILE GRP IMAGE INIT MAIN MOVE MOVPAT MUSIC OBJECT PANEL SBFX SCRIPT SHTMOV SHTPAT SOUND SPTILE); fi
    for unit in "${units[@]}"; do
      ( cd "$dir" && rm -f "${prefix}_${unit}.ERR" && "${OWCC[@]}" -fr="${prefix}_${unit}.ERR" -fo="${prefix}_${unit}.OBJ" "${prefix}_${unit}.C" ) >>"$HOST_BUILD_LOG" 2>&1 || {
        if [[ -f "$dir/${prefix}_${unit}.ERR" ]]; then cat "$dir/${prefix}_${unit}.ERR" | tee -a "$HOST_BUILD_LOG" >/dev/null; fi
        echo "error: compile failed for ${dir}/${prefix}_${unit}.C (see $HOST_BUILD_LOG)"
        exit 1
      }
    done
    ( cd "$ROOT_DIR" && "$OWLINK_BIN" "@$wlk" ) >>"$HOST_BUILD_LOG" 2>&1 || {
      echo "error: link failed for _${game} (see $HOST_BUILD_LOG)"
      exit 1
    }
  }

  build_game "g1" "1" "$G1_WLK_HOST"
  build_game "g2" "2" "$G2_WLK_HOST"
  build_game "g3" "3" "$G3_WLK_HOST"

  cat "$HOST_BUILD_LOG"
  exit 0
fi

DOSBOX_BIN="${DOSBOX_BIN:-}"
if [[ -z "$DOSBOX_BIN" ]]; then
  # Prefer the classic DOSBox.app for scripted builds on macOS; DOSBox-X has
  # been observed to SIGSEGV when executing commands headlessly in this env.
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

# OpenWatcom toolchain (DOS-hosted) is vendored via scripts/toolchain/fetch_openwatcom.py
WATCOM_HOST_ROOT="$ROOT_DIR/ow/DEVEL/WATCOMC"
if [[ ! -f "$WATCOM_HOST_ROOT/BINW/WCC.EXE" || ! -f "$WATCOM_HOST_ROOT/BINW/WASM.EXE" || ! -f "$WATCOM_HOST_ROOT/BINW/WLINK.EXE" ]]; then
  echo "error: OpenWatcom toolchain not found under:"
  echo "  $WATCOM_HOST_ROOT"
  echo "expected at least:"
  echo "  $WATCOM_HOST_ROOT/BINW/WCC.EXE"
  echo "  $WATCOM_HOST_ROOT/BINW/WASM.EXE"
  echo "  $WATCOM_HOST_ROOT/BINW/WLINK.EXE"
  echo
  echo "hint: run: python3 scripts/toolchain/fetch_openwatcom.py"
  exit 1
fi

# Build log written by DOS tools into the mounted host `build/` dir.
DOS_BUILD_LOG="C:\\BUILD\\BUILD.TXT"

# Avoid passing a huge number of `-c` args to DOSBox-X; we observed crashes
# (SIGSEGV) on macOS in some configurations. Use a batch file instead.
BUILD_BAT_HOST="$ROOT_DIR/build/BUILD.BAT"

# WLINK response files are stored under build/ so we don't need to commit them,
# and we can keep them 8.3-safe.
G1_WLK_HOST="$ROOT_DIR/build/G1.WLK"
G2_WLK_HOST="$ROOT_DIR/build/G2.WLK"
G3_WLK_HOST="$ROOT_DIR/build/G3.WLK"

dos_crlf() {
  # Write DOS-friendly CRLF endings in generated batch/response files.
  # (DOSBox can usually handle LF, but the tooling is more predictable with CRLF.)
  perl -pi -e 's/\n/\r\n/g' "$1"
}

{
  cat <<'EOF'
system dos
option map=C:\SRC\_G1\_G1.MAP
name C:\SRC\_G1\_G1.EXE

file C:\SRC\_G1\1_BACK.OBJ
file C:\SRC\_G1\1_BOSS1.OBJ
file C:\SRC\_G1\1_FILE.OBJ
file C:\SRC\_G1\1_GRP.OBJ
file C:\SRC\_G1\1_IMAGE.OBJ
file C:\SRC\_G1\1_INIT.OBJ
file C:\SRC\_G1\1_MAIN.OBJ
file C:\SRC\_G1\1_MOVE.OBJ
file C:\SRC\_G1\1_MOVPAT.OBJ
file C:\SRC\_G1\1_MUSIC.OBJ
file C:\SRC\_G1\1_OBJECT.OBJ
file C:\SRC\_G1\1_PANEL.OBJ
file C:\SRC\_G1\1_SBFX.OBJ
file C:\SRC\_G1\1_SCRIPT.OBJ
file C:\SRC\_G1\1_SHTMOV.OBJ
file C:\SRC\_G1\1_SHTPAT.OBJ
file C:\SRC\_G1\1_SOUND.OBJ
file C:\SRC\_G1\1_SPTILE.OBJ

file C:\SRC\UTILITY\G_ASM.OBJ
file C:\SRC\UTILITY\ADLIB.OBJ
file C:\SRC\UTILITY\FX_MAN.OBJ
file C:\SRC\UTILITY\JOY.OBJ
file C:\SRC\UTILITY\LZSS.OBJ
file C:\SRC\UTILITY\MU_MAN.OBJ
file C:\SRC\UTILITY\RES_ENCO.OBJ
file C:\SRC\UTILITY\RES_FALL.OBJ
file C:\SRC\UTILITY\RES_FIND.OBJ
file C:\SRC\UTILITY\RES_INIT.OBJ
file C:\SRC\UTILITY\RES_INT.OBJ
file C:\SRC\UTILITY\RES_READ.OBJ

file C:\SRC\DIGISND\DIGISND.OBJ
EOF
} >"$G1_WLK_HOST"
dos_crlf "$G1_WLK_HOST"

{
  cat <<'EOF'
system dos
option map=C:\SRC\_G2\_G2.MAP
name C:\SRC\_G2\_G2.EXE

file C:\SRC\_G2\2_BACK.OBJ
file C:\SRC\_G2\2_BOSS.OBJ
file C:\SRC\_G2\2_FILE.OBJ
file C:\SRC\_G2\2_GRP.OBJ
file C:\SRC\_G2\2_IMAGE.OBJ
file C:\SRC\_G2\2_INIT.OBJ
file C:\SRC\_G2\2_MAIN.OBJ
file C:\SRC\_G2\2_MOVE.OBJ
file C:\SRC\_G2\2_MOVPAT.OBJ
file C:\SRC\_G2\2_MUSIC.OBJ
file C:\SRC\_G2\2_OBJECT.OBJ
file C:\SRC\_G2\2_PANEL.OBJ
file C:\SRC\_G2\2_SBFX.OBJ
file C:\SRC\_G2\2_SCRIPT.OBJ
file C:\SRC\_G2\2_SHTMOV.OBJ
file C:\SRC\_G2\2_SHTPAT.OBJ
file C:\SRC\_G2\2_SOUND.OBJ
file C:\SRC\_G2\2_SPTILE.OBJ

file C:\SRC\UTILITY\G_ASM.OBJ
file C:\SRC\UTILITY\ADLIB.OBJ
file C:\SRC\UTILITY\FX_MAN.OBJ
file C:\SRC\UTILITY\JOY.OBJ
file C:\SRC\UTILITY\LZSS.OBJ
file C:\SRC\UTILITY\MU_MAN.OBJ
file C:\SRC\UTILITY\RES_ENCO.OBJ
file C:\SRC\UTILITY\RES_FALL.OBJ
file C:\SRC\UTILITY\RES_FIND.OBJ
file C:\SRC\UTILITY\RES_INIT.OBJ
file C:\SRC\UTILITY\RES_INT.OBJ
file C:\SRC\UTILITY\RES_READ.OBJ

file C:\SRC\DIGISND\DIGISND.OBJ
EOF
} >"$G2_WLK_HOST"
dos_crlf "$G2_WLK_HOST"

{
  cat <<'EOF'
system dos
option map=C:\SRC\_G3\_G3.MAP
name C:\SRC\_G3\_G3.EXE

file C:\SRC\_G3\3_BACK.OBJ
file C:\SRC\_G3\3_BOSS.OBJ
file C:\SRC\_G3\3_FILE.OBJ
file C:\SRC\_G3\3_GRP.OBJ
file C:\SRC\_G3\3_IMAGE.OBJ
file C:\SRC\_G3\3_INIT.OBJ
file C:\SRC\_G3\3_MAIN.OBJ
file C:\SRC\_G3\3_MOVE.OBJ
file C:\SRC\_G3\3_MOVPAT.OBJ
file C:\SRC\_G3\3_MUSIC.OBJ
file C:\SRC\_G3\3_OBJECT.OBJ
file C:\SRC\_G3\3_PANEL.OBJ
file C:\SRC\_G3\3_SBFX.OBJ
file C:\SRC\_G3\3_SCRIPT.OBJ
file C:\SRC\_G3\3_SHTMOV.OBJ
file C:\SRC\_G3\3_SHTPAT.OBJ
file C:\SRC\_G3\3_SOUND.OBJ
file C:\SRC\_G3\3_SPTILE.OBJ

file C:\SRC\UTILITY\G_ASM.OBJ
file C:\SRC\UTILITY\ADLIB.OBJ
file C:\SRC\UTILITY\FX_MAN.OBJ
file C:\SRC\UTILITY\JOY.OBJ
file C:\SRC\UTILITY\LZSS.OBJ
file C:\SRC\UTILITY\MU_MAN.OBJ
file C:\SRC\UTILITY\RES_ENCO.OBJ
file C:\SRC\UTILITY\RES_FALL.OBJ
file C:\SRC\UTILITY\RES_FIND.OBJ
file C:\SRC\UTILITY\RES_INIT.OBJ
file C:\SRC\UTILITY\RES_INT.OBJ
file C:\SRC\UTILITY\RES_READ.OBJ

file C:\SRC\DIGISND\DIGISND.OBJ
EOF
} >"$G3_WLK_HOST"
dos_crlf "$G3_WLK_HOST"

{
  printf "@echo off\r\n"
  printf "if not exist C:\\\\BUILD mkdir C:\\\\BUILD\r\n"
  printf "set WATCOM=C:\\\\OW\\\\DEVEL\\\\WATCOMC\r\n"
  printf "set PATH=%%WATCOM%%\\\\BINW;%%PATH%%\r\n"
  # Keep command lines short: add project includes here instead of per-compile `-i=...`
  printf "set INCLUDE=C:\\\\SRC\\\\OWCOMPAT;C:\\\\SRC\\\\UTILITY;C:\\\\SRC\\\\DIGISND;%%WATCOM%%\\\\H;%%WATCOM%%\\\\H\\\\DOS\r\n"
  printf "set LIB=%%WATCOM%%\\\\LIB286\\\\DOS\r\n"
  # Note: using a short `-fi=OWPRE.H` keeps the command line small enough for
  # the DOS4GW stub in some environments (absolute paths can trip "Arg list too big").
  # Disable optimizations for now: faster compiles, fewer compiler edge cases.
  printf "set OWCC=wcc -bt=dos -mm -2 -ze -j -q -od -fi=owpre.h\r\n"
  printf "set OWASM=wasm -bt=dos -2 -zcm=tasm -zq\r\n"
  printf "echo GOT build started > %s\r\n" "$DOS_BUILD_LOG"

  if [[ "$DO_CLEAN" == "1" ]]; then
    printf "echo Cleaning... >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "cd \\\\SRC\\\\DIGISND\r\n"
	    printf "del *.obj >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.err >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "cd \\\\SRC\\\\UTILITY\r\n"
	    printf "del *.obj >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.err >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "cd \\\\SRC\\\\_G1\r\n"
	    printf "del *.obj >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.exe >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.map >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.err >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "cd \\\\SRC\\\\_G2\r\n"
	    printf "del *.obj >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.exe >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.map >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.err >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "cd \\\\SRC\\\\_G3\r\n"
	    printf "del *.obj >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.exe >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.map >> %s\r\n" "$DOS_BUILD_LOG"
	    printf "del *.err >> %s\r\n" "$DOS_BUILD_LOG"
	  else
    printf "echo [digisnd] >> %s\r\n" "$DOS_BUILD_LOG"
    printf "cd \\\\SRC\\\\DIGISND\r\n"
    printf "%%OWCC%% -fr=DIGISND.ERR -fo=DIGISND.OBJ DIGISND.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist DIGISND.ERR type DIGISND.ERR >> %s\r\n" "$DOS_BUILD_LOG"

    printf "echo [utility objs] >> %s\r\n" "$DOS_BUILD_LOG"
    printf "cd \\\\SRC\\\\UTILITY\r\n"
    printf "%%OWASM%% -fo=G_ASM.OBJ G_ASM.ASM >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=ADLIB.ERR -fo=ADLIB.OBJ ADLIB.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist ADLIB.ERR type ADLIB.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=FX_MAN.ERR -fo=FX_MAN.OBJ FX_MAN.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist FX_MAN.ERR type FX_MAN.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=JOY.ERR -fo=JOY.OBJ JOY.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist JOY.ERR type JOY.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=LZSS.ERR -fo=LZSS.OBJ LZSS.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist LZSS.ERR type LZSS.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=MU_MAN.ERR -fo=MU_MAN.OBJ MU_MAN.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist MU_MAN.ERR type MU_MAN.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=RES_ENCO.ERR -fo=RES_ENCO.OBJ RES_ENCO.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist RES_ENCO.ERR type RES_ENCO.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=RES_FALL.ERR -fo=RES_FALL.OBJ RES_FALL.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist RES_FALL.ERR type RES_FALL.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=RES_FIND.ERR -fo=RES_FIND.OBJ RES_FIND.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist RES_FIND.ERR type RES_FIND.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=RES_INIT.ERR -fo=RES_INIT.OBJ RES_INIT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist RES_INIT.ERR type RES_INIT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=RES_INT.ERR -fo=RES_INT.OBJ RES_INT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist RES_INT.ERR type RES_INT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=RES_READ.ERR -fo=RES_READ.OBJ RES_READ.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist RES_READ.ERR type RES_READ.ERR >> %s\r\n" "$DOS_BUILD_LOG"

    printf "echo [_g1] >> %s\r\n" "$DOS_BUILD_LOG"
    printf "cd \\\\SRC\\\\_G1\r\n"
    printf "%%OWCC%% -fr=1_BACK.ERR -fo=1_BACK.OBJ 1_BACK.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_BACK.ERR type 1_BACK.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_BOSS1.ERR -fo=1_BOSS1.OBJ 1_BOSS1.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_BOSS1.ERR type 1_BOSS1.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_FILE.ERR -fo=1_FILE.OBJ 1_FILE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_FILE.ERR type 1_FILE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_GRP.ERR -fo=1_GRP.OBJ 1_GRP.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_GRP.ERR type 1_GRP.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_IMAGE.ERR -fo=1_IMAGE.OBJ 1_IMAGE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_IMAGE.ERR type 1_IMAGE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_INIT.ERR -fo=1_INIT.OBJ 1_INIT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_INIT.ERR type 1_INIT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_MAIN.ERR -fo=1_MAIN.OBJ 1_MAIN.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_MAIN.ERR type 1_MAIN.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_MOVE.ERR -fo=1_MOVE.OBJ 1_MOVE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_MOVE.ERR type 1_MOVE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_MOVPAT.ERR -fo=1_MOVPAT.OBJ 1_MOVPAT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_MOVPAT.ERR type 1_MOVPAT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_MUSIC.ERR -fo=1_MUSIC.OBJ 1_MUSIC.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_MUSIC.ERR type 1_MUSIC.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_OBJECT.ERR -fo=1_OBJECT.OBJ 1_OBJECT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_OBJECT.ERR type 1_OBJECT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_PANEL.ERR -fo=1_PANEL.OBJ 1_PANEL.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_PANEL.ERR type 1_PANEL.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_SBFX.ERR -fo=1_SBFX.OBJ 1_SBFX.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_SBFX.ERR type 1_SBFX.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_SCRIPT.ERR -fo=1_SCRIPT.OBJ 1_SCRIPT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_SCRIPT.ERR type 1_SCRIPT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_SHTMOV.ERR -fo=1_SHTMOV.OBJ 1_SHTMOV.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_SHTMOV.ERR type 1_SHTMOV.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_SHTPAT.ERR -fo=1_SHTPAT.OBJ 1_SHTPAT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_SHTPAT.ERR type 1_SHTPAT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_SOUND.ERR -fo=1_SOUND.OBJ 1_SOUND.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_SOUND.ERR type 1_SOUND.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=1_SPTILE.ERR -fo=1_SPTILE.OBJ 1_SPTILE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 1_SPTILE.ERR type 1_SPTILE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "wlink @C:\\\\BUILD\\\\G1.WLK >> %s\r\n" "$DOS_BUILD_LOG"

    printf "echo [_g2] >> %s\r\n" "$DOS_BUILD_LOG"
    printf "cd \\\\SRC\\\\_G2\r\n"
    printf "%%OWCC%% -fr=2_BACK.ERR -fo=2_BACK.OBJ 2_BACK.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_BACK.ERR type 2_BACK.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_BOSS.ERR -fo=2_BOSS.OBJ 2_BOSS.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_BOSS.ERR type 2_BOSS.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_FILE.ERR -fo=2_FILE.OBJ 2_FILE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_FILE.ERR type 2_FILE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_GRP.ERR -fo=2_GRP.OBJ 2_GRP.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_GRP.ERR type 2_GRP.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_IMAGE.ERR -fo=2_IMAGE.OBJ 2_IMAGE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_IMAGE.ERR type 2_IMAGE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_INIT.ERR -fo=2_INIT.OBJ 2_INIT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_INIT.ERR type 2_INIT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_MAIN.ERR -fo=2_MAIN.OBJ 2_MAIN.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_MAIN.ERR type 2_MAIN.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_MOVE.ERR -fo=2_MOVE.OBJ 2_MOVE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_MOVE.ERR type 2_MOVE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_MOVPAT.ERR -fo=2_MOVPAT.OBJ 2_MOVPAT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_MOVPAT.ERR type 2_MOVPAT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_MUSIC.ERR -fo=2_MUSIC.OBJ 2_MUSIC.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_MUSIC.ERR type 2_MUSIC.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_OBJECT.ERR -fo=2_OBJECT.OBJ 2_OBJECT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_OBJECT.ERR type 2_OBJECT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_PANEL.ERR -fo=2_PANEL.OBJ 2_PANEL.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_PANEL.ERR type 2_PANEL.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_SBFX.ERR -fo=2_SBFX.OBJ 2_SBFX.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_SBFX.ERR type 2_SBFX.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_SCRIPT.ERR -fo=2_SCRIPT.OBJ 2_SCRIPT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_SCRIPT.ERR type 2_SCRIPT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_SHTMOV.ERR -fo=2_SHTMOV.OBJ 2_SHTMOV.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_SHTMOV.ERR type 2_SHTMOV.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_SHTPAT.ERR -fo=2_SHTPAT.OBJ 2_SHTPAT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_SHTPAT.ERR type 2_SHTPAT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_SOUND.ERR -fo=2_SOUND.OBJ 2_SOUND.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_SOUND.ERR type 2_SOUND.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=2_SPTILE.ERR -fo=2_SPTILE.OBJ 2_SPTILE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 2_SPTILE.ERR type 2_SPTILE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "wlink @C:\\\\BUILD\\\\G2.WLK >> %s\r\n" "$DOS_BUILD_LOG"

    printf "echo [_g3] >> %s\r\n" "$DOS_BUILD_LOG"
    printf "cd \\\\SRC\\\\_G3\r\n"
    printf "%%OWCC%% -fr=3_BACK.ERR -fo=3_BACK.OBJ 3_BACK.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_BACK.ERR type 3_BACK.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_BOSS.ERR -fo=3_BOSS.OBJ 3_BOSS.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_BOSS.ERR type 3_BOSS.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_FILE.ERR -fo=3_FILE.OBJ 3_FILE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_FILE.ERR type 3_FILE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_GRP.ERR -fo=3_GRP.OBJ 3_GRP.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_GRP.ERR type 3_GRP.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_IMAGE.ERR -fo=3_IMAGE.OBJ 3_IMAGE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_IMAGE.ERR type 3_IMAGE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_INIT.ERR -fo=3_INIT.OBJ 3_INIT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_INIT.ERR type 3_INIT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_MAIN.ERR -fo=3_MAIN.OBJ 3_MAIN.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_MAIN.ERR type 3_MAIN.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_MOVE.ERR -fo=3_MOVE.OBJ 3_MOVE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_MOVE.ERR type 3_MOVE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_MOVPAT.ERR -fo=3_MOVPAT.OBJ 3_MOVPAT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_MOVPAT.ERR type 3_MOVPAT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_MUSIC.ERR -fo=3_MUSIC.OBJ 3_MUSIC.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_MUSIC.ERR type 3_MUSIC.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_OBJECT.ERR -fo=3_OBJECT.OBJ 3_OBJECT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_OBJECT.ERR type 3_OBJECT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_PANEL.ERR -fo=3_PANEL.OBJ 3_PANEL.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_PANEL.ERR type 3_PANEL.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_SBFX.ERR -fo=3_SBFX.OBJ 3_SBFX.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_SBFX.ERR type 3_SBFX.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_SCRIPT.ERR -fo=3_SCRIPT.OBJ 3_SCRIPT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_SCRIPT.ERR type 3_SCRIPT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_SHTMOV.ERR -fo=3_SHTMOV.OBJ 3_SHTMOV.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_SHTMOV.ERR type 3_SHTMOV.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_SHTPAT.ERR -fo=3_SHTPAT.OBJ 3_SHTPAT.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_SHTPAT.ERR type 3_SHTPAT.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_SOUND.ERR -fo=3_SOUND.OBJ 3_SOUND.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_SOUND.ERR type 3_SOUND.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "%%OWCC%% -fr=3_SPTILE.ERR -fo=3_SPTILE.OBJ 3_SPTILE.C >> %s\r\n" "$DOS_BUILD_LOG"
    printf "if exist 3_SPTILE.ERR type 3_SPTILE.ERR >> %s\r\n" "$DOS_BUILD_LOG"
    printf "wlink @C:\\\\BUILD\\\\G3.WLK >> %s\r\n" "$DOS_BUILD_LOG"
  fi
  printf "exit\r\n"
} >"$BUILD_BAT_HOST"
dos_crlf "$BUILD_BAT_HOST"

"$DOSBOX_BIN" -conf "$ROOT_DIR/shell.conf" \
  -c "mount c \"$ROOT_DIR\"" \
  -c "c:" \
  -c "call C:\\BUILD\\BUILD.BAT" \
  -c "exit" \
  >/dev/null 2>&1 || true

if [[ -f "$ROOT_DIR/build/BUILD.TXT" ]]; then
  cat "$ROOT_DIR/build/BUILD.TXT"
else
  echo "error: DOS build did not produce build/BUILD.TXT"
  echo "hint: verify DOSBox ran and that OpenWatcom is set up under ow/."
  exit 1
fi
