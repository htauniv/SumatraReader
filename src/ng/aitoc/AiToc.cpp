/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// AI Table of Contents. See AiToc.h for the overview.

#include "base/Base.h"
#include "gui/UIModels.h"
#include "EngineBase.h"

#include "ng/aitoc/AiToc.h"

#include <curl/curl.h>

#include "base/File.h"
#include "base/JsonParser.h"
#include "AppTools.h"

// --- limits -----------------------------------------------------------------

static constexpr int kConnectTimeoutSecs = 10;
static constexpr int kTotalTimeoutSecs = 120;
static constexpr long kHttpOk = 200;
// statuses worth a single retry: transient server / rate-limit / timeout-ish
static constexpr long kRetryStatuses[] = {408, 425, 429, 500, 502, 503, 504};

static constexpr int kMaxCharsPerPage = 6000;  // per candidate page in the prompt
static constexpr int kMaxTotalChars = 48000;   // prompt budget for all pages together
static constexpr int kMaxResponseBytes = 16 * 1024 * 1024;

// --- config -----------------------------------------------------------------

// Reads <AppDataDir>/ai-toc.json, same directory as SumatraReader-settings.txt.
// Expected shape: {"baseUrl": "...", "apiKey": "...", "model": "..."}.
// The key is never logged. On success the Str members of out are heap copies;
// release them with str::Free() when no longer needed.
bool AiTocLoadConfig(AiTocConfig& out) {
    TempStr path = GetPathInAppDataDirTemp(StrL("ai-toc.json"));
    if (len(path) == 0) {
        return false;
    }
    Str data = file::ReadFile(path);
    if (len(data) == 0) {
        str::Free(data);
        return false;
    }

    AiTocConfig cfg;
    json::Reader r(data);
    for (json::Value& v : r) {
        if (v.type != json::Type::String) {
            continue;
        }
        // v.value is a temp-arena copy, valid only until the next Next();
        // dupe to the heap so the config survives past this function
        if (json::PathMatch(v.path, StrL("/baseUrl"))) {
            cfg.baseUrl = str::Dup(v.value);
        } else if (json::PathMatch(v.path, StrL("/apiKey"))) {
            cfg.apiKey = str::Dup(v.value);
        } else if (json::PathMatch(v.path, StrL("/model"))) {
            cfg.model = str::Dup(v.value);
        }
    }
    bool ok = r.Ok() && len(cfg.baseUrl) > 0 && len(cfg.apiKey) > 0 && len(cfg.model) > 0;
    if (ok) {
        out = cfg;
    } else {
        str::FreePtr(&cfg.baseUrl);
        str::FreePtr(&cfg.apiKey);
        str::FreePtr(&cfg.model);
    }
    str::Free(data);
    return ok;
}

// --- prompt construction ----------------------------------------------------

// The model must reply with a bare JSON array of [title, pageNo] pairs, where
// pageNo is copied from the "=== [page N] ===" markers we provide. Keeping the
// format flat makes parsing robust and trivially verifiable.
static const char* kSystemPrompt =
    "You are a precise table-of-contents extractor. The user gives you the text of "
    "several pages of a document; each page starts with a marker '=== [page N] ===' "
    "where N is the physical page number. "
    "Identify the table of contents (printed TOC page or structural headings) and "
    "produce a flat outline. "
    "Reply with ONLY a JSON array, no prose and no code fences. Each element is a "
    "two-element array: [\"entry title\", physicalPageNumber]. The page number MUST "
    "be copied verbatim from the marker of the page the entry points to; never "
    "compute or guess it. If no table of contents can be found, reply with []. "
    "Preserve the original entry titles and their order.";

// Appends one candidate page's text, truncated, after its marker.
static bool AppendPageText(str::Builder* prompt, EngineBase* engine, int pageNo, int& budgetLeft) {
    PageText pt;
    if (!engine->TryExtractPageText(pageNo, &pt)) {
        // no text on this page (pure scan without OCR layer): still emit the
        // marker so the model sees the page exists, then move on
        prompt->Append(fmt("=== [page %d] ===\n(no extractable text)\n\n", pageNo));
        return true;
    }
    Str text = pt.text;
    FreePageText(&pt);
    if (len(text) > kMaxCharsPerPage) {
        text = Str(text.s, kMaxCharsPerPage);
    }
    if (len(text) > budgetLeft) {
        text = Str(text.s, budgetLeft);
    }
    bool ok = prompt->Append(fmt("=== [page %d] ===\n", pageNo));
    ok = ok && prompt->Append(text);
    ok = ok && prompt->Append(StrL("\n\n"));
    budgetLeft -= len(text);
    return ok;
}

static bool BuildRequestBody(const AiTocConfig& cfg, EngineBase* engine, const Vec<int>& pages, str::Builder* bodyOut) {
    str::Builder prompt;
    str::BuilderReserve(prompt, 16 * 1024);
    int budgetLeft = kMaxTotalChars;
    for (int pageNo : pages) {
        if (budgetLeft <= 0) {
            break;
        }
        if (!AppendPageText(&prompt, engine, pageNo, budgetLeft)) {
            return false;
        }
    }
    if (len(prompt) == 0) {
        return false;
    }

    bodyOut->Append(StrL("{\"model\":"));
    bodyOut->Append(fmt("\"%s\"", json::EscapeStrTemp(cfg.model)));
    bodyOut->Append(StrL(",\"temperature\":0.2,\"messages\":["));
    bodyOut->Append(StrL("{\"role\":\"system\",\"content\":\""));
    bodyOut->Append(json::EscapeStrTemp(Str(kSystemPrompt)));
    bodyOut->Append(StrL("\"},{\"role\":\"user\",\"content\":\""));
    bodyOut->Append(json::EscapeStrTemp(ToStr(prompt)));
    bodyOut->Append(StrL("\"}]}"));
    return true;
}

// --- HTTP -------------------------------------------------------------------

static size_t WriteResponseCb(char* data, size_t size, size_t count, void* userData) {
    size_t n = size * count;
    auto* out = (str::Builder*)userData;
    if (len(*out) + (int)n > kMaxResponseBytes) {
        return 0; // abort: absurdly large response
    }
    return out->Append(Str(data, (int)n)) ? n : 0;
}

static bool ShouldRetryStatus(long status) {
    for (long s : kRetryStatuses) {
        if (status == s) {
            return true;
        }
    }
    return false;
}

// One POST to <baseUrl>/chat/completions. Returns true on HTTP 200 with the
// raw body in rspOut. Never logs the API key or the request body.
static bool PostChatCompletion(const AiTocConfig& cfg, Str body, str::Builder* rspOut, Str* errOut) {
    TempStr url = str::JoinTemp(cfg.baseUrl, StrL("/chat/completions"));
    logf("AiToc: POST %s\n", url);

    static bool curlGlobalReady = false;
    if (!curlGlobalReady) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            *errOut = StrL("curl_global_init failed");
            return false;
        }
        curlGlobalReady = true;
    }

    str::Builder auth;
    auth.Append(StrL("Authorization: Bearer "));
    auth.Append(cfg.apiKey);

    bool ok = false;
    CURLcode code = CURLE_FAILED_INIT;
    long status = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) {
            logf("AiToc: retrying POST (prev status %ld)\n", status);
        }
        rspOut->Reset();
        CURL* curl = curl_easy_init();
        if (!curl) {
            *errOut = StrL("curl_easy_init failed");
            return false;
        }
        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        // Builder storage is NUL-padded, so auth.els is a valid C string
        // (apiKey is validated non-empty by the caller)
        headers = curl_slist_append(headers, auth.els);

        curl_easy_setopt(curl, CURLOPT_URL, CStrTemp(url));
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "SumatraPdfHTTP");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)kConnectTimeoutSecs);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)kTotalTimeoutSecs);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.s ? body.s : "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)len(body));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponseCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, rspOut);

        code = curl_easy_perform(curl);
        status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (code == CURLE_OK && status == kHttpOk) {
            ok = true;
            break;
        }
        bool retriable = (code != CURLE_OK) || ShouldRetryStatus(status);
        if (!retriable || attempt == 1) {
            break;
        }
    }

    if (ok) {
        return true;
    }
    if (code != CURLE_OK) {
        *errOut = fmt("network error: %s (attempted %s)", Str(curl_easy_strerror(code)), url);
    } else {
        *errOut = fmt("HTTP status %ld from %s", status, url);
    }
    return false;
}

// --- response parsing -------------------------------------------------------

// Removes an optional ```json ... ``` fence around the payload.
static Str StripCodeFence(Str s) {
    str::TrimWSInPlace(s, str::TrimOpt::Both);
    if (!str::StartsWith(s, StrL("```"))) {
        return s;
    }
    // skip the fence line (e.g. "```json")
    int nl = str::IndexOfChar(s, '\n');
    if (nl < 0) {
        return s;
    }
    Str rest = Str(s.s + nl + 1, s.len - nl - 1);
    // cut the closing fence if present
    int close = str::LastIndexOfChar(rest, '`');
    if (close > 0) {
        rest = Str(rest.s, close);
    }
    str::TrimWSInPlace(rest, str::TrimOpt::Both);
    return rest;
}

// Pulls "choices[*].message.content" out of the chat completion response.
static bool ExtractMessageContent(Str rsp, str::Builder* contentOut) {
    json::Reader r(rsp);
    bool found = false;
    for (json::Value& v : r) {
        if (v.type != json::Type::String) {
            continue;
        }
        if (json::PathMatch(v.path, StrL("/choices"), StrL("*"), StrL("/message"), StrL("/content"))) {
            contentOut->Append(v.value);
            found = true;
        }
    }
    return found && r.Ok();
}

struct TocParseCtx {
    Vec<AiTocItem>* items = nullptr;
    int curIdx = -1;
};

// Accepts exactly-two-level leaves of the array [["title", pageNo], ...]:
//   /i/j == 0 → title of entry i
//   /i/j == 1 → page of entry i
static void OnTocValue(TocParseCtx* ctx, json::Value& v) {
    StrNode* segEntry = json::PathNth(v.path, 0);
    StrNode* segField = json::PathNth(v.path, 1);
    if (!segEntry || !segField || json::PathNth(v.path, 2)) {
        return; // deeper nesting: ignore
    }
    int entry = json::PathSegIndex(segEntry);
    int field = json::PathSegIndex(segField);
    if (entry < 0 || field < 0) {
        return;
    }
    if (field == 0 && v.type == json::Type::String) {
        // a new entry starts with its title; entries arrive in order
        if (entry != ctx->curIdx + 1) {
            return;
        }
        AiTocItem item;
        item.title = str::Dup(v.value);
        VecAppend(*ctx->items, item);
        ctx->curIdx = entry;
        return;
    }
    if (field == 1 && v.type == json::Type::Number && ctx->items->len > 0 && entry == ctx->curIdx) {
        int pageNo = ParseInt(v.value);
        VecLast(*ctx->items).pageNo = pageNo;
    }
}

static bool ParseTocArray(Str content, Vec<AiTocItem>& out) {
    TocParseCtx ctx;
    ctx.items = &out;
    json::Reader r(content);
    for (json::Value& v : r) {
        OnTocValue(&ctx, v);
    }
    return r.Ok();
}

// Drops entries with invalid page numbers (model hallucinations) and sorts the
// rest by page, keeping the model's order for equal pages.
static void FilterAndSort(Vec<AiTocItem>& items, int maxPage) {
    for (int i = items.len - 1; i >= 0; i--) {
        if (items[i].pageNo < 1 || items[i].pageNo > maxPage || len(items[i].title) == 0) {
            str::Free(items[i].title);
            VecRemoveAt(items, i);
        }
    }
    // insertion sort: stable, and the input is small
    for (int i = 1; i < items.len; i++) {
        AiTocItem key = items[i];
        int j = i - 1;
        while (j >= 0 && items[j].pageNo > key.pageNo) {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }
}

// --- public entry point -----------------------------------------------------

bool AiTocGenerate(EngineBase* engine, const AiTocConfig& cfg, const Vec<int>& candidatePages, Vec<AiTocItem>& outToc,
                   Str& outError, const AiTocProgressCb* progress) {
    outError = {};
    if (!engine || len(cfg.baseUrl) == 0 || len(cfg.apiKey) == 0 || len(cfg.model) == 0) {
        outError = StrL("invalid arguments (engine or config missing)");
        return false;
    }
    if (!str::StartsWithI(cfg.baseUrl, StrL("https://"))) {
        // the API key must never travel over plain http
        outError = StrL("baseUrl must use https://");
        return false;
    }
    int maxPage = engine->PageCount();
    if (maxPage < 1) {
        outError = StrL("document has no pages");
        return false;
    }
    if (len(candidatePages) == 0) {
        outError = StrL("no candidate pages given");
        return false;
    }

    if (progress) {
        progress->Call(5);
    }

    str::Builder body;
    if (!BuildRequestBody(cfg, engine, candidatePages, &body)) {
        outError = StrL("failed to build request (no extractable text?)");
        return false;
    }
    if (progress) {
        progress->Call(20);
    }

    str::Builder rsp;
    Str err;
    if (!PostChatCompletion(cfg, ToStr(body), &rsp, &err)) {
        outError = err;
        return false;
    }
    if (progress) {
        progress->Call(80);
    }

    str::Builder content;
    if (!ExtractMessageContent(ToStr(rsp), &content)) {
        outError = StrL("unexpected API response (no choices[].message.content)");
        return false;
    }

    Str payload = StripCodeFence(ToStr(content));
    if (len(payload) == 0) {
        outError = StrL("model returned empty content");
        return false;
    }

    for (AiTocItem& item : outToc) {
        str::Free(item.title);
    }
    VecReset(outToc);
    if (!ParseTocArray(payload, outToc)) {
        outError = StrL("model reply is not valid JSON");
        for (AiTocItem& item : outToc) {
            str::Free(item.title);
        }
        VecReset(outToc);
        return false;
    }
    FilterAndSort(outToc, maxPage);
    if (len(outToc) == 0) {
        outError = StrL("no table of contents detected in the candidate pages");
        return false;
    }
    if (progress) {
        progress->Call(100);
    }
    return true;
}
