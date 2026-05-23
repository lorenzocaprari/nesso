FROM ubuntu:26.04

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies, GCC 15, native Clang-Tidy 21, and lcov for CI coverage
RUN apt-get update && apt-get install -y \
    build-essential \
    software-properties-common wget curl git jq ccache python3-pip \
    cmake ninja-build lcov \
    && add-apt-repository ppa:ubuntu-toolchain-r/test -y \
    && apt-get update && apt-get install -y \
    gcc-15 g++-15 clang-tidy-21

# Set GCC 15 and LLVM 21 as defaults
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-15 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-15 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-21 100 \
    && update-alternatives --install /usr/bin/run-clang-tidy run-clang-tidy /usr/bin/run-clang-tidy-21 100

# Install Conan 2 and pre-commit
RUN pip install conan pre-commit --break-system-packages

LABEL org.opencontainers.image.source=https://github.com/lorenzocaprari/mach1
