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

# Prebuild Conan deps (onnxruntime + graph) into the image package cache.
# Coverage reuses the Debug package ID after consumer-only flag split.
# Drop build/source/download trees after each install so peak disk stays under
# GHA runner limits (ORT object files alone can fill the disk).
COPY conanfile.py /tmp/mach1/conanfile.py
COPY conan/profiles /tmp/mach1/conan/profiles
WORKDIR /tmp/mach1
RUN conan profile detect --force \
    && conan install . -pr:h=conan/profiles/gcc-26 -pr:b=default --build=missing \
    && conan cache clean "*" --build --source --download --temp \
    && conan install . -pr:h=conan/profiles/gcc-26-debug -pr:b=default --build=missing \
    && conan cache clean "*" --build --source --download --temp \
    && rm -rf /tmp/mach1 /tmp/* /var/tmp/*
WORKDIR /

LABEL org.opencontainers.image.source=https://github.com/lorenzocaprari/mach1
