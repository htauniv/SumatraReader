/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// AI Table of Contents: extract text from candidate pages of a scanned or
// bookmark-less PDF, ask an OpenAI-compatible chat API to produce a bookmark
// outline and return it as a flat list of (title, 1-based pageNo) items.
//
// Repository header convention: no includes here; the including .cpp must
// pull in base/Base.h (and EngineBase.h for the EngineBase type) first.
// Integration notes live in integrate/aitoc.md (repo root, outside src/).

struct AiTocItem {
    Str title;
    int pageNo = 0;
};

// connection settings for an OpenAI-compatible chat completions endpoint.
// baseUrl is the API root without a trailing slash, e.g. "https://api.xx.com/v1";
// the module appends "/chat/completions" itself.
// Strs returned by AiTocLoadConfig are heap-owned; free with str::Free().
struct AiTocConfig {
    Str baseUrl;
    Str apiKey;
    Str model;
};

// optional progress notification; arg is a percentage 0..100
using AiTocProgressCb = Func1<int>;

// Reads <AppDataDir>/ai-toc.json (same directory as SumatraReader-settings.txt)
// into out. Returns false (with out untouched) when the file is missing,
// malformed, or any of baseUrl/apiKey/model is empty.
bool AiTocLoadConfig(AiTocConfig& out);

// Extracts text from candidatePages (1-based; clamped to the document page
// count), POSTs it to the chat completions endpoint described by cfg and
// parses the model reply into outToc (sorted by pageNo). Never logs the API
// key. progress may be nullptr.
// On failure returns false and sets outError to a human-readable message
// (never containing the key).
bool AiTocGenerate(EngineBase* engine, const AiTocConfig& cfg, const Vec<int>& candidatePages,
                   Vec<AiTocItem>& outToc, Str& outError, const AiTocProgressCb* progress = nullptr);
