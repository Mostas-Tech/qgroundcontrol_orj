# Custom Build Overlay

Company custom build overlay, auto-detected by CMake (`QGC_CUSTOM_DIR`).

This is currently a **neutral baseline**: `CustomPlugin` is a pass-through
`QGCCorePlugin` subclass with no overrides, so the resulting application looks
and behaves identical to stock QGroundControl. Company branding and behavior
changes land here incrementally.

Reference template with example customizations: [`custom-example/`](../custom-example/README.md).

After changing `cmake/CustomOverrides.cmake`, wipe the build directory —
overrides are written to the CMake cache with `FORCE` and survive reconfigures.
