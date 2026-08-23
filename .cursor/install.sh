#!/usr/bin/env bash
# Cloud Agent install: refresh Conan deps and compile the Debug (ASan/UBSan)
# build that the README and CI treat as the development default. The base image
# (.devcontainer/Dockerfile) bakes every Conan dependency into CONAN_HOME, so
# --no-remote reuses that cache and avoids ConanCenter recipe drift.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

conan profile detect --force

conan install . \
  -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing --no-remote

conan build . \
  -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing --no-remote

echo "install: Debug build ready at build/Debug/mach1"
