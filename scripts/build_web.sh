#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v emcmake >/dev/null 2>&1; then
  echo "emcmake not found. Install/activate the Emscripten SDK (emsdk) first." >&2
  exit 1
fi

BUILD_DIR="${BUILD_DIR:-build_web}"
DIST_DIR="${DIST_DIR:-dist_web}"
WEB_TARGET="${WEB_TARGET:-got_raylib}"

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

emcmake cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DGOT_BUILD_RAYLIB=ON
cmake --build "${BUILD_DIR}" --target "${WEB_TARGET}" -j "${JOBS:-8}"

cp -f GOTRES.DAT "${DIST_DIR}/GOTRES.DAT"
if [ -f VERSION.GOT ]; then
  cp -f VERSION.GOT "${DIST_DIR}/VERSION.GOT"
fi

# Copy unified single-page outputs.
for ext in html js wasm data; do
  src="${BUILD_DIR}/${WEB_TARGET}.${ext}"
  if [ -f "${src}" ]; then
    cp -f "${src}" "${DIST_DIR}/"
  fi
done

# Publish a single page entrypoint.
if [ -f "${DIST_DIR}/${WEB_TARGET}.html" ]; then
  cp -f "${DIST_DIR}/${WEB_TARGET}.html" "${DIST_DIR}/index.html"
fi

echo "Web build artifacts are in: ${DIST_DIR}"
echo "To run locally: ./scripts/serve_web.sh ${DIST_DIR}"
