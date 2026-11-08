/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"

#include <atomic>
#include <dispatch/dispatch.h>
#include <mutex>
#include <objc/runtime.h>

// ng: objc's BOOL is bool (or signed char); ours is int. Rename theirs for
// the import. Point / Rect / Size are handled by MacTypesHide.h.
#include "base/MacTypesHide.h"
#define BOOL MacObjcBool
#import <AVFoundation/AVFoundation.h>
#undef BOOL
#include "base/MacTypesShow.h"

#include "Tts.h"

// user-facing rate is 0.5 .. 2.0 with 1.0 as the default
constexpr float kTtsUserRateMin = 0.5f;
constexpr float kTtsUserRateMax = 2.0f;
constexpr float kTtsUserRateDefault = 1.0f;

// a single AVSpeechUtterance that is too long can be cut off by the system,
// so text is split into segments of at most this many characters
constexpr NSInteger kTtsMaxSegmentChars = 4000;
// pause between segments, in seconds, so paragraph boundaries stay audible
constexpr NSTimeInterval kTtsSegmentDelay = 0.05;

// All synthesizer state lives on this serial queue: gTtsSynth, gTtsDelegate,
// gTtsGen and gTtsSegmentsLeft are only touched inside its blocks. Callers
// can be on any thread; the delegate callbacks hop back onto the queue.
// gTtsRate / gTtsSpeaking are atomics so TtsSetRate / TtsGetRate /
// TtsIsSpeaking never block.
static dispatch_queue_t gTtsQueue;
static AVSpeechSynthesizer* gTtsSynth = nil;
static NSObject<AVSpeechSynthesizerDelegate>* gTtsDelegate = nil;
// bumped for every TtsSpeak; utterances carry it so callbacks from a
// canceled older session are ignored
static int gTtsGen = 0;
static int gTtsSegmentsLeft = 0;
static std::atomic<float> gTtsRate{kTtsUserRateDefault};
static std::atomic<bool> gTtsSpeaking{false};
static std::once_flag gTtsOnce;

static void TtsInitQueue() {
    gTtsQueue = dispatch_queue_create("sumatrapdf.tts", DISPATCH_QUEUE_SERIAL);
}

// key for the generation tag objc_setAssociatedObject puts on utterances
static char kTtsGenKey;

static NSString* TtsNsStr(Str text) {
    if (len(text) == 0) {
        return nil;
    }
    return [[NSString alloc] initWithBytes:text.s length:(NSUInteger)text.len encoding:NSUTF8StringEncoding];
}

// user rate -> AVSpeechUtterance rate, piecewise-linear so the default lands
// exactly on AVSpeechUtteranceDefaultSpeechRate: 0.5 -> min, 1.0 -> default,
// 2.0 -> max
static float TtsMapRate(float userRate) {
    if (userRate <= kTtsUserRateDefault) {
        float t = (userRate - kTtsUserRateMin) / (kTtsUserRateDefault - kTtsUserRateMin);
        return (float)AVSpeechUtteranceMinimumSpeechRate +
               t * (float)(AVSpeechUtteranceDefaultSpeechRate - AVSpeechUtteranceMinimumSpeechRate);
    }
    float t = (userRate - kTtsUserRateDefault) / (kTtsUserRateMax - kTtsUserRateDefault);
    return (float)AVSpeechUtteranceDefaultSpeechRate +
           t * (float)(AVSpeechUtteranceMaximumSpeechRate - AVSpeechUtteranceDefaultSpeechRate);
}

// voice for a BCP-47 language ("zh-CN" / "en-US"): the compact system voice
// of the language first, then any voice the language asks for, then any
// voice in the language family, then nil (the synthesizer's default voice)
static AVSpeechSynthesisVoice* TtsPickVoice(NSString* lang) {
    NSString* compactPrefix = [@"com.apple.voice.compact." stringByAppendingString:lang];
    AVSpeechSynthesisVoice* compact = nil;
    AVSpeechSynthesisVoice* sameLang = nil;
    for (AVSpeechSynthesisVoice* voice in [AVSpeechSynthesisVoice speechVoices]) {
        if (!compact && [voice.identifier hasPrefix:compactPrefix]) {
            compact = voice;
        }
        if (!sameLang && [voice.language isEqualToString:lang]) {
            sameLang = voice;
        }
    }
    if (compact) {
        return compact;
    }
    if (sameLang) {
        return sameLang;
    }
    AVSpeechSynthesisVoice* byLang = [AVSpeechSynthesisVoice voiceWithLanguage:lang];
    if (byLang) {
        return byLang;
    }
    NSString* family = [lang substringToIndex:2];
    for (AVSpeechSynthesisVoice* voice in [AVSpeechSynthesisVoice speechVoices]) {
        if ([voice.language hasPrefix:family]) {
            return voice;
        }
    }
    return nil;
}

// don't cut a surrogate pair in half when force-splitting
static NSInteger TtsAdjustCut(NSString* text, NSInteger cut) {
    if (cut >= text.length) {
        return text.length;
    }
    if (CFStringIsSurrogateHighCharacter([text characterAtIndex:(NSUInteger)(cut - 1)])) {
        return cut - 1;
    }
    return cut;
}

// long text -> segments of at most kTtsMaxSegmentChars, split at paragraph
// boundaries first and at the 4000-character cap within a paragraph
static NSArray<NSString*>* TtsSegmentText(NSString* text) {
    NSMutableArray<NSString*>* segments = [NSMutableArray array];
    NSString* normalized = [text stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
    NSArray<NSString*>* paras = [normalized componentsSeparatedByString:@"\n"];
    NSMutableString* chunk = [NSMutableString string];
    for (NSString* para in paras) {
        if ((NSInteger)para.length > kTtsMaxSegmentChars) {
            if (chunk.length > 0) {
                [segments addObject:chunk];
                [chunk setString:@""];
            }
            NSInteger start = 0;
            while (start < (NSInteger)para.length) {
                NSInteger cut = TtsAdjustCut(para, start + kTtsMaxSegmentChars);
                [segments addObject:[para substringWithRange:NSMakeRange(start, cut - start)]];
                start = cut;
            }
            continue;
        }
        if (chunk.length > 0 && (NSInteger)(chunk.length + para.length + 1) > kTtsMaxSegmentChars) {
            [segments addObject:chunk];
            [chunk setString:@""];
        }
        if (chunk.length > 0) {
            [chunk appendString:@"\n"];
        }
        [chunk appendString:para];
    }
    if (chunk.length > 0) {
        [segments addObject:chunk];
    }
    return segments;
}

@interface SumatraTtsDelegate : NSObject <AVSpeechSynthesizerDelegate>
@end

// a segment finished (spoken or canceled): drop it from the count and clear
// the speaking flag when the last one is done
static void TtsOnSegmentDone(AVSpeechUtterance* utterance) {
    NSNumber* genNum = objc_getAssociatedObject(utterance, kTtsGenKey);
    int gen = genNum ? [genNum intValue] : 0;
    dispatch_async(gTtsQueue, ^{
        @autoreleasepool {
            if (gen != gTtsGen) {
                return; // utterance of a session that has been replaced
            }
            if (gTtsSegmentsLeft > 0) {
                gTtsSegmentsLeft--;
            }
            if (gTtsSegmentsLeft == 0) {
                gTtsSpeaking = false;
            }
        }
    });
}

@implementation SumatraTtsDelegate

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer
    didFinishSpeechUtterance:(AVSpeechUtterance*)utterance {
    TtsOnSegmentDone(utterance);
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer
    didCancelSpeechUtterance:(AVSpeechUtterance*)utterance {
    TtsOnSegmentDone(utterance);
}

@end

// on gTtsQueue: create the synthesizer on first use
static AVSpeechSynthesizer* TtsEnsureSynth() {
    if (!gTtsSynth) {
        gTtsDelegate = [[SumatraTtsDelegate alloc] init];
        gTtsSynth = [[AVSpeechSynthesizer alloc] init];
        gTtsSynth.delegate = gTtsDelegate;
    }
    return gTtsSynth;
}

bool TtsSpeak(Str text, const char* langCode) {
    if (len(text) == 0) {
        return false;
    }
    std::call_once(gTtsOnce, TtsInitQueue);
    NSString* nsText = TtsNsStr(text);
    if (!nsText) {
        return false;
    }
    bool isCn = langCode && str::EqI(Str(langCode), StrL("cn"));
    NSString* lang = isCn ? @"zh-CN" : @"en-US";
    __block bool started = false;
    dispatch_sync(gTtsQueue, ^{
        @autoreleasepool {
            AVSpeechSynthesizer* synth = TtsEnsureSynth();
            // a previous read must not keep talking under the new one
            if (synth.isSpeaking) {
                [synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
            }
            NSArray<NSString*>* segments = TtsSegmentText(nsText);
            if (segments.count == 0) {
                return;
            }
            // the canceled previous session's segments never report back
            // (their callbacks are stale by generation), so start counting
            // from zero
            gTtsSegmentsLeft = 0;
            AVSpeechSynthesisVoice* voice = TtsPickVoice(lang);
            float rate = TtsMapRate(gTtsRate.load());
            int gen = ++gTtsGen;
            NSUInteger count = segments.count;
            for (NSUInteger i = 0; i < count; i++) {
                AVSpeechUtterance* utterance = [AVSpeechUtterance speechUtteranceWithString:segments[i]];
                utterance.voice = voice;
                utterance.rate = rate;
                // small pause before every segment but the last
                if (i + 1 < count) {
                    utterance.postUtteranceDelay = kTtsSegmentDelay;
                }
                objc_setAssociatedObject(utterance, kTtsGenKey, @(gen), OBJC_ASSOCIATION_RETAIN);
                gTtsSegmentsLeft++;
                [synth speakUtterance:utterance];
            }
            gTtsSpeaking = true;
            started = true;
        }
    });
    return started;
}

void TtsStop() {
    std::call_once(gTtsOnce, TtsInitQueue);
    dispatch_async(gTtsQueue, ^{
        @autoreleasepool {
            if (gTtsSynth && gTtsSynth.isSpeaking) {
                [gTtsSynth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
            }
            gTtsSegmentsLeft = 0;
            gTtsSpeaking = false;
        }
    });
}

bool TtsIsSpeaking() {
    return gTtsSpeaking.load();
}

void TtsSetRate(float rate) {
    gTtsRate.store(limitValue(rate, kTtsUserRateMin, kTtsUserRateMax));
}

float TtsGetRate() {
    return gTtsRate.load();
}
