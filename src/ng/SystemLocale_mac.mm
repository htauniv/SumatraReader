/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"

// ng: objc's BOOL is signed char; ours is int, and MacTypes.h (pulled in by
// CoreFoundation) defines Point / Rect / Size. Same dance as VisionOcr_mac.mm.
#include "base/MacTypesHide.h"
#define BOOL MacObjcBool
#import <CoreFoundation/CoreFoundation.h>
#undef BOOL
#include "base/MacTypesShow.h"

#include "Translations.h"

namespace trans {

// System Settings > Language & Region, as BCP 47 tags ("zh-Hans-CN"),
// most preferred first.
//
// DetectUserLang() needs this and cannot rely on the environment alone: an app
// started from Finder inherits no LC_ALL / LANG, so $LANG is empty on a Chinese
// Mac and the UI would come up in English.
void GetSystemPreferredLocales(StrVec& out) {
    CFArrayRef langs = CFLocaleCopyPreferredLanguages();
    if (!langs) {
        return;
    }
    CFIndex n = CFArrayGetCount(langs);
    for (CFIndex i = 0; i < n; i++) {
        auto s = (CFStringRef)CFArrayGetValueAtIndex(langs, i);
        // a tag is short ("zh-Hant-TW"); 64 was too tight for the odd long one
        char buf[128];
        if (s && CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) {
            out.Append(Str(buf));
        }
    }
    CFRelease(langs);
}

} // namespace trans
