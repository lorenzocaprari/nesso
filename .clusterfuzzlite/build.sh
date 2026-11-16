#!/bin/bash -eu
# Parser, tokenizer, archive reader, and corpus reader. A full `conan install`
# also builds onnxruntime (boost, protobuf, eigen, ...) and the project
# CMakeLists requires CMake >= 4.2 while base-builder ships 3.29.
# nlohmann_json 3.11.3 is header-only and is the only dependency the tokenizer
# links. The parser and archive fuzzers also link zlib (system libz; the product
# build uses the locked Conan package). The corpus fuzzer links none of those.
# The json commit matches the v3.11.3 tag recorded in conanfile.py / conan.lock.

JSON_COMMIT=9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
JSON_SHA256=9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6
JSON_FWD_SHA256=5cefbba751baf5243033fd894c70be9b37103d64b3d3a959e4173197d71137b9
json_dir=/tmp/nlohmann/nlohmann
json_header="${json_dir}/json.hpp"
json_fwd_header="${json_dir}/json_fwd.hpp"
mkdir -p "${json_dir}"
curl -fsSL \
  "https://raw.githubusercontent.com/nlohmann/json/${JSON_COMMIT}/single_include/nlohmann/json.hpp" \
  -o "${json_header}"
echo "${JSON_SHA256}  ${json_header}" | sha256sum -c -
curl -fsSL \
  "https://raw.githubusercontent.com/nlohmann/json/${JSON_COMMIT}/single_include/nlohmann/json_fwd.hpp" \
  -o "${json_fwd_header}"
echo "${JSON_FWD_SHA256}  ${json_fwd_header}" | sha256sum -c -

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
# Extensive libc++ checks cover iterator, span, and string bounds that the fast
# production mode skips. Debug mode adds checks that dominate fuzzing time.
# -U drops a mode CXXFLAGS may already have set, so the -D does not warn.
flags="${CXXFLAGS:-} -std=c++26 -DNESSO_LIBFUZZER -U_LIBCPP_HARDENING_MODE -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE -I/tmp/nlohmann"

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

# Corpus reader only. The writer, parser, and ONNX embedder stay out of this
# binary. Checksum fixup lives in the harness; MapCorpusFile still verifies it.
"${CXX}" ${flags} \
  -I"${root}/src/core/include" \
  -I"${root}/src/core" \
  -I"${root}/src/platform/include" \
  "${root}/src/platform/assert.cpp" \
  "${root}/src/platform/file.cpp" \
  "${root}/src/core/sha256.cpp" \
  "${root}/src/core/corpus_format.cpp" \
  "${root}/src/core/corpus_policy.cpp" \
  "${root}/src/core/corpus_lexical.cpp" \
  "${root}/src/core/corpus_reader.cpp" \
  "${root}/test/fuzz/fuzz_corpus.cpp" \
  ${LIB_FUZZING_ENGINE} \
  -o "${OUT}/nesso_fuzz_corpus"

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

python3 - "${root}/test/fuzz/seeds/corpus" "${OUT}/nesso_fuzz_corpus_seed_corpus.zip" <<'PY'
import sys
import zipfile
from pathlib import Path

src, dest = Path(sys.argv[1]), Path(sys.argv[2])
with zipfile.ZipFile(dest, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(item for item in src.iterdir() if item.is_file()):
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

# libc++ availability of <experimental/simd>. The Catch2 test is not built
# here. The binary is not a fuzz target and is not linked into a harness.
probe_bin="$(mktemp)"
"${CXX}" ${flags} -DNESSO_SIMD_PROBE_STANDALONE \
  "${root}/test/unit/platform/experimental_simd_probe_test.cpp" \
  -o "${probe_bin}"
"${probe_bin}"
rm -f "${probe_bin}"
