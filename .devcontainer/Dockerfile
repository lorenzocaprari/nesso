FROM ubuntu:26.04

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies, GCC 15, native Clang-Tidy 21, and lcov for CI coverage
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    software-properties-common wget curl git jq ccache python3-pip \
    cmake ninja-build lcov \
    && add-apt-repository ppa:ubuntu-toolchain-r/test -y \
    && apt-get update && apt-get install -y --no-install-recommends \
    gcc-15 g++-15 clang-tidy-21 \
    && rm -rf /var/lib/apt/lists/*

# Set GCC 15 and LLVM 21 as defaults
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-15 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-15 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-21 100 \
    && update-alternatives --install /usr/bin/run-clang-tidy run-clang-tidy /usr/bin/run-clang-tidy-21 100

# Install Conan 2 and pre-commit
RUN pip install conan pre-commit --break-system-packages \
    && rm -rf /root/.cache/pip

# Fixed path: GitHub Actions container jobs override HOME to /github/home, so a
# cache under ~/.conan2 is invisible to CI. Bake and consume from CONAN_HOME.
ENV CONAN_HOME=/opt/conan2

# Prebuild Conan deps into the image package cache.
# Release and Debug are separate layers so BuildKit can reuse independently.
# Coverage reuses the Debug package ID (consumer-only --coverage flags).
# Drop build/source/download trees after each install to keep peak disk down.
# Remote is only for this bake; CI jobs use --no-remote against this cache.
WORKDIR /tmp/mach1
COPY conanfile.py /tmp/mach1/conanfile.py
COPY conan.lock /tmp/mach1/conan.lock
COPY conan/profiles/gcc-26 /tmp/mach1/conan/profiles/gcc-26
RUN conan profile detect --force \
    && conan remote add conancenter https://center2.conan.io --force \
    && conan install . -pr:h=conan/profiles/gcc-26 -pr:b=default --lockfile=conan.lock --build=missing \
    && conan cache clean "*" --build --source --download --temp

COPY conan/profiles/gcc-26-debug /tmp/mach1/conan/profiles/gcc-26-debug
RUN conan install . -pr:h=conan/profiles/gcc-26-debug -pr:b=default --lockfile=conan.lock --build=missing \
    && conan cache clean "*" --build --source --download --temp \
    && rm -rf /tmp/mach1 /tmp/* /var/tmp/*
WORKDIR /

LABEL org.opencontainers.image.source=https://github.com/lorenzocaprari/mach1
