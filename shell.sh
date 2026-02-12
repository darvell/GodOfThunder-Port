#!/bin/bash

REPO_DIR=$(dirname $0 | realpath)

if [[ "$1" != "" ]]; then
  if command -v dosbox-x >/dev/null 2>&1; then
    DOSBOX_BIN="dosbox-x"
  elif [[ -x "/Applications/dosbox.app/Contents/MacOS/DOSBox" ]]; then
    DOSBOX_BIN="/Applications/dosbox.app/Contents/MacOS/DOSBox"
  else
    DOSBOX_BIN="dosbox"
  fi
  "$DOSBOX_BIN" -conf shell.conf \
    -c "mount c ." \
    -c "c:" \
    -c "set PATH=%PATH%;C:\SRC\_G1;C:\SRC\_G2;C:\SRC\_G3;C:\SRC\UTILITY"\
    -c "$1"\
    -nopromptfolder \
    -fastlaunch
else
  if command -v dosbox-x >/dev/null 2>&1; then
    DOSBOX_BIN="dosbox-x"
  elif [[ -x "/Applications/dosbox.app/Contents/MacOS/DOSBox" ]]; then
    DOSBOX_BIN="/Applications/dosbox.app/Contents/MacOS/DOSBox"
  else
    DOSBOX_BIN="dosbox"
  fi
  "$DOSBOX_BIN" -conf shell.conf \
    -c "mount c ." \
    -c "c:" \
    -c "set PATH=%PATH%;C:\SRC\_G1;C:\SRC\_G2;C:\SRC\_G3;C:\SRC\UTILITY"\
    -nopromptfolder \
    -fastlaunch
fi
