FROM ubuntu:26.04

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Toolchain only: Conan packages are restored via GHA cache in CI, not baked in.
# Node is required for JavaScript Actions (checkout, cache) inside container jobs.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    software-properties-common wget curl git jq ccache python3-pip \
    cmake ninja-build lcov nodejs \
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

LABEL org.opencontainers.image.source=https://github.com/lorenzocaprari/mach1
