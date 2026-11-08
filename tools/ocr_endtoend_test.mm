// Standalone end-to-end test of src/ng/ocr: render a real PDF page with
// CoreGraphics and run the Vision OCR on the pixels.
#include "base/Base.h"
#include "ng/ocr/Ocr.h"

// hide the mac types Base.h redefines (Size, Point, ...), import the
// frameworks, restore - the same pattern VisionOcr_mac.mm uses
#include "base/MacTypesHide.h"
// ng: objc's BOOL is bool (or signed char); ours is int. Rename theirs for
// the import; objc.h's `typedef signed char BOOL` then defines MacObjcBool.
#define BOOL MacObjcBool
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#undef BOOL
#include "base/MacTypesShow.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: %s <pdf>\n", argv[0]); return 2; }
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]];
        CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL((__bridge CFURLRef)url);
        if (!doc) { printf("FAIL: cannot open pdf\n"); return 1; }
        size_t nPages = CGPDFDocumentGetNumberOfPages(doc);
        printf("pages: %zu\n", nPages);
        CGPDFPageRef page = CGPDFDocumentGetPage(doc, 1);
        CGRect box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
        int targetW = 1200;
        int w = targetW, h = (int)(box.size.height / box.size.width * targetW);
        unsigned char* pix = (unsigned char*)calloc((size_t)w * h, 4);
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        // BGRA8 like Sumatra's PixmapFormat::BGRA8 (little-endian premultiplied first)
        CGContextRef ctx = CGBitmapContextCreate(pix, w, h, 8, w * 4, cs,
            (CGBitmapInfo)kCGImageAlphaPremultipliedFirst | (CGBitmapInfo)kCGBitmapByteOrder32Little);
        CGColorSpaceRelease(cs);
        CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
        CGContextFillRect(ctx, CGRectMake(0, 0, w, h));
        CGContextScaleCTM(ctx, (CGFloat)w / box.size.width, (CGFloat)h / box.size.height);
        CGContextTranslateCTM(ctx, -box.origin.x, -box.origin.y);
        CGContextDrawPDFPage(ctx, page);
        CGContextRelease(ctx);

        // pixel stats: is the render actually non-blank?
        long dark = 0; long total = (long)w * h;
        for (long i = 0; i < total; i++) {
            if (pix[i*4] < 128 && pix[i*4+1] < 128 && pix[i*4+2] < 128) dark++;
        }
        printf("pixels: %dx%d, dark: %.1f%%\n", w, h, 100.0 * dark / total);

        Vec<OcrLine> lines;
        bool ok = OcrRecognizeImage(pix, w, h, 4, lines);
        printf("ocr ran: %s, lines: %d\n", ok ? "YES" : "NO", len(lines));
        int shown = 0;
        for (OcrLine& l : lines) {
            if (shown++ < 8) printf("  %.*s\n", l.text.len, l.text.s ? l.text.s : "");
        }
        OcrFreeLines(lines);
        free(pix);
        CGPDFDocumentRelease(doc);
        return ok && len(lines) > 0 ? 0 : 1;
    }
}

// stubs satisfied by the app, not libbase
void _uploadDebugReport(Str, Str, bool) {}
void log(Str) {}
