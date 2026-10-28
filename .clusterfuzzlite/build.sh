#!/bin/bash -eu
# Parser and tokenizer only. A full `conan install` also builds onnxruntime
# (boost, protobuf, eigen, ...) and the project CMakeLists requires CMake >= 4.2
# while base-builder ships 3.29. nlohmann_json 3.11.3 is header-only and is the
# only dependency those two targets link; the commit matches the v3.11.3 tag
# recorded in conanfile.py / conan.lock.

JSON_COMMIT=9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
JSON_SHA256=9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6
json_dir=/tmp/nlohmann/nlohmann
json_header="${json_dir}/json.hpp"
mkdir -p "${json_dir}"
curl -fsSL \
  "https://raw.githubusercontent.com/nlohmann/json/${JSON_COMMIT}/single_include/nlohmann/json.hpp" \
  -o "${json_header}"
echo "${JSON_SHA256}  ${json_header}" | sha256sum -c -

root="${SRC}/nesso"
flags="${CXXFLAGS:-} -std=c++26 -DNESSO_LIBFUZZER -I/tmp/nlohmann"

"${CXX}" ${flags} \
  -I"${root}/src/parser/include" \
  "${root}/src/parser/log_chunker.cpp" \
  "${root}/src/parser/template_mask.cpp" \
  "${root}/test/fuzz/fuzz_parser.cpp" \
  ${LIB_FUZZING_ENGINE} \
  -o "${OUT}/nesso_fuzz_parser"

"${CXX}" ${flags} \
  -I"${root}/src/embed/include" \
  "${root}/src/embed/wordpiece_tokenizer.cpp" \
  "${root}/test/fuzz/fuzz_tokenizer.cpp" \
  ${LIB_FUZZING_ENGINE} \
  -o "${OUT}/nesso_fuzz_tokenizer"

python3 - "${root}/test/fuzz/seeds/parser" "${OUT}/nesso_fuzz_parser_seed_corpus.zip" <<'PY'
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
