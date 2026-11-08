/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// ng: offline dictionary lookup over StarDict dictionaries, the data half of
// the "Lookup Word" command (SumatraPDF-Plus' double-click look-up, minus the
// double-click trigger; that needs a selection-changed hook and is a follow-up).
//
// A dictionary is the standard StarDict triple: <base>.ifo (metadata),
// <base>.idx (the word index) and <base>.dict (the articles) or its
// gzip-compressed form <base>.dict.dz. All files live in one directory
// (<AppDataDir>/dict); every .ifo found there is loaded.
//
// Memory: the .idx files are held in full (they are the search index, typical
// English-Chinese dictionaries are 5-50 MB) and a .dict.dz is decompressed to
// memory in full on load (the uncompressed size is logged; uncompressed .dict
// files are seeked on demand instead). Articles are read per lookup.
//
// Threading: DictLoadDictionaries() loads once (a second call is a no-op) and
// is mutex-guarded, so a lazy first call from any thread is safe. After load,
// DictLookupWord() only reads immutable state and can run concurrently.
//
// Strings: every Str handed out is allocated from gPermArena and lives for the
// process' lifetime; callers neither copy nor free them.

struct DictEntry {
    // the dictionary's bookname from the .ifo, e.g. "Oxford English-Chinese"
    Str dictName;
    // the article's text, html/xdxf tags stripped to plain text
    Str definition;
};

// Loads every .ifo in dirPath. Returns the number of dictionaries loaded
// successfully (0 if the directory is missing, empty or unusable); failures
// per dictionary are logged and summarized by DictLastError().
int DictLoadDictionaries(const char* dirPath);

// Looks up word (case-insensitive; on a miss retries a lowercased form and
// a light de-inflection of trailing 's / s / es / ed / ing) across all loaded
// dictionaries. Appends the matches to out (dictName / definition strings are
// perma-arena, see above). Returns true if at least one entry was appended.
bool DictLookupWord(const char* word, Vec<DictEntry>& out);

// The last error recorded while loading (allocation failed, a .ifo field was
// missing, .idx size mismatch, unsupported sametypesequence, ...). Empty
// string when no dictionary failed. Never null; owned permanently.
const char* DictLastError();
