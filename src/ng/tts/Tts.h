/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// ng: system text-to-speech. Speaks a document page's text through the OS
// synthesizer; the mac backend is AVSpeechSynthesizer (Tts_mac.mm), other
// platforms are no-op stubs (Tts_nonmac.cpp).
//
// All functions are safe to call from any thread.

#include "base/Base.h"

// speak <text>, picking the voice by <langCode>: "cn" -> Chinese, anything
// else -> English (en-US). Returns true if speaking was started. Starting a
// new speak stops a previous one.
bool TtsSpeak(Str text, const char* langCode);

// stop speaking immediately
void TtsStop();

// true while speech (including queued segments) is in flight
bool TtsIsSpeaking();

// playback speed in 0.5 .. 2.0, default 1.0; affects the next TtsSpeak
void TtsSetRate(float rate);
float TtsGetRate();
