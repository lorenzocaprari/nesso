FROM ubuntu:26.04@sha256:d66b366dac61b575bee2aef07e9c26e8070096fbe84a3d4d3eb0da069b5d76db

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies, GCC 15, AFL++ (LLVM 21), cppcheck, lcov.
# afl++ depends on clang-21. libclang-rt-21-dev is the ASan runtime that
# AFL_USE_ASAN links. Clang 23.1.6 is installed from apt.llvm.org below.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    software-properties-common wget curl git jq python3-pip \
    cmake ninja-build lcov cppcheck \
    && add-apt-repository ppa:ubuntu-toolchain-r/test -y \
    && apt-get update && apt-get install -y --no-install-recommends \
    gcc-15 g++-15 \
    libclang-rt-21-dev \
    afl++ \
    && rm -rf /var/lib/apt/lists/*

# apt.llvm.org signing key fingerprint:
# 6084 F3CF 814B 57C1 CF12 EFD5 15CF 4D18 AF4F 7421
# Same key and resolute suite as .devcontainer/Dockerfile.clang23.
# The image fails unless clang++-23 reports 23.1.6.
RUN apt-get update && apt-get install -y --no-install-recommends gnupg ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && curl --proto '=https' --tlsv1.2 -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key -o /tmp/llvm-snapshot.gpg.key \
    && gpg --show-keys --with-colons /tmp/llvm-snapshot.gpg.key \
      | grep -Eq '^fpr:.*6084F3CF814B57C1CF12EFD515CF4D18AF4F7421:' \
    && install -d /usr/share/keyrings \
    && gpg --dearmor -o /usr/share/keyrings/apt.llvm.org.gpg /tmp/llvm-snapshot.gpg.key \
    && rm -f /tmp/llvm-snapshot.gpg.key \
    && printf '%s\n' \
      'deb [signed-by=/usr/share/keyrings/apt.llvm.org.gpg] https://apt.llvm.org/resolute/ llvm-toolchain-resolute-23 main' \
      > /etc/apt/sources.list.d/llvm-toolchain-resolute-23.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends -t llvm-toolchain-resolute-23 \
      clang-23 \
      clang-tidy-23 \
      libclang-rt-23-dev \
    && rm -rf /var/lib/apt/lists/* \
    && clang++-23 --version \
    && clang++-23 --version | grep -q '23\.1\.6'

# Set GCC 15 and Clang 23.1.6 as defaults. afl++ still invokes clang-21.
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-15 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-15 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-23 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-23 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-23 100 \
    && update-alternatives --install /usr/bin/run-clang-tidy run-clang-tidy /usr/bin/run-clang-tidy-23 100

# Ubuntu ships compiler-rt under lib/linux/*-x86_64.a; some Clang drivers look for
# lib/<triple>/libclang_rt.*.a (unprefixed). Bridge LLVM 21 so AFL_USE_ASAN links,
# and LLVM 23 so the Clang ASan/TSan lanes link (tsan included).
RUN set -eu; \
    bridge() { \
      rt_lib="$1"; \
      if [ ! -d "${rt_lib}/linux" ]; then \
        echo "compiler-rt linux dir missing: ${rt_lib}" >&2; \
        return 1; \
      fi; \
      if [ ! -e "${rt_lib}/x86_64-pc-linux-gnu" ]; then \
        mkdir -p "${rt_lib}/x86_64-pc-linux-gnu"; \
        for kind in asan asan_cxx asan_static asan-preinit \
          ubsan_standalone ubsan_standalone_cxx tsan tsan_cxx \
          fuzzer fuzzer_no_main; do \
          for src in \
            "${rt_lib}/linux/libclang_rt.${kind}-x86_64.a" \
            "${rt_lib}/linux/libclang_rt.${kind}.a"; do \
            if [ -f "${src}" ]; then \
              ln -sfn "${src}" "${rt_lib}/x86_64-pc-linux-gnu/libclang_rt.${kind}.a"; \
              break; \
            fi; \
          done; \
        done; \
      fi; \
    }; \
    bridge /usr/lib/llvm-21/lib/clang/21/lib; \
    found23=0; \
    for rt_lib in /usr/lib/llvm-23/lib/clang/*/lib; do \
      if [ -d "${rt_lib}/linux" ]; then \
        bridge "${rt_lib}"; \
        found23=1; \
      fi; \
    done; \
    test "${found23}" -eq 1

# Install Conan 2 and pre-commit. Hashes are required; Dependabot updates them.
COPY .devcontainer/requirements.txt /tmp/nesso-pip-requirements.txt
# --ignore-installed: Ubuntu's python3-pip ships Debian packaging without a
# pip RECORD, so upgrading it fails unless pip overlays instead of uninstalling.
RUN pip install --require-hashes --break-system-packages --ignore-installed \
      -r /tmp/nesso-pip-requirements.txt \
    && rm -rf /root/.cache/pip /tmp/nesso-pip-requirements.txt

# Fixed path: GitHub Actions container jobs override HOME to /github/home, so a
# cache under ~/.conan2 is invisible to CI. Bake and consume from CONAN_HOME.
ENV CONAN_HOME=/opt/conan2

# Prebuild Conan deps into the image package cache.
# Release and Debug are separate layers so BuildKit can reuse independently.
# Coverage reuses the Debug package ID (consumer-only --coverage flags).
# Drop build/source/download trees after each install to keep peak disk down.
# Remote is only for this bake; CI jobs use --no-remote against this cache.
WORKDIR /tmp/nesso
COPY conanfile.py /tmp/nesso/conanfile.py
COPY conan.lock /tmp/nesso/conan.lock
COPY conan/profiles/gcc-26 /tmp/nesso/conan/profiles/gcc-26
COPY conan/global.conf /tmp/nesso/conan/global.conf
RUN conan profile detect --force \
    && cat conan/global.conf >> "${CONAN_HOME}/global.conf" \
    && conan remote add conancenter https://center2.conan.io --force \
    && conan install . -pr:h=conan/profiles/gcc-26 -pr:b=default --lockfile=conan.lock --build=missing \
    && conan cache clean "*" --build --source --download --temp

COPY conan/profiles/gcc-26-debug /tmp/nesso/conan/profiles/gcc-26-debug
RUN conan install . -pr:h=conan/profiles/gcc-26-debug -pr:b=default --lockfile=conan.lock --build=missing \
    && conan cache clean "*" --build --source --download --temp \
    && rm -rf /tmp/nesso /tmp/* /var/tmp/*
WORKDIR /

# After the Conan installs so this layer can change without rebuilding them.
# afl++ is repeated from the compiler layer above. Ubuntu's afl++ is built
# against LLVM 21, so scripts/fuzz-test needs ld.lld-21, llvm-ar-21, and
# llvm-ranlib-21. lld-23 and llvm-23 are the Clang 23.1.6 toolchain (the
# apt.llvm.org list from the compiler layer). GNU time is /usr/bin/time for
# scripts/bench. The arm64 image does not run that job. GitHub Actions sets
# HOME to /github/home, so the godog module cache cannot live under /root.
RUN apt-get update && apt-get install -y --no-install-recommends \
    afl++ \
    ccache \
    golang-go \
    lld-21 \
    llvm-21 \
    stress-ng \
    time \
    zstd \
    && apt-get install -y --no-install-recommends -t llvm-toolchain-resolute-23 \
      lld-23 \
      llvm-23 \
    && rm -rf /var/lib/apt/lists/*

ENV GOPATH=/opt/go
ENV GOMODCACHE=/opt/go/pkg/mod
COPY test/functional/go.mod test/functional/go.sum /tmp/nesso-go/
RUN cd /tmp/nesso-go \
    && GOTOOLCHAIN=local GOPROXY=https://proxy.golang.org,direct go mod download \
    && chmod -R a+rwX /opt/go \
    && rm -rf /tmp/nesso-go

LABEL org.opencontainers.image.source=https://github.com/lorenzocaprari/nesso
