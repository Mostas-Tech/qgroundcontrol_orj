---
name: windows-dev-env
description: Use the verified paths, build tree, and commands for QGroundControl development on this Windows machine.
---

# Windows Development Environment

Run commands from `E:\Github\qgroundcontrol_orj`.

## Verified paths

- Qt: `E:\Qt\6.11.1\msvc2022_64`
- Visual Studio environment:
  `C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat`
- Configured tree: `build-spray-msvc`
- GStreamer SDK:
  `.cache\CPM\gstreamer-win-x86_64-1.28.4\sdk`
- Python/dev tools: `.venv\Scripts`
- Local machine settings: `.env` (git-ignored)
- Compilation database: `build-spray-msvc\compile_commands.json`

Reuse `build-spray-msvc`. Before any configure, inspect `build-spray-msvc\CMakeCache.txt` and keep
the existing generator, Qt path, and options. Do not create a second tree or reconfigure merely to
probe the environment.

## Build and test

Open a new PowerShell after installing tools so the WinGet `just` alias is visible. If an older
terminal has not refreshed `PATH`, use `.venv\Scripts\just.exe` in place of `just`.

```powershell
just info
just build
just test-one AgriculturalSprayPlannerTest
just test Custom
```

`just build`, `just test`, and `just run` call `tools\setup\run_with_msvc.py`, which loads the
Visual Studio environment and the Qt/GStreamer runtime paths. Do not activate `VsDevCmd.bat`
manually or create another build tree.

The repaired tree was verified with consecutive no-op builds (`ninja: no work to do`, approximately
2.5 seconds). If the dependency database is interrupted again, compact it once with
`ninja -C build-spray-msvc -t recompact`; do not delete the tree.

## Debug console launch

Launch the Debug console build with:

```powershell
just run
```

This launch path was verified to keep `QGroundControl-console.exe` running. If launch fails, report
the environment error once; do not rebuild or reconfigure to address a missing runtime DLL.
