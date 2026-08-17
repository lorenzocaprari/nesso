# VecGrep models

Checked-in MiniLM artifacts (Apache-2.0, from [sentence-transformers/all-MiniLM-L6-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2)):

* `all-MiniLM-L6-v2-int8.onnx` — official `onnx/model_qint8_avx512.onnx` (~22 MB)
* `vocab.txt` — BERT WordPiece vocabulary for that checkpoint

```bash
vecgrep index --log-file robot.log --path /tmp/robot.vecgrep \
  --model models/all-MiniLM-L6-v2-int8.onnx --vocab models/vocab.txt
```

CI still uses `test/fixtures/tiny_minilm.onnx` and does not download weights.
