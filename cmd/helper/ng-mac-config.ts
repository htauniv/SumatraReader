// Shared macOS deployment target for this local Intel build.
//
// Kept in its own module so both the compile/link flags (ng-compile.ts), the
// embedded-resource object (embedded.ts) and the .app Info.plist
// (ng-mac-bundle.ts) use exactly the same value without an import cycle.

/** Lowest macOS version this binary supports (Ventura). */
export const MAC_MIN_VERSION = "13.0";
