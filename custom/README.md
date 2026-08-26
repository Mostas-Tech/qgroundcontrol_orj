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

Agricultural Spray JSON version 8 stores ordered item-owned fields in `fields`; each field contains its id, name,
polygon, settings, and `nonSprayPolygons`. Versions 1-7 remain loadable: the legacy GeoFence source polygon and
settings are migrated into one field. Each field is planned independently and aggregate mission expansion inserts
direct transit waypoints between consecutive field routes; an invalid field prevents save or mission expansion.
