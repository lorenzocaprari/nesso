#!/usr/bin/env bash
# Print whether one MLAS symbol is reached during a single harness run.
# Usage: probe-kernel.sh SYMBOL -- harness-args...

set -euo pipefail

if [[ $# -lt 2 || "$2" != "--" ]]; then
  echo "usage: probe-kernel.sh SYMBOL -- HARNESS [args...]" >&2
  exit 2
fi

SYMBOL="$1"
shift 2
LOG="$(mktemp)"
gdb -batch -nx \
  -ex 'set pagination off' \
  -ex 'set confirm off' \
  -ex 'set breakpoint pending on' \
  -ex "break ${SYMBOL}" \
  -ex run \
  -ex quit \
  --args "$@" > "${LOG}" 2>&1 || true

if grep -q "hit Breakpoint 1" "${LOG}"; then
  echo "symbol=${SYMBOL} hit=1"
  rm -f "${LOG}"
  exit 0
fi
if grep -q "exited normally" "${LOG}"; then
  echo "symbol=${SYMBOL} hit=0"
  rm -f "${LOG}"
  exit 0
fi
echo "symbol=${SYMBOL} hit=unknown" >&2
cat "${LOG}" >&2
rm -f "${LOG}"
exit 1
