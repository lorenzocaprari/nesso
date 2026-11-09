#!/usr/bin/env bash
# Build the AMX determinism harness against an ONNX Runtime shared library.
# Usage: build-harness.sh ORT_INCLUDE_DIR ORT_LIB_DIR OUTPUT

set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: build-harness.sh ORT_INCLUDE_DIR ORT_LIB_DIR OUTPUT" >&2
  exit 2
fi

ORT_INCLUDE="$1"
ORT_LIB="$2"
OUTPUT="$3"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

g++ -std=c++23 -O2 -Wall -Wextra -Wpedantic \
  -I "${ROOT}/src/embed/include" \
  -I "${ORT_INCLUDE}" \
  "${ROOT}/experiments/ort-amx/harness.cpp" \
  "${ROOT}/src/embed/wordpiece_tokenizer.cpp" \
  "${ROOT}/src/embed/amx_guard.cpp" \
  -L "${ORT_LIB}" -lonnxruntime \
  -Wl,-rpath,"${ORT_LIB}" \
  -o "${OUTPUT}"
