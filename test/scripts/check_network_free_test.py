import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check-network-free"

CLEAN_READELF = """\
Dynamic section at offset 0x1234 contains 8 entries:
  Tag        Type                         Name/Value
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-x86-64.so.2]
"""

NM = """\
#!/bin/sh
kind=
dyn=0
posix=0
for arg in "$@"; do
  case "$arg" in
    --undefined-only) kind=undef ;;
    --defined-only) kind=defined ;;
    --format=posix) posix=1 ;;
    -D) dyn=1 ;;
  esac
done
if [ "$posix" != 1 ]; then
  echo "nm: expected --format=posix, got $*" >&2
  exit 1
fi
if [ "$dyn" = 1 ] && [ "$kind" = defined ]; then
  kind=dyn
fi
case "$kind" in
  undef)
    printf '%s' "$NM_UNDEF_STDOUT"
    [ -z "$NM_UNDEF_STDERR" ] || printf '%s\\n' "$NM_UNDEF_STDERR" >&2
    exit "${NM_UNDEF_CODE:-0}"
    ;;
  defined)
    printf '%s' "$NM_DEFINED_STDOUT"
    [ -z "$NM_DEFINED_STDERR" ] || printf '%s\\n' "$NM_DEFINED_STDERR" >&2
    exit "${NM_DEFINED_CODE:-0}"
    ;;
  dyn)
    printf '%s' "$NM_DYN_STDOUT"
    [ -z "$NM_DYN_STDERR" ] || printf '%s\\n' "$NM_DYN_STDERR" >&2
    exit "${NM_DYN_CODE:-0}"
    ;;
  *)
    echo "nm: unexpected args: $*" >&2
    exit 1
    ;;
esac
"""

READELF = """\
#!/bin/sh
found=0
for arg in "$@"; do
  if [ "$arg" = "-d" ]; then
    found=1
  fi
done
if [ "$found" != 1 ]; then
  echo "readelf: expected -d, got $*" >&2
  exit 1
fi
printf '%s' "$READELF_STDOUT"
[ -z "$READELF_STDERR" ] || printf '%s\\n' "$READELF_STDERR" >&2
exit "${READELF_CODE:-0}"
"""

STRINGS = """\
#!/bin/sh
found=0
for arg in "$@"; do
  if [ "$arg" = "-a" ]; then
    found=1
  fi
done
if [ "$found" != 1 ]; then
  echo "strings: expected -a, got $*" >&2
  exit 1
fi
printf '%s' "$STRINGS_STDOUT"
[ -z "$STRINGS_STDERR" ] || printf '%s\\n' "$STRINGS_STDERR" >&2
exit "${STRINGS_CODE:-0}"
"""


def run(
    readelf: str = CLEAN_READELF,
    readelf_code: int = 0,
    readelf_stderr: str = "",
    undef: str = "close@GLIBC_2.2.5 U\nputs@GLIBC_2.2.5 U\n",
    undef_code: int = 0,
    undef_stderr: str = "",
    defined: str = "main T 1000 20\n",
    defined_code: int = 0,
    defined_stderr: str = "",
    dyn_defined: str = "_ZNSo5flushEv T 2000 8\n",
    dyn_code: int = 0,
    dyn_stderr: str = "",
    strings_out: str = "nesso grep\n",
    strings_code: int = 0,
    strings_stderr: str = "",
    allow: str | None = None,
    args: list[str] | None = None,
) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory() as temp:
        root = pathlib.Path(temp)
        (root / "nm").write_text(NM)
        (root / "readelf").write_text(READELF)
        (root / "strings").write_text(STRINGS)
        for name in ("nm", "readelf", "strings"):
            (root / name).chmod(0o755)
        env = dict(os.environ)
        env.pop("NESSO_NETWORK_ALLOW", None)
        env["PATH"] = str(root) + os.pathsep + env.get("PATH", "")
        env["READELF_STDOUT"] = readelf
        env["READELF_STDERR"] = readelf_stderr
        env["READELF_CODE"] = str(readelf_code)
        env["NM_UNDEF_STDOUT"] = undef
        env["NM_UNDEF_STDERR"] = undef_stderr
        env["NM_UNDEF_CODE"] = str(undef_code)
        env["NM_DEFINED_STDOUT"] = defined
        env["NM_DEFINED_STDERR"] = defined_stderr
        env["NM_DEFINED_CODE"] = str(defined_code)
        env["NM_DYN_STDOUT"] = dyn_defined
        env["NM_DYN_STDERR"] = dyn_stderr
        env["NM_DYN_CODE"] = str(dyn_code)
        env["STRINGS_STDOUT"] = strings_out
        env["STRINGS_STDERR"] = strings_stderr
        env["STRINGS_CODE"] = str(strings_code)
        if allow is not None:
            allow_path = root / "allow"
            allow_path.write_text(allow)
            env["NESSO_NETWORK_ALLOW"] = str(allow_path)
        return subprocess.run(
            [sys.executable, str(SCRIPT), *(args if args is not None else ["nesso"])],
            capture_output=True,
            text=True,
            check=False,
            env=env,
        )


class CheckNetworkFreeTest(unittest.TestCase):
    def test_passes_on_a_clean_binary(self) -> None:
        result = run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_fails_on_a_denied_needed_library(self) -> None:
        readelf = CLEAN_READELF + (
            " 0x0000000000000001 (NEEDED)             Shared library: [libcurl.so.4]\n"
            " 0x0000000000000001 (NEEDED)             Shared library: [libssl.so.3]\n"
            " 0x0000000000000001 (NEEDED)             Shared library: [libcrypto.so.3]\n"
            " 0x0000000000000001 (NEEDED)             Shared library: [libresolv.so.2]\n"
        )
        result = run(readelf=readelf)
        self.assertEqual(result.returncode, 1)
        self.assertIn("NEEDED libcurl.so.4", result.stderr)
        self.assertIn("NEEDED libssl.so.3", result.stderr)
        self.assertIn("NEEDED libcrypto.so.3", result.stderr)
        self.assertIn("NEEDED libresolv.so.2", result.stderr)
        self.assertNotIn("libc.so.6", result.stderr)
        self.assertNotIn("libstdc++.so.6", result.stderr)

    def test_fails_on_a_denied_undefined_symbol(self) -> None:
        result = run(undef="connect@GLIBC_2.2.5 U\nsocket U\nclose@GLIBC_2.2.5 U\n", allow="# none\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("imports connect", result.stderr)
        self.assertIn("imports socket", result.stderr)
        self.assertNotIn("imports close", result.stderr)

    def test_passes_an_allow_listed_symbol(self) -> None:
        result = run(
            undef="connect@GLIBC_2.2.5 U\nclose@GLIBC_2.2.5 U\n",
            allow="connect # test double resolves connect locally\n",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_fails_on_a_statically_linked_curl_or_ssl_symbol(self) -> None:
        curl = run(defined="curl_easy_init T 100 10\nmain T 1000 20\n")
        self.assertEqual(curl.returncode, 1)
        self.assertIn("defines curl_easy_init", curl.stderr)
        # A strip drops the static table. The dynamic table still names SSL_*.
        ssl = run(
            defined="",
            defined_code=1,
            defined_stderr="nm: nesso: no symbols",
            dyn_defined="SSL_CTX_new T 200 20\n",
        )
        self.assertEqual(ssl.returncode, 1, ssl.stderr)
        self.assertIn("defines SSL_CTX_new", ssl.stderr)

    def test_fails_on_curl_or_openssl_version_strings(self) -> None:
        result = run(strings_out="libcurl/8.21.0\nOpenSSL 3.6.3\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("contains 'libcurl/'", result.stderr)
        self.assertIn("contains 'OpenSSL '", result.stderr)

    def test_names_every_offender(self) -> None:
        readelf = CLEAN_READELF + " 0x0000000000000001 (NEEDED)             Shared library: [libcurl.so.4]\n"
        result = run(
            readelf=readelf,
            undef="getaddrinfo@GLIBC_2.2.5 U\n",
            defined="EVP_sha256 T 300 8\n",
            strings_out="OpenSSL 3.6.3\n",
            allow="# none\n",
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("NEEDED libcurl.so.4", result.stderr)
        self.assertIn("imports getaddrinfo", result.stderr)
        self.assertIn("defines EVP_sha256", result.stderr)
        self.assertIn("contains 'OpenSSL '", result.stderr)

    def test_reports_tool_failure_and_usage(self) -> None:
        failed = run(readelf_code=1, readelf_stderr="readelf: stub error")
        self.assertEqual(failed.returncode, 2)
        self.assertIn("readelf: stub error", failed.stderr)
        missing_nm = run(undef_code=1, undef_stderr="nm: stub error")
        self.assertEqual(missing_nm.returncode, 2)
        self.assertIn("nm: stub error", missing_nm.stderr)
        missing_strings = run(strings_code=1, strings_stderr="strings: stub error")
        self.assertEqual(missing_strings.returncode, 2)
        self.assertIn("strings: stub error", missing_strings.stderr)
        usage = run(args=[])
        self.assertEqual(usage.returncode, 2)
        self.assertIn("usage:", usage.stderr)
        bare = run(undef="connect U\n", allow="connect\n")
        self.assertEqual(bare.returncode, 2)
        self.assertIn("reason", bare.stderr)


if __name__ == "__main__":
    unittest.main()
