# Custom Build Overlay

Company custom build overlay, auto-detected by CMake (`QGC_CUSTOM_DIR`).

This is currently a **neutral baseline**: `CustomPlugin` is a pass-through
`QGCCorePlugin` subclass with no overrides, so the resulting application looks
and behaves identical to stock QGroundControl. Company branding and behavior
changes land here incrementally.

Reference template with example customizations: [`custom-example/`](../custom-example/README.md).

After changing `cmake/CustomOverrides.cmake`, wipe the build directory —
overrides are written to the CMake cache with `FORCE` and survive reconfigures.

## Agricultural Spray

The Agricultural Spray complex item uses GeoFence inclusion/exclusion shapes to generate its route. Its
"Traversable non-spray areas" are separate, item-owned polygons: operators can add, edit, and delete them in the
mission editor without creating obstacles or GeoFence entries. The vehicle path remains unchanged; route segments
are split at polygon boundaries so relay 0 is turned off inside each area and restored only when the active route
segment is a spray leg.

Agricultural Spray JSON version 7 stores these polygons in `nonSprayPolygons`. Versions 1-6 remain loadable and
migrate with an empty non-spray list. Version 7 loading validates the complete polygon array before replacing the
item's current list.
