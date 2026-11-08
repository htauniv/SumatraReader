/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"

#include "Tts.h"

// No-op TTS backend for platforms without a mac implementation. Guarded so
// this file can be listed in every platform's sources: on darwin the real
// backend is Tts_mac.mm.

#if !OS_DARWIN

bool TtsSpeak(Str text, const char* langCode) {
    (void)text;
    (void)langCode;
    return false;
}

void TtsStop() {}

bool TtsIsSpeaking() {
    return false;
}

void TtsSetRate(float rate) {
    (void)rate;
}

float TtsGetRate() {
    return 1.0f;
}

#endif // !OS_DARWIN
