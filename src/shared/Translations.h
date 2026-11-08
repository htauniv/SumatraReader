/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

namespace trans {

int GetLangsCount();

Str GetCurrentLangCode();

void SetCurrentLangByCode(Str langCode);
Str ValidateLangCode(Str langCode);

Str GetTranslation(Str s);
TempStr GetLangCodeByIdxTemp(int idx);
TempStr GetLangNameByIdxTemp(int idx);
bool IsCurrLangRtl();
Str DetectUserLang();
void Destroy();

#if !OS_WIN
// The OS's language preferences as BCP 47 tags, most preferred first. macOS
// reads System Settings (a Finder-launched app has no LC_ALL / LANG); builds
// without an implementation return nothing and DetectUserLang() then falls back
// to the environment.
void GetSystemPreferredLocales(StrVec& out);
// "zh-Hans-CN", "de_DE.UTF-8" or "pt_BR" to our language code; empty when we
// have no translation for it. Trailing subtags are dropped until one matches.
Str LangCodeFromLocale(Str locale);
#endif

} // namespace trans

Str Tr(Str s);
inline Str Tr(const char* s) {
    return Tr(Str(s));
}
template <int N>
inline Str Tr(const char (&s)[N]) {
    return Tr(Str((char*)s, N - 1));
}

// TrN() marks strings that need to be translated but are used in a context
// that doesn't allow calling Trans::GetTranslationTemp() (e.g. when used as part
// of a struct). This allows the translation manager script to see the string
// but they'll need additional code that does Trans::GetTranslationTemp() on them
#define TrN(x) StrL(x)
