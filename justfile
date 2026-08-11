# QGroundControl Development Commands
# Install (requires just >=1.30 for home_directory()):
#   python tools/setup/install_python.py dev   (recommended; pulls rust-just into .venv)
#   brew install just / cargo install just / pipx install rust-just
# `apt install just` on Ubuntu ships 1.21 which is too old.

set dotenv-load
set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-Command"]

# Configuration from build-config.json
python := env_var_or_default("PYTHON", if os_family() == "windows" { "py" } else { "python3" })
qt_version := shell(python, "./tools/setup/read_config.py", "--get", "qt.version")
cmake_min_version := shell(python, "./tools/setup/read_config.py", "--get", "build.cmake_minimum_version")
gstreamer_version := shell(python, "./tools/setup/read_config.py", "--get", "gstreamer.version.default")
qt_arch := if os_family() == "windows" { "msvc2022_64" } else { "gcc_64" }
qt_dir := env_var_or_default("QT_DIR", home_directory() / "Qt" / qt_version / qt_arch)
build_type := env_var_or_default("BUILD_TYPE", "Debug")
build_dir := env_var_or_default("BUILD_DIR", "build")
qgc_executable := env_var_or_default("QGC_EXECUTABLE", build_dir / build_type / if os_family() == "windows" { "QGroundControl-console.exe" } else { "QGroundControl" })
environment_runner := python + " ./tools/setup/run_with_msvc.py --"
# Use all cores by default; override with JOBS=N.
jobs := env_var_or_default("JOBS", num_cpus())

# Default: show available commands
default:
    @just --list --unsorted

# ─────────────────────────────────────────────────────────────────────────────
# Setup
# ─────────────────────────────────────────────────────────────────────────────

# Install system dependencies (Debian/Ubuntu)
deps:
    @echo "Installing dependencies (requires sudo)..."
    {{python}} ./tools/setup/install_dependencies --platform debian

# Initialize git submodules
submodules:
    git submodule update --init --recursive

# ─────────────────────────────────────────────────────────────────────────────
# Build
# ─────────────────────────────────────────────────────────────────────────────

# Configure CMake build
configure: submodules
    {{environment_runner}} {{python}} ./tools/configure.py -B {{build_dir}} -t {{build_type}} --testing --qt-root "{{qt_dir}}"

# Build the project
build:
    {{environment_runner}} cmake --build {{build_dir}} --config {{build_type}} --parallel {{jobs}}

# Configure and build Release
release:
    {{environment_runner}} {{python}} ./tools/configure.py -B {{build_dir}} --release --qt-root "{{qt_dir}}"
    {{environment_runner}} cmake --build {{build_dir}} --config Release --parallel {{jobs}}

# Clean build directory (forwards to tools/clean.py; pass --cache, --all, --dry-run)
clean *ARGS:
    {{python}} ./tools/clean.py {{ARGS}}

# Clean, configure, and build
rebuild: clean configure build

# Full setup: deps, submodules, configure, build
setup: deps submodules configure build

# ─────────────────────────────────────────────────────────────────────────────
# Quality
# ─────────────────────────────────────────────────────────────────────────────

# Run unit tests (matches CI label filters; override with `LABELS=... EXCLUDE=... just test`)
test labels=env_var_or_default("LABELS", "Unit|Integration") exclude=env_var_or_default("EXCLUDE", "Flaky|Network"):
    {{environment_runner}} ctest --test-dir {{build_dir}} --output-on-failure -L "{{labels}}" -LE "{{exclude}}"

# Run tests matching a CTest name regex
test-one pattern:
    {{environment_runner}} ctest --test-dir {{build_dir}} --output-on-failure -R "{{pattern}}"

# Run pre-commit checks
lint:
    {{environment_runner}} {{python}} -m pre_commit run --all-files

# Check code formatting (no changes)
format:
    {{environment_runner}} {{python}} ./tools/analyze.py --tool clang-format

# Format code (apply fixes)
format-fix:
    {{environment_runner}} {{python}} ./tools/analyze.py --tool clang-format --fix

# Run static analysis
analyze:
    {{environment_runner}} {{python}} ./tools/analyze.py

# Generate coverage report
coverage:
    {{environment_runner}} {{python}} ./tools/coverage.py

# Run lint + test
check: lint test

# ─────────────────────────────────────────────────────────────────────────────
# Run & Deploy
# ─────────────────────────────────────────────────────────────────────────────

# Launch QGroundControl
run:
    {{environment_runner}} "{{qgc_executable}}"

# Build documentation
docs:
    npm run docs:build

# Build using Docker (Ubuntu)
docker:
    ./deploy/docker/run-docker.sh ubuntu

# ─────────────────────────────────────────────────────────────────────────────
# Utilities
# ─────────────────────────────────────────────────────────────────────────────

# Show build configuration
info:
    @echo "Qt version:  {{qt_version}}"
    @echo "Qt dir:      {{qt_dir}}"
    @echo "CMake min:   {{cmake_min_version}}"
    @echo "GStreamer:   {{gstreamer_version}}"
    @echo "Build type:  {{build_type}}"
    @echo "Build dir:   {{build_dir}}"
    @echo "Executable:  {{qgc_executable}}"
    @echo "Python:      {{python}}"
    @echo "Jobs:        {{jobs}}"

# Check dependency versions
check-deps:
    {{python}} ./tools/check_deps.py

# Clean build, caches, and generated files
distclean:
    {{python}} ./tools/clean.py --all
    rm -rf node_modules
