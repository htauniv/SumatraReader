/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// macOS implementation of ng's local OCR (see ng/ocr/Ocr.h), backed by the
// Vision framework. The request runs at the Accurate level with simplified
// Chinese and English as recognition languages and no language correction,
// which is the best trade-off for mixed zh-Hans/en-US documents.

#include "base/Base.h"

#include "ng/ocr/Ocr.h"

// ng: objc's BOOL is bool (or signed char); ours is int. Rename theirs for
// the import. Point / Rect / Size are handled by MacTypesHide.h.
#include "base/MacTypesHide.h"
#define BOOL MacObjcBool
#import <Vision/Vision.h>
#undef BOOL
#include "base/MacTypesShow.h"

#include <dispatch/dispatch.h>

// key for dispatch_queue_set_specific; the address is the identity, never
// dereferenced. Used to detect calls that are already on the OCR queue.
static const void* kOcrQueueKey = &kOcrQueueKey;

// Recognition must not run on the main thread (an Accurate-level pass on a
// large page takes hundreds of ms), and concurrent requests would thrash the
// language models, so all work is serialized on this private queue.
static dispatch_queue_t OcrQueue() {
    static dispatch_queue_t q = nullptr;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        q = dispatch_queue_create("sumatra.ocr", DISPATCH_QUEUE_SERIAL);
        dispatch_queue_set_specific(q, (void*)kOcrQueueKey, (void*)kOcrQueueKey, nullptr);
    });
    return q;
}

// Device RGB never changes; create it once. Leaked by design, like other
// process-lifetime resources in this codebase.
static CGColorSpaceRef OcrRgbColorSpace() {
    static CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    return cs;
}

// Builds a CGImage over the caller's pixel buffer (no copy; the buffer must
// stay valid for the duration of the recognition, which the synchronous flow
// below guarantees). Returns null on failure.
static CGImageRef OcrMakeImage(const unsigned char* pix, int w, int h, int nComponents) {
    if (!pix || w <= 0 || h <= 0) {
        return nullptr;
    }
    if (nComponents != 3 && nComponents != 4) {
        return nullptr;
    }
    size_t bytesPerRow = (size_t)w * (size_t)nComponents;
    size_t nBytes = bytesPerRow * (size_t)h;
    // 4 components: BGRA in memory, Sumatra's PixmapFormat::BGRA8 layout
    // (32-bit little-endian word with alpha first). 3 components: plain RGB.
    CGBitmapInfo info;
    if (nComponents == 4) {
        // kCGBitmapByteOrder32Little is a different enum than CGImageAlphaInfo;
        // cast both to the underlying integer before OR-ing (SDK deprecates
        // bitwise ops across distinct enum types)
        info = (CGBitmapInfo)((uint32_t)kCGBitmapByteOrder32Little | (uint32_t)kCGImageAlphaPremultipliedFirst);
    } else {
        info = (CGBitmapInfo)kCGImageAlphaNone;
    }
    CGDataProviderRef prov = CGDataProviderCreateWithData(nullptr, pix, nBytes, nullptr);
    if (!prov) {
        return nullptr;
    }
    CGImageRef img = CGImageCreate((size_t)w, (size_t)h, 8, 8 * nComponents, bytesPerRow,
                                   OcrRgbColorSpace(), info, prov, nullptr, false,
                                   kCGRenderingIntentDefault);
    CFRelease(prov);
    return img;
}

// The actual recognition. Runs on OcrQueue(); outLines/outText are filled in
// reading order. Returns true if Vision completed the request.
static bool OcrRunPix(const unsigned char* pix, int w, int h, int nComponents, Vec<OcrLine>* outLines,
                      str::Builder* outText) {
    @autoreleasepool {
        CGImageRef img = OcrMakeImage(pix, w, h, nComponents);
        if (!img) {
            return false;
        }
        bool ok = false;
        VNRecognizeTextRequest* req = [[VNRecognizeTextRequest alloc] initWithCompletionHandler:nil];
        // an OOM-nil request would make the @[ ... ] literal below throw
        if (!req) {
            CGImageRelease(img);
            return false;
        }
        req.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        req.usesLanguageCorrection = NO;
        // simplified Chinese first: it is the core requirement, and Vision
        // tries the languages in order when classifying a text region
        req.recognitionLanguages = @[ @"zh-Hans", @"en-US" ];
        VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCGImage:img options:@{}];
        if (handler) {
            NSError* err = nil;
            ok = [handler performRequests:@[ req ] error:&err] != NO;
            if (!ok) {
                // FmtArg rejects raw char*, so wrap it in Str explicitly
                Str msg = err.localizedDescription ? Str(err.localizedDescription.UTF8String) : StrL("unknown error");
                logf("OcrRunPix: performRequests failed: %s\n", msg);
            }
        } else {
            logf("OcrRunPix: handler creation failed\n");
        }
        if (ok) {
            for (VNRecognizedTextObservation* obs in req.results) {
                VNRecognizedText* candidate = [obs topCandidates:1].firstObject;
                NSString* s = candidate.string;
                if (s.length == 0) {
                    continue;
                }
                const char* utf8 = s.UTF8String;
                Str text = utf8 ? str::Dup(Str(utf8)) : Str{};
                if (len(text) == 0) {
                    continue;
                }
                // Vision normalizes the bounding box to 0..1 with the origin
                // at the bottom-left; flip y so ours is top-left
                CGRect box = obs.boundingBox;
                if (outLines) {
                    OcrLine line;
                    line.text = text;
                    line.x = (float)box.origin.x;
                    line.y = 1.f - (float)box.origin.y - (float)box.size.height;
                    line.w = (float)box.size.width;
                    line.h = (float)box.size.height;
                    if (!VecAppend(*outLines, line)) {
                        str::Free(text); // the vec owns a copy only on success
                    }
                }
                if (outText) {
                    if (outText->len > 0) {
                        outText->AppendChar('\n');
                    }
                    outText->Append(text);
                }
            }
        }
        CGImageRelease(img);
        return ok;
    }
}

static bool OcrRecognizeInternal(const unsigned char* pix, int w, int h, int nComponents,
                                 Vec<OcrLine>* outLines, str::Builder* outText) {
    if (!outLines && !outText) {
        return false;
    }
    // already on the OCR queue (e.g. a test driving us directly): run
    // in-place, dispatch_sync() on the same queue would deadlock
    if (dispatch_get_specific((void*)kOcrQueueKey)) {
        return OcrRunPix(pix, w, h, nComponents, outLines, outText);
    }
    __block bool ok = false;
    dispatch_sync(OcrQueue(), ^{
        ok = OcrRunPix(pix, w, h, nComponents, outLines, outText);
    });
    return ok;
}

bool OcrRecognizeImage(const unsigned char* pix, int w, int h, int nComponents, Vec<OcrLine>& outLines) {
    return OcrRecognizeInternal(pix, w, h, nComponents, &outLines, nullptr);
}

bool OcrPageText(const unsigned char* pix, int w, int h, int nComponents, str::Builder& outText) {
    return OcrRecognizeInternal(pix, w, h, nComponents, nullptr, &outText);
}

void OcrFreeLines(Vec<OcrLine>& lines) {
    for (OcrLine& l : lines) {
        str::Free(l.text);
    }
    VecReset(lines);
}
