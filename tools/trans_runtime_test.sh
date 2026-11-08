#!/bin/bash
# Build and run the translation subsystem runtime test against the current
# out/mac/rel objects. Re-run after any change to translations.txt, the
# language table, Translations.cpp or the embedded resource packing.
set -euo pipefail

# this copy lives inside the repo: sumatrapdf/tools/ -> repo root
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT"
OUT="$SRC/out/mac/rel"
OBJ="$OUT/obj/app/src/shared"
TEST_BIN="${TMPDIR:-/tmp}/sumatra_trans_runtime_test"

MIN_VER="$(sed -n 's/.*MAC_MIN_VERSION = "\([^"]*\)".*/\1/p' "$SRC/cmd/helper/ng-mac-config.ts")"
if [ -z "$MIN_VER" ]; then MIN_VER=13.0; fi

FLAGS=(-std=gnu++17 -O1 -g "-mmacosx-version-min=$MIN_VER")
INCLUDES=(-I "$OUT/generated/shared/src")

clang++ "${FLAGS[@]}" "${INCLUDES[@]}" \
    -c "$ROOT/tools/trans_runtime_test.cpp" -o "${TEST_BIN}.o"

clang++ "-mmacosx-version-min=$MIN_VER" -o "$TEST_BIN" "${TEST_BIN}.o" \
    "$OBJ/Translations.cpp.o" \
    "$OBJ/TranslationLangs.cpp.o" \
    "$OBJ/EmbeddedResources.cpp.o" \
    "$OUT/embedded.o" \
    "$OUT/lib/libbase.a" \
    "$OUT/lib/liblzma.a" \
    -framework Cocoa -framework Foundation -framework Security \
    -lcurl -liconv

exec "$TEST_BIN" "$@"
