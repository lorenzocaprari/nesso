FROM ubuntu:26.04@sha256:f144425ff09be612d6d9ad965196e9cdc23dae1f42110a8a11a3e9a8198759f7

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies, GCC 15, Clang 21 (+ compiler-rt for ASan/AFL), tidy, cppcheck, lcov
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    software-properties-common wget curl git jq python3-pip \
    cmake ninja-build lcov cppcheck \
    && add-apt-repository ppa:ubuntu-toolchain-r/test -y \
    && apt-get update && apt-get install -y --no-install-recommends \
    gcc-15 g++-15 \
    clang-21 clang-tidy-21 libclang-rt-21-dev \
    afl++ \
    && rm -rf /var/lib/apt/lists/*

# Set GCC 15 and LLVM 21 as defaults
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-15 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-15 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-21 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-21 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-21 100 \
    && update-alternatives --install /usr/bin/run-clang-tidy run-clang-tidy /usr/bin/run-clang-tidy-21 100

# Ubuntu ships compiler-rt under lib/linux/*-x86_64.a; some Clang drivers look for
# lib/<triple>/libclang_rt.*.a (unprefixed). Bridge so AFL_USE_ASAN links cleanly.
RUN rt_lib="/usr/lib/llvm-21/lib/clang/21/lib" \
    && if [ -d "${rt_lib}/linux" ] && [ ! -e "${rt_lib}/x86_64-pc-linux-gnu" ]; then \
         mkdir -p "${rt_lib}/x86_64-pc-linux-gnu" \
         && for kind in asan asan_cxx asan_static asan-preinit \
              ubsan_standalone ubsan_standalone_cxx fuzzer fuzzer_no_main; do \
              for src in \
                "${rt_lib}/linux/libclang_rt.${kind}-x86_64.a" \
                "${rt_lib}/linux/libclang_rt.${kind}.a"; do \
                if [ -f "${src}" ]; then \
                  ln -sfn "${src}" "${rt_lib}/x86_64-pc-linux-gnu/libclang_rt.${kind}.a"; \
                  break; \
                fi; \
              done; \
            done; \
       fi

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
RUN conan profile detect --force \
    && conan remote add conancenter https://center2.conan.io --force \
    && conan install . -pr:h=conan/profiles/gcc-26 -pr:b=default --lockfile=conan.lock --build=missing \
    && conan cache clean "*" --build --source --download --temp

COPY conan/profiles/gcc-26-debug /tmp/nesso/conan/profiles/gcc-26-debug
RUN conan install . -pr:h=conan/profiles/gcc-26-debug -pr:b=default --lockfile=conan.lock --build=missing \
    && conan cache clean "*" --build --source --download --temp \
    && rm -rf /tmp/nesso /tmp/* /var/tmp/*
WORKDIR /

# After the Conan installs. A change to ccache, Go, stress-ng, or zstd must
# not rebuild those layers. GitHub Actions sets HOME to /github/home, so the
# godog module cache cannot live under /root.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ccache \
    golang-go \
    stress-ng \
    zstd \
    && rm -rf /var/lib/apt/lists/*

ENV GOPATH=/opt/go
ENV GOMODCACHE=/opt/go/pkg/mod
COPY test/functional/go.mod test/functional/go.sum /tmp/nesso-go/
RUN cd /tmp/nesso-go \
    && GOTOOLCHAIN=local GOPROXY=https://proxy.golang.org,direct go mod download \
    && chmod -R a+rwX /opt/go \
    && rm -rf /tmp/nesso-go

LABEL org.opencontainers.image.source=https://github.com/lorenzocaprari/nesso
