# Scripts

Contributor-facing helpers live in this directory. Pipeline and packaging
helpers live elsewhere so a local `ls` stays short.

| Location | Use |
| --- | --- |
| `scripts/` | Local gates and day-to-day tooling (`lint`, `fetch-model`, `bench`, …) |
| `scripts/ci/` | Pipeline helpers (Go `ci` binary, `build-env-hash`, `ci-build-archive`, `download-pinned`) |
| `packaging/scripts/` | Release `.deb`, runtime image, Trivy, and license/notice gates |

`scripts/check-abi-baseline BINARY` fails when `objdump -p` shows `GLIBC_` above 2.34, `GLIBCXX_` above 3.4.29, or `CXXABI_` above 1.3.13, a referenced version the providing library does not define, a `NEEDED` entry outside the baseline set (`libc.so.6`, `libm.so.6`, `libstdc++.so.6`, `libgcc_s.so.1`, `libgomp.so.1`, `ld-linux-x86-64.so.2`, `ld-linux-aarch64.so.1`), or a `PT_INTERP` other than `/lib64/ld-linux-x86-64.so.2` (x86_64) or `/lib/ld-linux-aarch64.so.1` (aarch64). `OBJDUMP` and `NESSO_LIB_RESOLVE` override objdump and library lookup. `packaging/scripts/build-release-binary` runs the check when `NESSO_ABI_BASELINE` is set, and then uses `conan/profiles/gcc-26-el9` or `conan/profiles/gcc-26-el9-armv8`.

See [AGENTS.md](../AGENTS.md) and [CONTRIBUTING.md](../CONTRIBUTING.md) for the commands to run before a push.
