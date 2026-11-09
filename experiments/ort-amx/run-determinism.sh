#!/usr/bin/env bash
# Compare harness dumps to a 1-thread reference by byte equality.
# Usage: run-determinism.sh --harness H --model M --vocab V --guard on|off --threads N --runs N --outdir DIR
#        [--repeats N] [--quant-precision 0|1] [--label NAME]

set -euo pipefail

HARNESS=""
MODEL=""
VOCAB=""
GUARD="off"
THREADS="4"
RUNS="200"
REPEATS="1"
QUANT="1"
OUTDIR=""
LABEL="run"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --harness) HARNESS="$2"; shift 2 ;;
    --model) MODEL="$2"; shift 2 ;;
    --vocab) VOCAB="$2"; shift 2 ;;
    --guard) GUARD="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --runs) RUNS="$2"; shift 2 ;;
    --repeats) REPEATS="$2"; shift 2 ;;
    --quant-precision) QUANT="$2"; shift 2 ;;
    --outdir) OUTDIR="$2"; shift 2 ;;
    --label) LABEL="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "${HARNESS}" || -z "${MODEL}" || -z "${VOCAB}" || -z "${OUTDIR}" ]]; then
  echo "missing --harness, --model, --vocab, or --outdir" >&2
  exit 2
fi

mkdir -p "${OUTDIR}"
SUMMARY="${OUTDIR}/summary.txt"
: > "${SUMMARY}"

run_once() {
  local threads="$1"
  local repeats="$2"
  local dump="$3"
  local log="$4"
  set +e
  "${HARNESS}" --model "${MODEL}" --vocab "${VOCAB}" --dump "${dump}" \
    --threads "${threads}" --repeats "${repeats}" --guard "${GUARD}" \
    --quant-precision "${QUANT}" > "${log}" 2> "${log}.err"
  local rc=$?
  set -e
  echo "${rc}"
}

note() {
  echo "$1" | tee -a "${SUMMARY}"
}

note "label=${LABEL}"
note "guard=${GUARD}"
note "quant_precision=${QUANT}"
note "loaded_threads=${THREADS}"
note "runs=${RUNS}"
note "repeats=${REPEATS}"
note "start_loadavg=$(cut -d' ' -f1-3 /proc/loadavg)"

REF="${OUTDIR}/reference.bin"
REF_RC="$(run_once 1 1 "${REF}" "${OUTDIR}/reference.log")"
note "reference_rc=${REF_RC}"
if [[ "${REF_RC}" -ne 0 ]]; then
  note "reference_failed=1"
  cat "${OUTDIR}/reference.log" "${OUTDIR}/reference.log.err" >&2 || true
  exit 1
fi
cp "${OUTDIR}/reference.log" "${OUTDIR}/reference.stdout"
note "reference_sha256=$(sha256sum "${REF}" | awk '{print $1}')"
grep -E '^(batch|max_seq|dims|vectors|bytes|init_ms|embed_ms|intra_mismatch)=' "${OUTDIR}/reference.log" | sed 's/^/reference_/' | tee -a "${SUMMARY}"

STABLE=0
for extra in 2 3; do
  DUMP="${OUTDIR}/reference-${extra}.bin"
  RC="$(run_once 1 1 "${DUMP}" "${OUTDIR}/reference-${extra}.log")"
  if [[ "${RC}" -ne 0 ]] || ! cmp -s "${REF}" "${DUMP}"; then
    STABLE=1
  fi
done
note "reference_1thread_unstable=${STABLE}"

STRESS_PID=""
cleanup() {
  if [[ -n "${STRESS_PID}" ]]; then
    kill "${STRESS_PID}" 2>/dev/null || true
    wait "${STRESS_PID}" 2>/dev/null || true
    STRESS_PID=""
  fi
}
trap cleanup EXIT

stress-ng --cpu 4 --cpu-method matrixprod --timeout 0 >/dev/null 2>&1 &
STRESS_PID=$!
note "stress_pid=${STRESS_PID}"
sleep 1

MISMATCH=0
CRASH=0
INTRA=0
OK=0
EMBED_LOG="${OUTDIR}/embed_ms.txt"
: > "${EMBED_LOG}"

for ((i = 1; i <= RUNS; i++)); do
  DUMP="${OUTDIR}/run.bin"
  LOG="${OUTDIR}/run.log"
  RC="$(run_once "${THREADS}" "${REPEATS}" "${DUMP}" "${LOG}")"
  if [[ "${RC}" -eq 4 ]]; then
    INTRA=$((INTRA + 1))
    MISMATCH=$((MISMATCH + 1))
    if [[ ! -f "${OUTDIR}/mismatch-sample.bin" ]]; then
      cp "${DUMP}" "${OUTDIR}/mismatch-sample.bin"
      cp "${LOG}" "${OUTDIR}/mismatch-sample.log"
    fi
  elif [[ "${RC}" -ne 0 ]]; then
    CRASH=$((CRASH + 1))
    if [[ ! -f "${OUTDIR}/crash-sample.log" ]]; then
      cp "${LOG}" "${OUTDIR}/crash-sample.log"
      cp "${LOG}.err" "${OUTDIR}/crash-sample.err"
    fi
  elif ! cmp -s "${REF}" "${DUMP}"; then
    MISMATCH=$((MISMATCH + 1))
    if [[ ! -f "${OUTDIR}/mismatch-sample.bin" ]]; then
      cp "${DUMP}" "${OUTDIR}/mismatch-sample.bin"
      cp "${LOG}" "${OUTDIR}/mismatch-sample.log"
    fi
  else
    OK=$((OK + 1))
  fi
  if [[ "${RC}" -eq 0 || "${RC}" -eq 4 ]]; then
    awk -F= '/^embed_ms=/{print $2}' "${LOG}" >> "${EMBED_LOG}"
  fi
  if (( i % 20 == 0 )); then
    echo "progress ${i}/${RUNS} ok=${OK} mismatch=${MISMATCH} intra=${INTRA} crash=${CRASH}"
  fi
done

note "ok=${OK}"
note "mismatch=${MISMATCH}"
note "intra_mismatch_processes=${INTRA}"
note "crash=${CRASH}"
note "end_loadavg=$(cut -d' ' -f1-3 /proc/loadavg)"
if [[ -s "${EMBED_LOG}" ]]; then
  python3 - "${EMBED_LOG}" <<'PY' | tee -a "${SUMMARY}"
import statistics, sys
vals = [float(line) for line in open(sys.argv[1]) if line.strip()]
if not vals:
    raise SystemExit(0)
vals.sort()
mid = statistics.median(vals)
print(f"embed_ms_n={len(vals)}")
print(f"embed_ms_min={vals[0]:.3f}")
print(f"embed_ms_median={mid:.3f}")
print(f"embed_ms_max={vals[-1]:.3f}")
PY
fi

cleanup
trap - EXIT
note "done=1"
