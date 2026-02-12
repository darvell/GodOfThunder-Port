#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

DIR="${1:-dist_web}"
PORT="${PORT:-8000}"

if [ ! -d "${DIR}" ]; then
  echo "Directory not found: ${DIR}" >&2
  exit 1
fi

# Use uv for python invocation (repo convention).
exec uv run python -m http.server "${PORT}" --directory "${DIR}"

