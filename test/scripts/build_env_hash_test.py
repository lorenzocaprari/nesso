import importlib.machinery
import importlib.util
import pathlib
import shutil
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def load_hash():
    path = ROOT / "scripts" / "build-env-hash"
    loader = importlib.machinery.SourceFileLoader("build_env_hash", str(path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def stage(module, arch: str, dest: pathlib.Path) -> None:
    dockerfile = module.DOCKERFILES[arch]
    text = (ROOT / dockerfile).read_text()
    rels = {dockerfile, *module.copied_sources(text)}
    for rel in rels:
        src = ROOT / rel
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        if src.is_dir():
            shutil.copytree(src, target)
        else:
            shutil.copy2(src, target)


class BuildEnvHashTest(unittest.TestCase):
    def test_hash_includes_dockerfiles_and_godog_modules(self) -> None:
        module = load_hash()
        for arch, dockerfile in module.DOCKERFILES.items():
            text = (ROOT / dockerfile).read_text()
            self.assertIn("stress-ng", text)
            self.assertIn("zstd", text)
            self.assertIn("ccache", text)
            self.assertIn("GOMODCACHE=/opt/go/pkg/mod", text)
            self.assertIn("test/functional/go.sum", text)
            conan_at = text.index("conan install .")
            for marker in ("golang-go", "stress-ng", "zstd", "GOMODCACHE=/opt/go/pkg/mod", "\n    ccache \\"):
                self.assertLess(conan_at, text.index(marker), f"{dockerfile} places {marker!r} before Conan")
            labels = {label for label, _ in module.hashed_chunks(ROOT, arch)}
            self.assertIn(f"file:{dockerfile}", labels)
            self.assertIn("file:test/functional/go.mod", labels)
            self.assertIn("file:test/functional/go.sum", labels)
            self.assertIn("spec:dependencies", labels)

    def test_go_sum_and_dockerfile_change_the_image_hash(self) -> None:
        module = load_hash()
        for arch in module.DOCKERFILES:
            with tempfile.TemporaryDirectory() as temp:
                dest = pathlib.Path(temp)
                stage(module, arch, dest)
                original = module.image_hash(dest, arch)

                go_sum = dest / "test/functional/go.sum"
                previous = go_sum.read_text()
                go_sum.write_text(previous + "module example.com/changed v0.0.1 h1:aaaa\n")
                self.assertNotEqual(module.image_hash(dest, arch), original)

                go_sum.write_text(previous)
                self.assertEqual(module.image_hash(dest, arch), original)

                dockerfile = dest / module.DOCKERFILES[arch]
                dockerfile.write_text(dockerfile.read_text() + "\n# hash probe\n")
                self.assertNotEqual(module.image_hash(dest, arch), original)


if __name__ == "__main__":
    unittest.main()
