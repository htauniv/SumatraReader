// Runtime test for the translation subsystem of the local macOS build.
//
// It links the exact same object files that go into SumatraPDF, so this
// exercises the shipped parser, the generated language table and the real
// embedded.lzsa archive - not a re-implementation of them.
//
// Build/link is done by tools/trans_runtime_test.sh.

#include "base/Base.h"
#include "EmbeddedResources.h"
#include "Translations.h"

#include <cstdio>
#include <cstdlib>

// The app normally supplies these two; this harness links only base + the
// translation objects, so provide no-op versions (same as upstream's own
// src/tools/* stubs and src/base/LogNoOp.cpp).
void log(Str /*s*/) {}
void _uploadDebugReport(Str, Str, bool) {}

// Same stubs upstream uses in src/ng/tools/test_embedded.cpp: the embedded-
// resource loader also reaches into mupdf's builtin font table, which this
// test doesn't need. C linkage means the parameter types aren't in the symbol.
extern "C" void fz_set_builtin_font_loader(void*) {}
extern "C" const char* sumatra_lookup_font_url(const char*) { return nullptr; }
extern "C" int sumatra_font_count(void) { return 0; }
extern "C" const char* sumatra_font_at(int, const char**, int*) { return nullptr; }
extern "C" unsigned char* sumatra_system_font_data_for_rune(unsigned int, int* size) {
    *size = 0;
    return nullptr;
}

// Match the shipped release configuration: English doesn't parse the archive.
bool gIsDebugBuild = false;

static void printStr(const char* label, Str s) {
    if (!s.s) {
        printf("  %-24s <null>\n", label);
        return;
    }
    printf("  %-24s %.*s\n", label, s.len, s.s);
}

static void show(const char* tag, const char* key) {
    Str r = Tr(key);
    printf("  %-8s %-22s => %.*s\n", tag, key, r.len, r.s ? r.s : "");
}

static bool embeddedHas(const char* name, int minSize) {
    int size = 0;
    u8* d = GetEmbeddedFileData(Str(name), &size);
    bool ok = d && size >= minSize;
    printf("  %-36s %6d bytes %s\n", name, size, ok ? "OK" : "MISSING/TOO SMALL");
    if (d) free(d);
    return ok;
}

static bool strIs(Str s, const char* want) {
    int n = (int)strlen(want);
    if (s.len != n) return false;
    return n == 0 || memcmp(s.s, want, n) == 0;
}

// The OS reports BCP 47 tags, apptranslator codes differ for a dozen
// languages. A Finder-launched app has no $LANG, so this mapping is what makes
// a Chinese Mac start in Chinese.
static bool localeCases() {
    struct Case {
        const char* in;
        const char* want;
    };
    // zh-Hant -> cn is this build's deviation: it ships en + cn only, so a
    // Traditional locale gets the Simplified UI instead of English
    const Case cases[] = {
        {"zh-Hans-CN", "cn"}, {"zh-Hans", "cn"}, {"zh_CN.UTF-8", "cn"}, {"zh-CN", "cn"},
        {"zh-Hant-TW", "cn"}, {"zh-TW", "cn"},  {"zh_HK", "cn"},      {"zh", "cn"},
        {"en-US", "en"},      {"en_GB.UTF-8", "en"}, {"en", "en"},
        // no translation shipped: must report "unknown", not a wrong code
        {"fr-FR", ""},        {"de-DE", ""},   {"ja-JP", ""},
        // neither is a language
        {"C", ""},            {"POSIX", ""},   {"", ""},
    };
    bool ok = true;
    for (const Case& c : cases) {
        Str got = trans::LangCodeFromLocale(Str((char*)c.in));
        bool same = strIs(got, c.want);
        printf("  %-14s -> %-4.*s %s\n", c.in[0] ? c.in : "(empty)", got.len, got.s ? got.s : "",
               same ? "OK" : "WRONG");
        if (!same) ok = false;
    }
    return ok;
}

int main() {
    printf("=== language table (%d entries) ===\n", trans::GetLangsCount());
    for (int i = 0; i < trans::GetLangsCount(); i++) {
        Str code = trans::GetLangCodeByIdxTemp(i);
        Str name = trans::GetLangNameByIdxTemp(i);
        printf("  [%d] %.*s / %.*s\n", i, code.len, code.s, name.len, name.s);
    }

    printStr("DetectUserLang():", trans::DetectUserLang());
    printStr("ValidateLangCode(en):", trans::ValidateLangCode(Str("en")));
    printStr("ValidateLangCode(cn):", trans::ValidateLangCode(Str("cn")));
    printStr("ValidateLangCode(zh_CN):", trans::ValidateLangCode(Str("zh_CN")));
    printStr("ValidateLangCode(de):", trans::ValidateLangCode(Str("de")));

    const char* keys[] = {
        "&File", "&Open", "&Settings", "&Close", "%d pages", "# History",
    };

    printf("=== default language ===\n");
    printStr("current:", trans::GetCurrentLangCode());
    for (const char* k : keys) show("default", k);

    printf("=== switch to cn ===\n");
    trans::SetCurrentLangByCode(Str("cn"));
    printStr("current:", trans::GetCurrentLangCode());
    int nCn = 0;
    for (const char* k : keys) {
        Str r = Tr(k);
        if (r.len > 0 && r.s && (unsigned char)r.s[0] >= 0x80) nCn++;
        show("cn", k);
    }

    printf("=== switch back to en ===\n");
    trans::SetCurrentLangByCode(Str("en"));
    printStr("current:", trans::GetCurrentLangCode());
    int nEn = 0;
    for (const char* k : keys) {
        Str r = Tr(k);
        if (r.len > 0 && r.s && (unsigned char)r.s[0] < 0x80) nEn++;
        show("en", k);
    }

    printf("=== locale -> language code ===\n");
    bool localeOk = localeCases();

    printf("=== embedded resources ===\n");
    bool manual = true;    manual &= embeddedHas("manual.shell.html", 1000);
    manual &= embeddedHas("manifest.json", 50);
    manual &= embeddedHas("all-docs.json", 20);
    manual &= embeddedHas("SumatraPDF-documentation.md", 200);
    manual &= embeddedHas("Keyboard-shortcuts.md", 500);
    manual &= embeddedHas("gen_docs.render.js", 1000);
    manual &= embeddedHas("markdown-it.min.js", 1000);
    manual &= embeddedHas("translations.txt", 1000);

    printf("=== verdict ===\n");
    bool ok = trans::GetLangsCount() == 2 && nCn > 0 &&
              nEn == (int)(sizeof(keys) / sizeof(keys[0])) && manual && localeOk;
    printf("  cn strings with CJK bytes: %d/%zu\n", nCn, sizeof(keys) / sizeof(keys[0]));
    printf("  en strings restored:       %d/%zu\n", nEn, sizeof(keys) / sizeof(keys[0]));
    printf("  locale mapping:            %s\n", localeOk ? "OK" : "FAIL");
    printf("  offline manual resources:  %s\n", manual ? "OK" : "FAIL");
    printf("  RESULT: %s\n", ok ? "TRANSLATION_RUNTIME_OK" : "TRANSLATION_RUNTIME_FAIL");
    trans::Destroy();
    return ok ? 0 : 1;
}
