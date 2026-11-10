#!/usr/bin/env python3
"""Write the tiny tar, tgz, tar.xz, and zip seeds used by the archive fuzzer.

The bytes are produced here. No third-party archive is copied in.
"""

import io
import tarfile
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "fuzz" / "seeds" / "archive"
TEXT = b"database connection refused\n"


def tar_bytes(mode: str) -> bytes:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode=mode) as archive:
        info = tarfile.TarInfo("var/log/syslog")
        info.size = len(TEXT)
        info.mtime = 0
        info.mode = 0o644
        info.uid = 0
        info.gid = 0
        info.uname = ""
        info.gname = ""
        archive.addfile(info, io.BytesIO(TEXT))
    return buffer.getvalue()


def zip_bytes() -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        info = zipfile.ZipInfo(filename="var/log/syslog", date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        archive.writestr(info, TEXT)
    return buffer.getvalue()


def main() -> None:
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / "sample.tar").write_bytes(tar_bytes("w"))
    (DEST / "sample.tar.gz").write_bytes(tar_bytes("w:gz"))
    (DEST / "sample.tgz").write_bytes(tar_bytes("w:gz"))
    (DEST / "sample.tar.xz").write_bytes(tar_bytes("w:xz"))
    (DEST / "sample.zip").write_bytes(zip_bytes())


if __name__ == "__main__":
    main()
