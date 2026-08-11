#!/usr/bin/env python3

"""Run a command with the Visual Studio and QGC runtime environment."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


def _find_vsdevcmd() -> Path:
    configured_path = os.environ.get("VSDEVCMD")
    if configured_path:
        path = Path(configured_path)
        if not path.is_file():
            raise FileNotFoundError(f"VSDEVCMD does not exist: {path}")
        return path

    program_files_x86 = Path(os.environ.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)"))
    vswhere = program_files_x86 / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if vswhere.is_file():
        result = subprocess.run(
            [
                str(vswhere),
                "-latest",
                "-products",
                "*",
                "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property",
                "installationPath",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        installation_path = result.stdout.strip()
        if installation_path:
            path = Path(installation_path) / "Common7" / "Tools" / "VsDevCmd.bat"
            if path.is_file():
                return path

    raise FileNotFoundError("Visual Studio C++ tools were not found. Set VSDEVCMD to VsDevCmd.bat.")


def _load_msvc_environment() -> dict[str, str]:
    vsdevcmd = _find_vsdevcmd()
    command = f'call "{vsdevcmd}" -arch=x64 -host_arch=x64 >nul && set'
    result = subprocess.run(
        command,
        check=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        executable=os.environ.get("COMSPEC", "cmd.exe"),
        shell=True,
        text=True,
    )

    environment = os.environ.copy()
    for line in result.stdout.splitlines():
        name, separator, value = line.partition("=")
        if separator and name:
            environment[name] = value
    return environment


def _add_runtime_paths(environment: dict[str, str]) -> None:
    runtime_paths: list[str] = []

    qt_dir = os.environ.get("QT_DIR")
    if qt_dir:
        qt_bin = Path(qt_dir) / "bin"
        if not qt_bin.is_dir():
            raise FileNotFoundError(f"Qt bin directory does not exist: {qt_bin}")
        runtime_paths.append(str(qt_bin))

    gstreamer_root = os.environ.get("GSTREAMER_ROOT")
    if gstreamer_root:
        gstreamer_bin = Path(gstreamer_root) / "bin"
        gstreamer_plugins = Path(gstreamer_root) / "lib" / "gstreamer-1.0"
        if not gstreamer_bin.is_dir():
            raise FileNotFoundError(f"GStreamer bin directory does not exist: {gstreamer_bin}")
        if not gstreamer_plugins.is_dir():
            raise FileNotFoundError(
                f"GStreamer plugin directory does not exist: {gstreamer_plugins}"
            )
        runtime_paths.append(str(gstreamer_bin))
        environment["GST_PLUGIN_PATH"] = str(gstreamer_plugins)

    if runtime_paths:
        runtime_paths.append(environment.get("PATH", ""))
        environment["PATH"] = os.pathsep.join(runtime_paths)


def main() -> int:
    command = sys.argv[1:]
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        print("Usage: run_with_msvc.py -- <command> [arguments...]", file=sys.stderr)
        return 2

    try:
        environment = _load_msvc_environment() if sys.platform == "win32" else os.environ.copy()
        _add_runtime_paths(environment)
        executable = shutil.which(command[0], path=environment.get("PATH"))
        if executable:
            command[0] = executable
        elif not Path(command[0]).is_file():
            raise FileNotFoundError(f"Command was not found: {command[0]}")
        return subprocess.run(command, env=environment, check=False).returncode
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        print(f"Environment setup failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
