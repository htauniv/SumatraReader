/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// Local OCR (optical character recognition) for the ng rewrite.
//
// On macOS this is backed by Apple's Vision framework (VisionOcr_mac.mm); on
// every other platform Ocr_nonmac.cpp provides stubs that return false, so
// callers must be prepared for a failed recognition at any time.
//
// The input is a raw, top-down pixel buffer of a rendered page:
//  - nComponents 3: tightly packed RGB bytes
//  - nComponents 4: tightly packed BGRA bytes (Sumatra's PixmapFormat::BGRA8
//    layout; note a 32bpp Pixmap always has stride == width * 4, so converting
//    the page with PixmapToBgra() and passing its data works as-is)
// Tightly packed means stride == width * nComponents; this API has no stride
// parameter. A 24bpp BGR8 Pixmap can be row-padded and must not be passed
// directly.
//
// Like the other headers in this tree, this file is not include-guarded and
// assumes base/Base.h has been included first.

// one recognized line of text. (x, y, w, h) is the line's bounding box
// relative to the page the OCR ran on, normalized to 0..1 with the origin at
// the top-left corner (Vision reports it bottom-left; that flip is done
// internally so callers can think in canvas coordinates)
struct OcrLine {
    Str text; // owned heap string, free with str::Free() or OcrFreeLines()
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

// Recognize text in the pixel buffer, reporting one OcrLine per recognized
// line in reading order. Returns true if the recognition ran (i.e. the image
// could be constructed and Vision completed the request); an empty outLines
// then means the page contains no recognizable text. The text of each line is
// a str::Dup'ed copy the caller owns. Safe to call from any thread: the work
// is serialized on an internal queue. Blocking.
bool OcrRecognizeImage(const unsigned char* pix, int w, int h, int nComponents, Vec<OcrLine>& outLines);

// Same recognition as OcrRecognizeImage(), but the recognized lines are
// appended to outText joined with '\n' (no trailing newline). Returns true if
// the recognition ran; an empty outText then means no text was recognized.
// outText is appended to, not reset. Safe to call from any thread. Blocking.
bool OcrPageText(const unsigned char* pix, int w, int h, int nComponents, str::Builder& outText);

// Convenience: frees the text of every line and empties the vec.
void OcrFreeLines(Vec<OcrLine>& lines);
