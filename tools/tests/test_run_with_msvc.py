"""Tests for tools/setup/run_with_msvc.py."""

from __future__ import annotations

import subprocess
from pathlib import Path
from unittest.mock import patch

from setup import run_with_msvc


def test_load_msvc_environment_normalizes_variable_names() -> None:
    completed = subprocess.CompletedProcess([], 0, stdout="Path=msvc-path\n", stderr="")

    with (
        patch.object(run_with_msvc.os, "environ", {"Path": "original-path"}),
        patch.object(run_with_msvc, "_find_vsdevcmd", return_value=Path("VsDevCmd.bat")),
        patch.object(run_with_msvc.subprocess, "run", return_value=completed),
    ):
        environment = run_with_msvc._load_msvc_environment()

    assert environment["PATH"] == "msvc-path"
    assert "Path" not in environment


def test_add_runtime_paths_overrides_stale_gstreamer_root(tmp_path: Path) -> None:
    qt_root = tmp_path / "qt"
    gstreamer_root = tmp_path / "gstreamer"
    (qt_root / "bin").mkdir(parents=True)
    (gstreamer_root / "bin").mkdir(parents=True)
    (gstreamer_root / "lib" / "gstreamer-1.0").mkdir(parents=True)
    environment = {
        "PATH": "original-path",
        "GSTREAMER_1_0_ROOT_MSVC_X86_64": "stale-root",
    }

    with patch.object(
        run_with_msvc.os,
        "environ",
        {"QT_DIR": str(qt_root), "GSTREAMER_ROOT": str(gstreamer_root)},
    ):
        run_with_msvc._add_runtime_paths(environment)

    assert environment["PATH"].split(run_with_msvc.os.pathsep)[:2] == [
        str(qt_root / "bin"),
        str(gstreamer_root / "bin"),
    ]
    assert environment["GSTREAMER_1_0_ROOT_MSVC_X86_64"] == str(gstreamer_root)
    assert environment["GST_PLUGIN_PATH"] == str(gstreamer_root / "lib" / "gstreamer-1.0")
