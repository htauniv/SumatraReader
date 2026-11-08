// Single source of truth for this fork's product identity on the build side.
// cmd/helper/ng-targets.ts, ng-mac-bundle.ts, ng-dbg.ts and tools/verify_build.py
// all read these values. The C++ side has the matching kAppName in
// src/ng/Version.h; verify_build.py asserts the two stay in sync.

/** User-visible product name, also the executable / .app / target name. */
export const APP_NAME = "SumatraReader";

/** Reverse-DNS bundle identifier for the macOS app. */
export const BUNDLE_ID = "org.sumatrareader.SumatraReader";
