---
applyTo: "src/parser/**,src/embed/wordpiece_tokenizer.*,src/embed/include/embed/wordpiece_tokenizer.hpp,test/fuzz/**,.clusterfuzzlite/**"
---

For each crash you fix, add a seed in the matching directory (`test/fuzz/seeds/parser` or `test/fuzz/seeds/tokenizer`) and a Catch2 regression in the matching test (`test/unit/parser/log_chunker_test.cpp` or `test/unit/tokenizer/wordpiece_tokenizer_test.cpp`).
