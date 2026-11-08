#!/bin/bash
# End-to-end OCR regression test: renders page 1 of a real PDF with
# CoreGraphics (BGRA8, the same pixel format the app uses) and runs
# src/ng/ocr/VisionOcr_mac.mm on the pixels via Apple Vision.
#   usage: bash tools/ocr_endtoend_test.sh <some.pdf> [more.pdf ...]
# exit 0 if every pdf yielded at least one recognized line.
set -e
# this copy lives inside the repo: sumatrapdf/tools/ -> repo root
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap "rm -rf $TMP" EXIT
FLAGS="-std=c++20 -DSUMATRA_NG -DIS_TRACY=0 -mmacosx-version-min=13.0 -I $ROOT/src -I $ROOT/src/ng -I $ROOT/out/mac/rel/generated/shared/src -I $ROOT/ext/gpui -I $ROOT/ext/mupdf/include"
# Compile the harness that sits next to this script, not a stale copy outside
# the repository (the old ../tools path silently built an outdated version).
xcrun clang++ $FLAGS -x objective-c++ -fobjc-arc -c "$ROOT/tools/ocr_endtoend_test.mm" -o $TMP/test.o
xcrun clang++ $FLAGS -x objective-c++ -c "$ROOT/src/ng/ocr/VisionOcr_mac.mm" -o $TMP/VisionOcr.o
xcrun clang++ -mmacosx-version-min=13.0 -o $TMP/ocr_test $TMP/test.o $TMP/VisionOcr.o \
  "$ROOT/out/mac/rel/lib/libbase.a" \
  -framework Vision -framework CoreGraphics -framework Foundation -liconv -lz
rc=0
for pdf in "$@"; do
  echo "== $pdf"
  $TMP/ocr_test "$pdf" | head -8 || rc=1
done
exit $rc
