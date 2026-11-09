# ORT AMX tile-state fix on this KVM guest

Recommendation: nesso can drop the AMX guard once a release that contains `924b50b0166d278cdc62fd39e0ce1a4c5cebe06c` ships. On this guest the fix never dispatches AMX; it stays on AVX-512 VNNI, and those vectors match the guarded VNNI path byte for byte.

This host does not pass the new tile-preservation probe, so a fix build that keeps AMX was not measured.

## Machine

`lscpu` on 2026-11-09:

- Model name: `Intel(R) Xeon(R) Processor`
- Family 6, model 207, stepping 2
- Hypervisor: KVM, full virtualization
- 4 vCPUs, 1 thread per core, 1 socket
- Flags present: `amx_tile`, `amx_int8`, `amx_bf16`, `avx512_vnni`, `avx512f`

`free -h` at the start: 15 GiB total, 5.0 GiB free, 5.1 GiB available. `/proc/vmstat` had `balloon_inflate` 2621440 and `balloon_deflate` 0 (about 10 GiB held in the balloon). AMX is advertised, so the run continued.

## Builds

Both are ONNX Runtime `1.32.0` (`VERSION_NUMBER`), CPU only, Release, shared library, unit tests off, Python off. Compiler: GCC 13.3.0. Generator: Ninja. Jobs: 2. ccache on. `--no_kleidiai --no_sve`.

```bash
./build.sh --config Release --build_shared_lib --parallel 2 --skip_tests \
  --skip_submodule_sync --cmake_generator Ninja --use_cache \
  --compile_no_warning_as_error --no_kleidiai --no_sve \
  --cmake_extra_defines onnxruntime_BUILD_UNIT_TESTS=OFF \
  CMAKE_C_COMPILER=/usr/bin/gcc CMAKE_CXX_COMPILER=/usr/bin/g++ \
  --update --build
```

| build | commit | `libonnxruntime.so.1.32.0` sha256 | result |
| --- | --- | --- | --- |
| control (first parent) | `870a122a59694aa472865e8514d1c697ca4a1d3a` | `afbee4793058a716f0eb44cb0ab5bb89df14d80392a9ec809343f05181f7138b` | success |
| fix | `924b50b0166d278cdc62fd39e0ce1a4c5cebe06c` | `bd21c040e4a471a35e31da74f3c5a622976ac4478c925d5aea0439f9714bc698` | success |

The two libraries are not byte-identical. The fix rebuild was incremental on the control tree.

## Harness

`experiments/ort-amx/harness.cpp` links nesso's WordPiece tokenizer and the same seccomp AMX deny as `src/embed/amx_guard.cpp`. It embeds 32 fixed lines with the int8 all-MiniLM-L6 graph (`model.onnx` sha256 `afdb6f1a0e45b715d0bb9b11772f032c399babd23bfc31fed1c170afc848bdb1`, vocab sha256 `07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3`).

Session matches nesso: sequential execution, inter-op 1, intra-op 4 for the loaded runs, `ORT_ENABLE_ALL`, `session.x64quantprecision=1`. Output is mean-pool plus L2, then raw float32 bytes. One batch per process: 32 vectors, max sequence 89, 384 dimensions, 49152 bytes.

The 1-thread reference is taken with no `stress-ng`, repeated 3 times. The 200 loaded processes run under `stress-ng --cpu 4 --cpu-method matrixprod`. Comparison is `cmp` of the dump against that build's 1-thread reference.

## Kernel path

gdb, first hit of a symbol in the unstripped `.so` (`probe-kernel.sh`). Quant precision 1.

| library | guard | threads | `MlasGemmU8S8CopyPackBAmx` | `MlasGemmU8S8KernelAvx512Vnni` | `MlasGemmU8S8KernelAvx512Core` |
| --- | --- | --- | --- | --- | --- |
| control | off | 4 | hit | not hit | |
| control | off | 1 | hit | not hit | |
| control | on | 4 | not hit | hit | not hit |
| fix | off | 4 | not hit | hit | not hit |
| fix | on | 4 | not hit | hit | |

Control with the guard off takes the AMX QGEMM pack path, including at 1 intra-op thread. The fix with the guard off does not; it takes AVX-512 VNNI. That is the fallback in `MlasInitAMX` when the migration probe fails, not a preserved-tile AMX dispatch.

## Determinism

200 processes, intra-op 4, guard as named, each compared to its own 1-thread reference. Every 1-thread reference was stable across 3 runs and had sha256 `a2bb76593403e95e914474ce97eea16e748767b5774bb3fef6a8a076a013bfd1`.

| run | guard | ok | mismatch | crash |
| --- | --- | --- | --- | --- |
| control | off | 4 | 196 | 0 |
| control | on | 200 | 0 | 0 |
| fix | off | 200 | 0 | 0 |
| fix | on | 200 | 0 | 0 |

The saved control/guard-off mismatch dump differed in 12287 of 12288 float32 values (one sample, not all 196).

Quiet dumps (no `stress-ng`), last of 11 runs, against the same VNNI sha256:

| run | threads | bytes vs VNNI reference |
| --- | --- | --- |
| control, guard off | 4 | differ (`74c555f2fc09ab32e1436bf85cef3ad264c90c2f9a7b9713c0103d521edd5713`) |
| control, guard off | 1 | identical |
| control, guard on | 1 and 4 | identical |
| fix, guard off | 1 and 4 | identical |
| fix, guard on | 1 and 4 | identical |

Fix with the guard off matches fix with the guard on, and both match control with the guard on. Those three are the VNNI path. Control with the guard off is AMX: the 1-thread quiet run matched VNNI, and 196 of 200 four-thread runs under load did not.

## Wall time

`embed_ms` is only `Session::Run` plus the pool, not session init. Quiet numbers are the median of 10 runs after one warmup, no `stress-ng`. Load average at the start of that block was 0.69 1.60 1.76.

| path | threads | median ms | min | max |
| --- | --- | --- | --- | --- |
| control AMX (guard off) | 4 | 80.990 | 75.174 | 99.254 |
| control VNNI (guard on) | 4 | 89.870 | 76.728 | 127.339 |
| fix VNNI (guard off) | 4 | 79.274 | 75.889 | 88.928 |
| fix VNNI (guard on) | 4 | 78.416 | 75.884 | 88.285 |
| control AMX (guard off) | 1 | 214.104 | 209.595 | 307.199 |
| control VNNI (guard on) | 1 | 265.639 | 253.996 | 338.960 |
| fix VNNI (guard off) | 1 | 268.089 | 254.939 | 280.760 |
| fix VNNI (guard on) | 1 | 260.111 | 256.274 | 269.434 |

The 4-thread quiet ranges overlap, so this run does not establish an AMX versus VNNI speedup. Under `stress-ng` the four 200-run medians were 273.923, 331.437, 328.586, and 302.704 ms, while load average climbed from about 1 to about 7, so those figures are not a kernel comparison.

## Not measured

A host where the tile-state probe succeeds. The fix build on this VM did not take AMX, so there is no timing or determinism number for preserved-tile AMX.
