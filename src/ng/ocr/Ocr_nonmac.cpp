/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// Non-mac stubs for ng's local OCR (see ng/ocr/Ocr.h). OCR is only
// implemented on macOS via the Vision framework (VisionOcr_mac.mm); elsewhere
// every request fails, so callers must handle a false return gracefully.

#include "base/Base.h"

#include "ng/ocr/Ocr.h"

bool OcrRecognizeImage(const unsigned char*, int, int, int, Vec<OcrLine>&) {
    return false;
}

bool OcrPageText(const unsigned char*, int, int, int, str::Builder&) {
    return false;
}

void OcrFreeLines(Vec<OcrLine>& lines) {
    VecReset(lines);
}
