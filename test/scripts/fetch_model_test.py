import hashlib
import pathlib
import re
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "scripts" / "fetch-model"
SOURCE_TEXT = SOURCE.read_text(encoding="utf-8")
XENOVA_COMMIT = re.search(r'^XENOVA_COMMIT="([0-9a-f]{40})"$', SOURCE_TEXT, re.MULTILINE).group(1)
SENTENCE_TRANSFORMERS_COMMIT = re.search(
    r'^SENTENCE_TRANSFORMERS_COMMIT="([0-9a-f]{40})"$', SOURCE_TEXT, re.MULTILINE
).group(1)

VOCAB = b"[PAD]\n[UNK]\n"
INT8_MODEL = b"int8 fixture\n"
FP32_MODEL = b"fp32 fixture\n"
PAYLOADS = {
    "VOCAB_SHA256": hashlib.sha256(VOCAB).hexdigest(),
    "INT8_MODEL_SHA256": hashlib.sha256(INT8_MODEL).hexdigest(),
    "FP32_MODEL_SHA256": hashlib.sha256(FP32_MODEL).hexdigest(),
}


def replace_sha(text: str, name: str, value: str, occurrence: int = 0) -> str:
    pattern = re.compile(rf'^([ \t]*){name}="[0-9a-f]{{64}}"$', re.MULTILINE)
    matches = list(pattern.finditer(text))
    if occurrence >= len(matches):
        raise AssertionError(f"missing {name} assignment {occurrence}")
    match = matches[occurrence]
    replacement = f'{match.group(1)}{name}="{value}"'
    return text[: match.start()] + replacement + text[match.end() :]


def write_fixture(base: pathlib.Path, commit: str, relative: str, payload: bytes) -> pathlib.Path:
    path = base / "resolve" / commit / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return path


def make_test_script(directory: pathlib.Path, bad_int8_hash: bool = False) -> pathlib.Path:
    xenova_base = directory / "Xenova" / "all-MiniLM-L6-v2"
    sentence_base = directory / "sentence-transformers" / "all-MiniLM-L6-v2"
    write_fixture(xenova_base, XENOVA_COMMIT, "onnx/model_quantized.onnx", INT8_MODEL)
    write_fixture(xenova_base, XENOVA_COMMIT, "onnx/model.onnx", FP32_MODEL)
    write_fixture(sentence_base, SENTENCE_TRANSFORMERS_COMMIT, "vocab.txt", VOCAB)

    text = SOURCE_TEXT.replace(
        'XENOVA_BASE_URL="https://huggingface.co/Xenova/all-MiniLM-L6-v2"',
        f'XENOVA_BASE_URL="{xenova_base.as_uri()}"',
    ).replace(
        'SENTENCE_TRANSFORMERS_BASE_URL="https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2"',
        f'SENTENCE_TRANSFORMERS_BASE_URL="{sentence_base.as_uri()}"',
    )
    text = replace_sha(text, "VOCAB_SHA256", PAYLOADS["VOCAB_SHA256"])
    text = replace_sha(text, "MODEL_SHA256", "0" * 64 if bad_int8_hash else PAYLOADS["INT8_MODEL_SHA256"], 0)
    text = replace_sha(text, "MODEL_SHA256", PAYLOADS["FP32_MODEL_SHA256"], 1)

    script = directory / "fetch-model"
    script.write_text(text, encoding="utf-8")
    script.chmod(0o755)
    return script


def run_fetch(script: pathlib.Path, model_dir: pathlib.Path, variant: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(script), str(model_dir), variant],
        capture_output=True,
        text=True,
        check=False,
    )


class FetchModelTest(unittest.TestCase):
    def test_artifacts_are_pinned_to_commits_and_expected_hashes(self) -> None:
        self.assertEqual(XENOVA_COMMIT, "751bff37182d3f1213fa05d7196b954e230abad9")
        self.assertEqual(SENTENCE_TRANSFORMERS_COMMIT, "1110a243fdf4706b3f48f1d95db1a4f5529b4d41")
        self.assertIn(f'resolve/${{XENOVA_COMMIT}}/onnx/model_quantized.onnx', SOURCE_TEXT)
        self.assertIn(f'resolve/${{XENOVA_COMMIT}}/onnx/model.onnx', SOURCE_TEXT)
        self.assertIn(f'resolve/${{SENTENCE_TRANSFORMERS_COMMIT}}/vocab.txt', SOURCE_TEXT)
        self.assertNotIn("/resolve/main/", SOURCE_TEXT)
        self.assertEqual(
            re.findall(r'^[ \t]*MODEL_SHA256="([0-9a-f]{64})"$', SOURCE_TEXT, re.MULTILINE),
            [
                "afdb6f1a0e45b715d0bb9b11772f032c399babd23bfc31fed1c170afc848bdb1",
                "759c3cd2b7fe7e93933ad23c4c9181b7396442a2ed746ec7c1d46192c469c46e",
            ],
        )
        self.assertRegex(
            SOURCE_TEXT,
            re.compile(r'^VOCAB_SHA256="07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3"$', re.MULTILINE),
        )

    def test_fetches_verified_int8_and_fp32_artifacts(self) -> None:
        for variant, payload in (("int8", INT8_MODEL), ("fp32", FP32_MODEL)):
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temp:
                directory = pathlib.Path(temp)
                script = make_test_script(directory)
                model_dir = directory / "models"
                result = run_fetch(script, model_dir, variant)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((model_dir / "model.onnx").read_bytes(), payload)
                self.assertEqual((model_dir / "vocab.txt").read_bytes(), VOCAB)
                self.assertEqual((model_dir / "model.onnx").stat().st_mode & 0o777, 0o644)

    def test_valid_cached_files_skip_downloads(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            script = make_test_script(directory)
            model_dir = directory / "models"
            first = run_fetch(script, model_dir, "int8")
            self.assertEqual(first.returncode, 0, first.stderr)

            for path in directory.glob("**/resolve/**"):
                if path.is_file():
                    path.unlink()
            second = run_fetch(script, model_dir, "int8")
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertIn("cached vocab.txt", second.stdout)
            self.assertIn("cached int8 model.onnx", second.stdout)

    def test_stale_cached_model_is_replaced_only_after_verification(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            script = make_test_script(directory)
            model_dir = directory / "models"
            model_dir.mkdir()
            (model_dir / "model.onnx").write_bytes(b"stale model\n")

            result = run_fetch(script, model_dir, "int8")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("downloaded int8 model.onnx", result.stdout)
            self.assertEqual((model_dir / "model.onnx").read_bytes(), INT8_MODEL)

    def test_hash_mismatch_fails_without_replacing_cached_model(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            script = make_test_script(directory, bad_int8_hash=True)
            model_dir = directory / "models"
            model_dir.mkdir()
            stale_model = b"old model\n"
            (model_dir / "model.onnx").write_bytes(stale_model)

            result = run_fetch(script, model_dir, "int8")
            self.assertEqual(result.returncode, 1)
            self.assertIn("sha256 mismatch for int8 model.onnx", result.stderr)
            self.assertIn("expected", result.stderr)
            self.assertEqual((model_dir / "model.onnx").read_bytes(), stale_model)
            self.assertEqual(list(model_dir.glob("*.partial.*")), [])

    def test_unknown_variant_fails_with_usage_error(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            script = make_test_script(directory)
            result = run_fetch(script, directory / "models", "unknown")
            self.assertEqual(result.returncode, 2)
            self.assertIn("variant must be int8 or fp32", result.stderr)


if __name__ == "__main__":
    unittest.main()
