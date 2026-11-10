#!/bin/bash -eu
# Parser and tokenizer only. A full `conan install` also builds onnxruntime
# (boost, protobuf, eigen, ...) and the project CMakeLists requires CMake >= 4.2
# while base-builder ships 3.29. nlohmann_json 3.11.3 is header-only and is the
# only dependency the tokenizer links. The parser fuzzer also links zlib for
# gzip input (system libz; the product build uses the locked Conan package).
# The json commit matches the v3.11.3 tag recorded in conanfile.py / conan.lock.

JSON_COMMIT=9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
JSON_SHA256=9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6
json_dir=/tmp/nlohmann/nlohmann
json_header="${json_dir}/json.hpp"
mkdir -p "${json_dir}"
curl -fsSL \
  "https://raw.githubusercontent.com/nlohmann/json/${JSON_COMMIT}/single_include/nlohmann/json.hpp" \
  -o "${json_header}"
echo "${JSON_SHA256}  ${json_header}" | sha256sum -c -

# gzip_reader links zlib. archive_source links libarchive and liblzma.
# The bad-build check runs outside this image, so libarchive is built static
# with only zlib and lzma. A shared libarchive.so is not on that host.
if [[ ! -f /usr/include/zlib.h || ! -f /usr/include/lzma.h ]]; then
  apt-get update
  apt-get install -y zlib1g-dev liblzma-dev
fi
if ! command -v cmake >/dev/null 2>&1; then
  apt-get update
  apt-get install -y cmake
fi

LA_VERSION=3.8.7
LA_SHA256=d3a8ba457ae25c27c84fd2830a2efdcc5b1d40bf585d4eb0d35f47e99e5d4774
LA_SRC=/tmp/libarchive-${LA_VERSION}
LA_PREFIX=/tmp/libarchive-install
curl -fsSL \
  "https://github.com/libarchive/libarchive/releases/download/v${LA_VERSION}/libarchive-${LA_VERSION}.tar.xz" \
  -o /tmp/libarchive.tar.xz
echo "${LA_SHA256}  /tmp/libarchive.tar.xz" | sha256sum -c -
tar -C /tmp -xf /tmp/libarchive.tar.xz
cmake -S "${LA_SRC}" -B /tmp/libarchive-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${LA_PREFIX}" \
  -DBUILD_SHARED_LIBS=OFF \
  -DENABLE_TEST=OFF \
  -DENABLE_TAR=OFF \
  -DENABLE_CPIO=OFF \
  -DENABLE_CAT=OFF \
  -DENABLE_UNZIP=OFF \
  -DENABLE_WERROR=OFF \
  -DENABLE_ZLIB=ON \
  -DENABLE_LZMA=ON \
  -DENABLE_BZip2=OFF \
  -DENABLE_LZ4=OFF \
  -DENABLE_ZSTD=OFF \
  -DENABLE_LIBXML2=OFF \
  -DENABLE_EXPAT=OFF \
  -DENABLE_ICONV=OFF \
  -DENABLE_ACL=OFF \
  -DENABLE_XATTR=OFF \
  -DENABLE_OPENSSL=OFF \
  -DENABLE_NETTLE=OFF \
  -DENABLE_LIBB2=OFF \
  -DENABLE_PCREPOSIX=OFF \
  -DENABLE_PCRE2POSIX=OFF \
  -DENABLE_MBEDTLS=OFF \
  -DENABLE_CNG=OFF
cmake --build /tmp/libarchive-build --parallel "$(nproc)"
cmake --install /tmp/libarchive-build

# archive.h is the prefix just installed. zlib.h and lzma.h are the system
# headers that static libz and liblzma were built with. Every target that
# compiles archive_source.cpp (and gzip_reader.cpp next to it) gets all three.
header_dir() {
  local name="$1"
  local found
  found="$(find "${LA_PREFIX}/include" /usr/include -name "${name}" -print -quit)"
  if [[ -z "${found}" ]]; then
    printf 'error: %s was not installed for the fuzz build\n' "${name}" >&2
    exit 1
  fi
  dirname "${found}"
}
archive_includes=(
  -I"$(header_dir archive.h)"
  -I"$(header_dir zlib.h)"
  -I"$(header_dir lzma.h)"
)
archive_link=(-L"${LA_PREFIX}/lib" -Wl,-Bstatic -larchive -llzma -lz -Wl,-Bdynamic)

root="${SRC}/nesso"
flags="${CXXFLAGS:-} -std=c++26 -DNESSO_LIBFUZZER -I/tmp/nlohmann"

"${CXX}" ${flags} \
  "${archive_includes[@]}" \
  -I"${root}/src/parser/include" \
  "${root}/src/parser/log_chunker.cpp" \
  "${root}/src/parser/template_mask.cpp" \
  "${root}/src/parser/gzip_reader.cpp" \
  "${root}/src/parser/archive_source.cpp" \
  "${root}/test/fuzz/fuzz_parser.cpp" \
  ${LIB_FUZZING_ENGINE} \
  "${archive_link[@]}" \
  -o "${OUT}/nesso_fuzz_parser"

"${CXX}" ${flags} \
  -I"${root}/src/embed/include" \
  "${root}/src/embed/wordpiece_tokenizer.cpp" \
  "${root}/test/fuzz/fuzz_tokenizer.cpp" \
  ${LIB_FUZZING_ENGINE} \
  -o "${OUT}/nesso_fuzz_tokenizer"

"${CXX}" ${flags} \
  "${archive_includes[@]}" \
  -I"${root}/src/parser/include" \
  "${root}/src/parser/gzip_reader.cpp" \
  "${root}/src/parser/archive_source.cpp" \
  "${root}/test/fuzz/fuzz_archive_source.cpp" \
  ${LIB_FUZZING_ENGINE} \
  "${archive_link[@]}" \
  -o "${OUT}/nesso_fuzz_archive_source"

python3 - "${root}/test/fuzz/seeds/parser" "${OUT}/nesso_fuzz_parser_seed_corpus.zip" <<'PY'
import sys
import zipfile
from pathlib import Path

src, dest = Path(sys.argv[1]), Path(sys.argv[2])
with zipfile.ZipFile(dest, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(item for item in src.iterdir() if item.is_file()):
        archive.write(path, arcname=path.name)
PY

python3 - "${root}/test/fuzz/seeds/archive" "${OUT}/nesso_fuzz_archive_source_seed_corpus.zip" <<'PY'
import sys
import zipfile
from pathlib import Path

src, dest = Path(sys.argv[1]), Path(sys.argv[2])
with zipfile.ZipFile(dest, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(item for item in src.iterdir() if item.is_file() and item.suffix != ".py"):
        archive.write(path, arcname=path.name)
PY

python3 - "${root}/test/fuzz/seeds/tokenizer" "${OUT}/nesso_fuzz_tokenizer_seed_corpus.zip" <<'PY'
import sys
import zipfile
from pathlib import Path

src, dest = Path(sys.argv[1]), Path(sys.argv[2])
with zipfile.ZipFile(dest, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(item for item in src.iterdir() if item.is_file()):
        archive.write(path, arcname=path.name)
PY
