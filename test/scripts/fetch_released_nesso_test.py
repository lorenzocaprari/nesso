import hashlib
import os
import pathlib
import subprocess
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "fetch-released-nesso"

FAKE_CURL = textwrap.dedent(
    """\
    #!/usr/bin/env python3
    import os
    import pathlib
    import sys

    args = sys.argv[1:]
    record = pathlib.Path(os.environ["CURL_RECORD"])
    previous = record.read_text(encoding="utf-8") if record.is_file() else ""
    record.write_text(previous + "\\0".join(args) + "\\n", encoding="utf-8")
    if "--proto" not in args or "--proto-redir" not in args or "--tlsv1.2" not in args:
        print("missing curl security flags", file=sys.stderr)
        sys.exit(3)
    if args[args.index("--proto") + 1] != "=https" or args[args.index("--proto-redir") + 1] != "=https":
        print("curl protocol is not https-only", file=sys.stderr)
        sys.exit(3)
    try:
        out = args[args.index("-o") + 1]
    except ValueError:
        sys.exit(2)
    name = args[-1].rsplit("/", 1)[-1].replace("%2B", "+")
    src = pathlib.Path(os.environ["CURL_FIXTURE"]) / name
    if not src.is_file():
        print(f"missing fixture {name}", file=sys.stderr)
        sys.exit(22)
    pathlib.Path(out).write_bytes(src.read_bytes())
    """
)


def run_script(args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(SCRIPT), *args],
        capture_output=True,
        text=True,
        check=False,
        env=env,
    )


def build_deb(directory: pathlib.Path, version: str, *, report: str | None = None, with_model: bool = True) -> None:
    report = version if report is None else report
    root = directory / f"pkg-{version}"
    binary = root / "usr" / "bin" / "nesso"
    binary.parent.mkdir(parents=True)
    binary.write_text(f"#!/bin/sh\nprintf '%s\\n' '{report}'\n", encoding="utf-8")
    binary.chmod(0o755)
    control = root / "DEBIAN" / "control"
    control.parent.mkdir(parents=True)
    control.write_text(
        "Package: nesso\nVersion: 0.3.1\nArchitecture: amd64\n"
        "Maintainer: test <test@example.com>\nDescription: test\n",
        encoding="utf-8",
    )
    model = root / "usr" / "share" / "nesso"
    model.mkdir(parents=True)
    if with_model:
        (model / "model.onnx").write_bytes(b"model")
        (model / "vocab.txt").write_bytes(b"vocab")
    deb = directory / f"nesso_{version}_amd64.deb"
    subprocess.run(
        ["dpkg-deb", "--root-owner-group", "--build", str(root), str(deb)],
        check=True,
        capture_output=True,
        text=True,
    )
    digest = hashlib.sha256(deb.read_bytes()).hexdigest()
    sums = directory / "SHA256SUMS"
    line = f"{digest}  nesso_{version}_amd64.deb\n"
    existing = sums.read_text(encoding="utf-8") if sums.is_file() else ""
    sums.write_text(existing + line, encoding="utf-8")


class FetchReleasedNessoTest(unittest.TestCase):
    def test_rejects_a_version_before_any_download(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            bindir = root / "bin"
            bindir.mkdir()
            curl = bindir / "curl"
            curl.write_text("#!/bin/sh\necho curl-called >&2\nexit 99\n", encoding="utf-8")
            curl.chmod(0o755)
            env = os.environ.copy()
            env["PATH"] = f"{bindir}{os.pathsep}{env['PATH']}"
            for version in ("", "0.3", "v0.3.1", "0.3.1;id", "../0.3.1", "0.3.1/amd64", "1.2.3 "):
                result = run_script([version] if version else [], env)
                self.assertNotEqual(result.returncode, 0, version)
                self.assertNotIn("curl-called", result.stderr)

    def test_unpacks_a_verified_deb_and_records_the_model_dir(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            build_deb(root, "0.3.1")
            env_file = root / "github.env"
            dest = root / "out"
            result = self._fetch(root, ["--dest", str(dest), "--github-env", str(env_file), "--", "0.3.1"])
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((dest / "usr" / "bin" / "nesso").is_file())
            self.assertTrue((dest / "usr" / "share" / "nesso" / "model.onnx").is_file())
            self.assertTrue((dest / "usr" / "share" / "nesso" / "vocab.txt").is_file())
            recorded = env_file.read_text(encoding="utf-8")
            self.assertIn(f"NESSO_BINARY={dest / 'usr' / 'bin' / 'nesso'}\n", recorded)
            self.assertIn(f"NESSO_MODEL_DIR={dest / 'usr' / 'share' / 'nesso'}\n", recorded)
            self.assertIn("--tlsv1.2", (root / "curl-record").read_text(encoding="utf-8"))

    def test_encodes_build_metadata_in_the_url(self) -> None:
        version = "1.2.3+build.1"
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            build_deb(root, version)
            result = self._fetch(root, ["--dest", str(root / "out"), "--", version])
            self.assertEqual(result.returncode, 0, result.stderr)
            record = (root / "curl-record").read_text(encoding="utf-8")
            self.assertIn("v1.2.3%2Bbuild.1/nesso_1.2.3%2Bbuild.1_amd64.deb", record)
            self.assertNotIn("v1.2.3+build.1/", record)

    def test_sha256_mismatch_does_not_unpack(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            build_deb(root, "0.3.1")
            (root / "SHA256SUMS").write_text(f"{'0' * 64}  nesso_0.3.1_amd64.deb\n", encoding="utf-8")
            dest = root / "out"
            result = self._fetch(root, ["--dest", str(dest), "--", "0.3.1"])
            self.assertEqual(result.returncode, 1)
            self.assertIn("sha256 mismatch", result.stderr)
            self.assertFalse(dest.exists())

    def test_version_reported_by_the_binary_must_match(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            build_deb(root, "0.3.1", report="0.3.0")
            result = self._fetch(root, ["--dest", str(root / "out"), "--", "0.3.1"])
            self.assertEqual(result.returncode, 1)
            self.assertIn("expected 0.3.1", result.stderr)

    def test_missing_model_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            build_deb(root, "0.3.1", with_model=False)
            result = self._fetch(root, ["--dest", str(root / "out"), "--", "0.3.1"])
            self.assertEqual(result.returncode, 1)
            self.assertIn("model.onnx", result.stderr)

    def _fetch(self, root: pathlib.Path, args: list[str]) -> subprocess.CompletedProcess[str]:
        bindir = root / "bin"
        bindir.mkdir(exist_ok=True)
        curl = bindir / "curl"
        curl.write_text(FAKE_CURL, encoding="utf-8")
        curl.chmod(0o755)
        env = os.environ.copy()
        env["PATH"] = f"{bindir}{os.pathsep}{env['PATH']}"
        env["CURL_FIXTURE"] = str(root)
        env["CURL_RECORD"] = str(root / "curl-record")
        return run_script(args, env)


if __name__ == "__main__":
    unittest.main()
