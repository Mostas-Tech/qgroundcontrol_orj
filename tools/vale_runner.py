"""Bootstrap and run the pinned official Vale release."""

from __future__ import annotations

import hashlib
import os
import platform
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

VERSION = "3.15.1"
ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".cache" / "vale" / VERSION
ASSETS = {
    ("Linux", "x86_64"): (
        "vale_3.15.1_Linux_64-bit.tar.gz",
        "c024d9c157874fb043d4f24a055d60050d1bb18755251f590593eed5bace1857",
        "77a1d5e0f9833c987c7b96da9f3bd6c86fcfe8b5f5ff656be72f7462c4c45b02",
    ),
    ("Linux", "aarch64"): (
        "vale_3.15.1_Linux_arm64.tar.gz",
        "281a419e140da11a408935356bab7a4ef770fed047a9d7bd1765c76acd647d01",
        "23e8363babdc315349c40c085b2ba0e884321ec169dbf5959cfaa0136b3c3c5f",
    ),
    ("Darwin", "x86_64"): (
        "vale_3.15.1_macOS_64-bit.tar.gz",
        "9268383c9e244332c4483cb359e52bd4cb030542873e2cafc48e3bbfff6a989a",
        "392a29196dec9f82c1c7c4d7a9057b525bd2daff8adfa959056d80e07e02a810",
    ),
    ("Darwin", "arm64"): (
        "vale_3.15.1_macOS_arm64.tar.gz",
        "968c6d8bf2052bc97aa24274234cc466dbcc249b55ace33dd382c2cdfa93b08c",
        "d5736e0b1ef3b114a5058f2626d14d9085fe8441ababbd3f0038c14666d1586a",
    ),
    ("Windows", "AMD64"): (
        "vale_3.15.1_Windows_64-bit.zip",
        "3395fca0ddfb10a9b6caa28e091d5df709b1d6b6579afb7dece852cad89b94f3",
        "103e18687f59f860c6fdbf9ef7edfaa116092542db968039bab6105fb8906aae",
    ),
}


def _asset() -> tuple[str, str, str]:
    system = platform.system()
    machine = platform.machine()
    if system == "Windows":
        system = "Windows"
    if machine.lower() in {"amd64", "x86_64"}:
        machine = "AMD64" if system == "Windows" else "x86_64"
    elif machine.lower() in {"aarch64", "arm64"}:
        machine = (
            "ARM64"
            if system == "Windows"
            else ("arm64" if system == "Darwin" else "aarch64")
        )
    try:
        return ASSETS[(system, machine)]
    except KeyError as exc:
        raise RuntimeError(f"Unsupported Vale platform: {system}/{machine}") from exc


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _binary() -> Path:
    asset, archive_digest, executable_digest = _asset()
    target = CACHE / ("vale.exe" if platform.system() == "Windows" else "vale")
    if target.exists() and _sha256(target) == executable_digest:
        return target
    CACHE.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=CACHE) as temp:
        archive = Path(temp) / asset
        urllib.request.urlretrieve(
            f"https://github.com/vale-cli/vale/releases/download/v{VERSION}/{asset}",
            archive,
        )
        if _sha256(archive) != archive_digest:
            raise RuntimeError(f"Checksum mismatch for {asset}")
        extract = Path(temp) / "extract"
        extract.mkdir()
        if asset.endswith(".zip"):
            with zipfile.ZipFile(archive) as zf:
                members = [
                    item
                    for item in zf.infolist()
                    if not item.is_dir() and Path(item.filename).name == target.name
                ]
                if len(members) != 1:
                    raise RuntimeError("Archive must contain exactly one Vale executable")
                item = members[0]
                out = (extract / item.filename).resolve()
                if not str(out).startswith(str(extract.resolve()) + os.sep):
                    raise RuntimeError("Unsafe archive path")
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(zf.read(item))
        else:
            with tarfile.open(archive, "r:gz") as tf:
                members = [
                    item
                    for item in tf.getmembers()
                    if item.isfile() and Path(item.name).name == target.name
                ]
                if len(members) != 1:
                    raise RuntimeError("Archive must contain exactly one Vale executable")
                item = members[0]
                out = (extract / item.name).resolve()
                if not str(out).startswith(str(extract.resolve()) + os.sep):
                    raise RuntimeError("Unsafe archive path")
                source = tf.extractfile(item)
                if source is None:
                    raise RuntimeError("Vale executable cannot be read from archive")
                out.parent.mkdir(parents=True, exist_ok=True)
                with source, out.open("wb") as destination:
                    shutil.copyfileobj(source, destination)
        candidate = next(extract.rglob(target.name), None)
        if candidate is None:
            raise RuntimeError("Vale executable missing from archive")
        staged = Path(temp) / target.name
        shutil.copyfile(candidate, staged)
        staged.chmod(staged.stat().st_mode | stat.S_IXUSR)
        if _sha256(staged) != executable_digest:
            raise RuntimeError("Extracted Vale executable checksum mismatch")
        try:
            os.replace(staged, target)
        except OSError:
            if not target.exists() or _sha256(target) != executable_digest:
                raise
    return target


if __name__ == "__main__":
    raise SystemExit(subprocess.call([str(_binary()), *sys.argv[1:]], cwd=ROOT))
