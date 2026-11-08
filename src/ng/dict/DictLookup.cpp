/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// ng: the StarDict parser behind DictLookup.h (see it for the data model,
// memory and threading notes). Format references:
// https://github.com/huzheng001/stardict-3/blob/stardict-3.0.7/dict/doc/StarDictFileFormat
//
// What is supported, in one place:
// - .ifo: version 2.4.2 and 3.x, bookname, wordcount, idxfilesize,
//   idxoffsetbits (32 or 64, default 32), sametypesequence
// - .idx: word\0 + big-endian (offset, size); loaded in full, parsed into a
//   sorted table, queried by binary search with an ASCII case-fold compare
// - .dict: opened and seeked per lookup (nothing but the article is read)
// - .dict.dz: dictzip (a gzip stream with an RA extra field); decompressed to
//   memory in full on load. Memory estimate: the uncompressed size, which is
//   the ISIZE field of the gzip trailer (what file::GetSize reports minus the
//   compressed size; logged at load). Typical English-Chinese dictionaries
//   decompress to tens to a few hundred MB; kMaxDictDzUncompressed caps it.
// - sametypesequence: m, l, g, t, y kept as text; h (html) and x (xdxf)
//   stripped to plain text; w (wav audio) and r (resource file) skipped;
//   anything else rejects the dictionary at load time.
//
// Known limitations: no Unicode case folding (ASCII only, so "STRASSE" does
// not match "straße"), no .syn synonym files, no .dict.zip (only .dz), no
// resource (res/) support, no sametypesequence fields with per-field sizes
// other than the format's u32 (that is what the format defines), and no
// re-adding of inflections (we only strip them).

#include "base/Base.h"
#include "base/DirScan.h"
#include "base/File.h"

#include <zlib.h>

#include "dict/DictLookup.h"

#include "SumatraLog.h"

// sanity caps: a .idx or .dict.dz bigger than this is treated as a bad file.
// The 1 GB .dz cap is ~2x the largest published StarDict dictionary.
constexpr i64 kMaxIdxFileSize = 512 * 1024 * 1024;
constexpr i64 kMaxDictFileSize = 1024ll * 1024 * 1024;
// a single article bigger than this is rejected (corrupt .idx offsets point
// into the middle of other articles far more often than real articles are
// this large)
constexpr u32 kMaxArticleSize = 4 * 1024 * 1024;
// .ifo files are tiny; a bigger one is not a .ifo
constexpr i64 kMaxIfoSize = 64 * 1024;

// one parsed .idx entry. word points into the book's idxData buffer
struct IdxEntry {
    const u8* word;
    int wordLen;
    u64 offset;
    u32 size;
};

// one loaded dictionary
struct DictBook {
    Str bookName;      // perma
    Str stss;          // perma copy of the .ifo's sametypesequence (possibly empty)
    const u8* idxData; // perma copy of the whole .idx
    int idxLen;
    Vec<IdxEntry> entries; // sorted by ASCII-case-folded word

    file::FileHandle dictFile = file::kInvalidFileHandle; // uncompressed .dict
    const u8* dzData = nullptr;                     // decompressed .dict.dz
    i64 dictSize = 0;
};

static Vec<DictBook*> gBooks;
static bool gLoaded = false;
static Mutex gDictLoadMutex;

// last load error, perma-allocated. Written under gDictLoadMutex during load,
// read concurrently afterwards - like the books, it never changes once load
// finished
static const char* gLastError = nullptr;

// --- small helpers -----------------------------------------------------------

static void SetErrorTemp(TempStr msg) {
    gLastError = str::Dup(GetPermArena(), msg).s;
}

// ASCII case fold: A-Z only. No Unicode folding (documented limitation)
static inline u8 FoldAscii(u8 c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

// g_ascii_strcasecmp() semantics, which is what StarDict sorted .idx with:
// fold ASCII, shorter word sorts first
static int CmpWords(const u8* a, int aLen, const u8* b, int bLen) {
    int n = std::min(aLen, bLen);
    for (int i = 0; i < n; i++) {
        u8 ca = FoldAscii(a[i]);
        u8 cb = FoldAscii(b[i]);
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
    }
    if (aLen == bLen) {
        return 0;
    }
    return aLen < bLen ? -1 : 1;
}

static int CmpIdxEntry(const IdxEntry* a, const IdxEntry* b) {
    return CmpWords(a->word, a->wordLen, b->word, b->wordLen);
}

static inline u32 ReadBE32(const u8* d) {
    return ((u32)d[0] << 24) | ((u32)d[1] << 16) | ((u32)d[2] << 8) | (u32)d[3];
}

static inline u64 ReadBE64(const u8* d) {
    return ((u64)ReadBE32(d) << 32) | (u64)ReadBE32(d + 4);
}

// --- .ifo --------------------------------------------------------------------

// a parsed .ifo. Strings point into ifoData (a scratch copy, freed by the
// caller's AutoArenaSavepoint), so they are only valid while it is alive
struct IfoInfo {
    Str version;
    Str bookName;
    Str sametypesequence;
    i64 wordCount = -1;   // -1: field missing (allowed, the format has it optional in practice)
    i64 idxFileSize = -1; // required
    int idxOffsetBits = 32;
};

static Str IfoValue(Str ifoData, Str key) {
    Str line;
    Str rest = ifoData;
    while (str::NextLine(rest, line, rest)) {
        Str before, after;
        if (!str::CutChar(line, '=', &before, &after)) {
            continue;
        }
        str::TrimWSInPlace(before, str::TrimOpt::Both);
        if (str::EqI(before, key)) {
            str::TrimWSInPlace(after, str::TrimOpt::Both);
            return after;
        }
    }
    return {};
}

static bool ParseIfo(Str ifoData, IfoInfo& infoOut) {
    infoOut.version = IfoValue(ifoData, StrL("version"));
    infoOut.bookName = IfoValue(ifoData, StrL("bookname"));
    infoOut.sametypesequence = IfoValue(ifoData, StrL("sametypesequence"));
    Str wc = IfoValue(ifoData, StrL("wordcount"));
    if (len(wc) > 0) {
        infoOut.wordCount = ParseInt(wc);
    }
    Str ifs = IfoValue(ifoData, StrL("idxfilesize"));
    if (len(ifs) > 0) {
        infoOut.idxFileSize = ParseInt64(ifs);
    }
    Str iob = IfoValue(ifoData, StrL("idxoffsetbits"));
    if (len(iob) > 0) {
        infoOut.idxOffsetBits = ParseInt(iob);
    }

    if (str::IsEmptyOrWhiteSpace(infoOut.bookName)) {
        return false;
    }
    if (infoOut.idxFileSize < 0) {
        return false;
    }
    if (infoOut.idxOffsetBits != 32 && infoOut.idxOffsetBits != 64) {
        return false;
    }
    // the format's versions: 2.4.2 and 3.x (2.4.2 is what every real-world
    // dictionary ships). Anything else is not a StarDict we can parse
    if (!(str::StartsWith(infoOut.version, StrL("2.4")) || str::StartsWith(infoOut.version, StrL("3.")))) {
        return false;
    }
    // unknown sametypesequence: reject the dictionary here, per one entry
    // below is too late to be safe
    for (int i = 0; i < len(infoOut.sametypesequence); i++) {
        char c = infoOut.sametypesequence.s[i];
        if (!str::ContainsChar(StrL("mlgthxywr"), c)) {
            return false;
        }
    }
    return true;
}

// --- article decoding --------------------------------------------------------

// true if c is one of the article data types we keep text of
static bool IsTextFieldType(char c) {
    return str::ContainsChar(StrL("mlgty"), c);
}

// strips <tag> markup to plain text: <br>, </p>, </div>, </tr>, </li> become
// newlines, the rest of the tags are dropped, entities are decoded
static void AppendHtmlAsText(str::Builder& b, Str html) {
    bool inTag = false;
    char tagLower[16];
    int tagLen = 0;
    for (int i = 0; i < len(html); i++) {
        char c = html.s[i];
        if (inTag) {
            if (c == '>') {
                inTag = false;
                // the few tags that mean a line break in the rendered article
                if (tagLen > 0 && (str::StartsWith(Str(tagLower, tagLen), StrL("br")) ||
                                   str::StartsWith(Str(tagLower, tagLen), StrL("/p")) ||
                                   str::StartsWith(Str(tagLower, tagLen), StrL("/div")) ||
                                   str::StartsWith(Str(tagLower, tagLen), StrL("/tr")) ||
                                   str::StartsWith(Str(tagLower, tagLen), StrL("/li")))) {
                    b.AppendChar('\n');
                }
                tagLen = 0;
                continue;
            }
            if (tagLen < dimofi(tagLower) - 1) {
                tagLower[tagLen++] = FoldAscii((u8)c);
            }
            continue;
        }
        if (c == '<') {
            inTag = true;
            continue;
        }
        b.AppendChar(c);
    }
}

// decodes the &amp;/&#NN;/&#xHH; entities html stripping left behind. Runs on
// the tag-free text, so '<' and '&' only appear as entities
static void AppendEntityDecoded(str::Builder& b, Str s) {
    for (int i = 0; i < len(s); i++) {
        char c = s.s[i];
        if (c != '&') {
            b.AppendChar(c);
            continue;
        }
        Str rest(s.s + i + 1, len(s) - i - 1);
        if (str::TrimPrefix(rest, StrL("amp")) || str::TrimPrefix(rest, StrL("AMP"))) {
            str::TrimPrefix(rest, StrL(";"));
            b.AppendChar('&');
            i += len(s) - len(rest) - 1;
            continue;
        }
        if (str::TrimPrefix(rest, StrL("lt"))) {
            str::TrimPrefix(rest, StrL(";"));
            b.AppendChar('<');
            i += len(s) - len(rest) - 1;
            continue;
        }
        if (str::TrimPrefix(rest, StrL("gt"))) {
            str::TrimPrefix(rest, StrL(";"));
            b.AppendChar('>');
            i += len(s) - len(rest) - 1;
            continue;
        }
        if (str::TrimPrefix(rest, StrL("quot"))) {
            str::TrimPrefix(rest, StrL(";"));
            b.AppendChar('"');
            i += len(s) - len(rest) - 1;
            continue;
        }
        if (str::TrimPrefix(rest, StrL("apos"))) {
            str::TrimPrefix(rest, StrL(";"));
            b.AppendChar('\'');
            i += len(s) - len(rest) - 1;
            continue;
        }
        if (str::TrimPrefix(rest, StrL("nbsp"))) {
            str::TrimPrefix(rest, StrL(";"));
            b.AppendChar(' ');
            i += len(s) - len(rest) - 1;
            continue;
        }
        // &#123; and &#x1A;: the codepoint, UTF-8 encoded
        if (i + 2 < len(s) && s.s[i + 1] == '#') {
            Str num(s.s + i + 2, len(s) - i - 2);
            bool isHex = str::TrimPrefixI(num, StrL("x"));
            if (str::TrimSuffix(num, StrL(";")) > 0 && len(num) > 0 && len(num) < 8) {
                int cp = 0;
                bool ok = true;
                for (int j = 0; j < len(num); j++) {
                    char d = num.s[j];
                    int v = str::HexDigitVal(d);
                    if (v < 0 || (!isHex && !str::IsDigit(d))) {
                        ok = false;
                        break;
                    }
                    cp = cp * (isHex ? 16 : 10) + v;
                }
                if (ok && cp > 0) {
                    char utf8[4];
                    int off = 0;
                    str::Utf8Encode(utf8, off, cp);
                    b.Append(Str(utf8, off));
                    i += len(s) - len(num) - 2 + 1 - 1;
                    continue;
                }
            }
        }
        // not an entity we know: keep the '&' as-is
        b.AppendChar('&');
    }
}

// one field of an article, appended to out as plain text
static void AppendFieldAsText(str::Builder& b, char type, Str data) {
    if (type == 'h') {
        str::Builder tagFree;
        AppendHtmlAsText(tagFree, data);
        AppendEntityDecoded(b, ToStr(tagFree));
        return;
    }
    if (type == 'x') {
        // xdxf is xml-ish: the same tag stripping and entity decoding applies
        str::Builder tagFree;
        AppendHtmlAsText(tagFree, data);
        AppendEntityDecoded(b, ToStr(tagFree));
        return;
    }
    // m, l, g, t, y are plain text (l nominally Latin-1; kept as-is, see the
    // header's limitations). t is a phonetic block
    if (type == 't' && len(b) > 0) {
        b.AppendChar('\n');
    }
    AppendEntityDecoded(b, data);
}

// decodes one article (the .dict bytes at the entry's offset). The result is
// perma-allocated. An article with no text field (e.g. audio-only) yields {}
static Str DecodeArticle(IfoInfo& ifo, const u8* data, u32 size) {
    str::Builder b;
    b.a = GetPermArena();

    if (len(ifo.sametypesequence) == 0) {
        // per-field: type char + u32 BE size, until the article is exhausted
        u32 off = 0;
        bool firstField = true;
        while (off + 5 <= size) {
            char type = (char)data[off];
            if (!IsTextFieldType(type) && type != 'w' && type != 'r') {
                // unknown in-article type: keep what we have, log and stop.
                // Load-time validation rejects dictionaries whose declared
                // types are unknown; a stray type in one article is not worth
                // losing the whole dictionary over
                logf("DictLookup: unknown article data type '%c'\n", type);
                break;
            }
            u32 fieldSize = ReadBE32(data + off + 1);
            if (fieldSize > size - off - 5) {
                break; // corrupt size: stop rather than read past the article
            }
            Str field((char*)(data + off + 5), (int)fieldSize);
            if (IsTextFieldType(type)) {
                if (!firstField && len(b) > 0) {
                    b.AppendChar('\n');
                }
                AppendFieldAsText(b, type, field);
            }
            firstField = false;
            off += 5 + fieldSize;
        }
    } else {
        // sametypesequence says the article is exactly these types. All fields
        // but the last carry a u32 BE size; the last runs to the end. We keep
        // text of the text fields and skip w / r
        u32 off = 0;
        int seqLen = len(ifo.sametypesequence);
        for (int i = 0; i < seqLen; i++) {
            char type = ifo.sametypesequence.s[i];
            bool isLast = (i == seqLen - 1);
            u32 fieldSize = 0;
            if (!isLast) {
                if (off + 4 > size) {
                    break; // corrupt: not enough bytes for the size field
                }
                fieldSize = ReadBE32(data + off);
                off += 4;
                if (fieldSize > size - off) {
                    break;
                }
            } else {
                fieldSize = size - off;
            }
            Str field((char*)(data + off), (int)fieldSize);
            if (IsTextFieldType(type) && len(field) > 0) {
                if (len(b) > 0) {
                    b.AppendChar('\n');
                }
                AppendFieldAsText(b, type, field);
            }
            off += fieldSize;
        }
    }

    Str res = ToStr(b);
    // drop trailing whitespace and stray '\r'
    str::TrimWSInPlace(res, str::TrimOpt::Right);
    res = str::ReplaceTemp(res, StrL("\r"), StrL(""));
    return str::Dup(GetPermArena(), res);
}

// --- article reading ---------------------------------------------------------

static Str ReadArticle(DictBook* book, IfoInfo& ifo, const IdxEntry& e) {
    if (e.offset >= (u64)book->dictSize || e.size > book->dictSize - e.offset) {
        // rejected at parse time too; a book cannot get here, but a corrupt
        // sort table could, so stay defensive
        return {};
    }
    if (e.size == 0) {
        return {};
    }
    if (e.size > kMaxArticleSize) {
        logf("DictLookup: article of '%s' at offset %llu is %u bytes, over the cap\n", book->bookName,
             (unsigned long long)e.offset, e.size);
        return {};
    }
    if (book->dzData) {
        return DecodeArticle(ifo, book->dzData + e.offset, e.size);
    }
    auto buf = AllocArrayTemp<u8>((int)e.size);
    if (!buf) {
        return {};
    }
    if (!file::ReadAt(book->dictFile, (i64)e.offset, buf, (int)e.size)) {
        logf("DictLookup: failed to read %u bytes at offset %llu of '%s'\n", e.size,
             (unsigned long long)e.offset, book->bookName);
        return {};
    }
    return DecodeArticle(ifo, buf, e.size);
}

// --- .dict.dz ----------------------------------------------------------------

// dictzip is a gzip stream whose FEXTRA carries the random-access chunk table
// we don't need (we decompress everything). Returns the decompressed bytes in
// perma memory, {} on any error
static Str DecompressDictDz(Str path, i64 fileSize) {
    auto fileData = AllocArrayTemp<u8>((int)fileSize);
    if (!fileData) {
        SetErrorTemp(fmt("out of memory reading '%s'", path));
        return {};
    }
    if (!file::ReadN(path, fileData, (size_t)fileSize)) {
        SetErrorTemp(fmt("failed to read '%s'", path));
        return {};
    }
    if (fileSize < 18) { // 10-byte header + 8-byte trailer minimum
        SetErrorTemp(fmt("'%s' is too small to be a .dict.dz", path));
        return {};
    }
    if (fileData[0] != 0x1f || fileData[1] != 0x8b || fileData[2] != 8) {
        SetErrorTemp(fmt("'%s' is not a gzip file", path));
        return {};
    }
    // parse the gzip header: FLG bits FTEXT(0) FHCRC(1) FEXTRA(2) FNAME(3)
    // FCOMMENT(4)
    u8 flg = fileData[3];
    i64 off = 10;
    if (bit::IsSet(flg, 2)) { // FEXTRA: 2-byte LE length then the field
        if (off + 2 > fileSize) {
            SetErrorTemp(fmt("'%s' has a truncated gzip header", path));
            return {};
        }
        int xlen = fileData[off] | (fileData[off + 1] << 8);
        off += 2 + xlen;
    }
    auto skipZeroTerm = [&](i64* p) {
        while (*p < fileSize && fileData[*p] != 0) {
            (*p)++;
        }
        if (*p < fileSize) {
            (*p)++; // the terminator
        }
    };
    if (bit::IsSet(flg, 3)) {
        skipZeroTerm(&off); // FNAME
    }
    if (bit::IsSet(flg, 4)) {
        skipZeroTerm(&off); // FCOMMENT
    }
    if (bit::IsSet(flg, 1)) {
        off += 2; // FHCRC
    }
    if (off >= fileSize - 8) {
        SetErrorTemp(fmt("'%s' has a truncated gzip header", path));
        return {};
    }
    // ISIZE: the uncompressed size mod 2^32, little-endian, in the trailer
    u64 isize = (u64)fileData[fileSize - 4] | ((u64)fileData[fileSize - 3] << 8) |
                ((u64)fileData[fileSize - 2] << 16) | ((u64)fileData[fileSize - 1] << 24);
    if (isize == 0 || isize > (u64)kMaxDictFileSize) {
        SetErrorTemp(fmt("'%s': cannot determine the uncompressed size (%llu bytes, cap %lld)", path,
                         (unsigned long long)isize, kMaxDictFileSize));
        return {};
    }
    // the memory this book costs from here on: the uncompressed .dict
    logf("DictLookup: decompressing '%s', ~%llu MB in memory\n", path, (unsigned long long)(isize >> 20));

    auto out = AllocArray<u8>(GetPermArena(), (int)isize);
    if (!out) {
        SetErrorTemp(fmt("out of memory decompressing '%s' (%llu bytes)", path, (unsigned long long)isize));
        return {};
    }

    z_stream zs;
    ZeroStruct(&zs);
    zs.next_in = fileData + off;
    zs.avail_in = (u32)(fileSize - off - 8);
    zs.next_out = out;
    zs.avail_out = (u32)isize;
    // raw deflate (windowBits = -15): we consumed the gzip header ourselves
    if (inflateInit2(&zs, -15) != Z_OK) {
        SetErrorTemp(fmt("zlib init failed for '%s'", path));
        return {};
    }
    int zerr = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (zerr != Z_STREAM_END) {
        SetErrorTemp(fmt("failed to decompress '%s' (zlib error %d)", path, zerr));
        return {};
    }
    if (zs.total_out != isize) {
        SetErrorTemp(fmt("'%s': decompressed %llu bytes, expected %llu", path,
                         (unsigned long long)zs.total_out, (unsigned long long)isize));
        return {};
    }
    return Str((char*)out, (int)isize);
}

// --- .idx --------------------------------------------------------------------

// parses the .idx into book->entries. word pointers go into idxData; offset
// and size are validated against dictSize. False on a corrupt index
static bool ParseIdx(DictBook* book, IfoInfo& ifo) {
    Str data((char*)book->idxData, book->idxLen);
    int off = 0;
    int offsetBytes = ifo.idxOffsetBits / 8;
    while (off < book->idxLen) {
        // word\0
        int wordStart = off;
        while (off < book->idxLen && data.s[off] != '\0') {
            off++;
        }
        if (off >= book->idxLen) {
            return false; // the last word has no terminator
        }
        int wordLen = off - wordStart;
        off++; // the '\0'
        if (wordLen == 0) {
            return false; // an empty keyword is not in the format
        }
        if (off + offsetBytes + 4 > book->idxLen) {
            return false; // truncated offset/size
        }
        u64 offset = (ifo.idxOffsetBits == 64) ? ReadBE64(book->idxData + off) : (u64)ReadBE32(book->idxData + off);
        off += offsetBytes;
        u32 size = ReadBE32(book->idxData + off);
        off += 4;
        // the format guarantees every offset is within the .dict file
        if (offset >= (u64)book->dictSize || size > book->dictSize - offset) {
            logf("DictLookup: entry '%.*s' of '%s' has offset %llu size %u, outside the .dict (%lld bytes)\n",
                 wordLen, book->idxData + wordStart, book->bookName, (unsigned long long)offset, size,
                 book->dictSize);
            continue; // a bad entry is skipped, not fatal
        }
        if (size > kMaxArticleSize) {
            continue; // same: over the article cap, drop the entry
        }
        IdxEntry e;
        e.word = book->idxData + wordStart;
        e.wordLen = wordLen;
        e.offset = offset;
        e.size = size;
        VecAppend(book->entries, e);
    }
    return len(book->entries) > 0;
}

// --- per-dictionary loading --------------------------------------------------

static void CloseBook(DictBook* book) {
    if (book && book->dictFile != file::kInvalidFileHandle) {
        file::Close(book->dictFile);
        book->dictFile = file::kInvalidFileHandle;
    }
}

static bool LoadOneDict(Str ifoPath) {
    TempStr basePath = path::GetPathNoExtTemp(ifoPath);
    if (str::EndsWithI(basePath, StrL(".syn"))) {
        // <base>.syn.ifo is not a thing, but be safe: a .syn companion should
        // not make us look for .syn.idx
        return false;
    }

    i64 ifoSize = file::GetSize(ifoPath);
    if (ifoSize <= 0 || ifoSize > kMaxIfoSize) {
        SetErrorTemp(fmt("'%s' is not a usable .ifo (%lld bytes)", ifoPath, ifoSize));
        return false;
    }
    Str ifoData = file::ReadFileWithArena(ifoPath, GetTempArena());
    if (len(ifoData) == 0) {
        SetErrorTemp(fmt("failed to read '%s'", ifoPath));
        return false;
    }
    IfoInfo ifo;
    if (!ParseIfo(ifoData, ifo)) {
        SetErrorTemp(fmt("'%s' is missing version/bookname/idxfilesize, has an unknown version or an unknown "
                         "sametypesequence",
                         ifoPath));
        return false;
    }

    TempStr idxPath = path::JoinTemp(basePath, StrL(".idx"));
    // StarDict names the files <base>.idx and <base>.dict where <base> is the
    // .ifo's name without extension; some dictionaries ship "x.dict.dz" for
    // "x.dict", never a bare "x.dz"
    i64 idxSize = file::GetSize(idxPath);
    if (idxSize <= 0 || idxSize > kMaxIdxFileSize) {
        SetErrorTemp(fmt("'%s' is missing or too big (%lld bytes, cap %lld)", idxPath, idxSize, kMaxIdxFileSize));
        return false;
    }
    // the .ifo's idxfilesize must equal the real file size, or the index is
    // from another revision and parsing it is unsafe
    if (ifo.idxFileSize != idxSize) {
        SetErrorTemp(fmt("'%s': idxfilesize %lld does not match the real size %lld", idxPath, ifo.idxFileSize,
                         idxSize));
        return false;
    }

    // the article source: prefer an uncompressed .dict (seek per lookup), fall
    // back to .dict.dz (decompress to memory)
    TempStr dictPath = path::JoinTemp(basePath, StrL(".dict"));
    TempStr dzPath = path::JoinTemp(basePath, StrL(".dict.dz"));
    i64 dictSize = file::GetSize(dictPath);
    bool isDz = false;
    if (dictSize <= 0) {
        dictSize = file::GetSize(dzPath);
        isDz = dictSize > 0;
    }
    if (dictSize <= 0 || dictSize > kMaxDictFileSize) {
        SetErrorTemp(fmt("no .dict or .dict.dz for '%s' (or it is over the %lld byte cap)", basePath,
                         kMaxDictFileSize));
        return false;
    }

    DictBook* book = new DictBook();
    book->bookName = str::Dup(GetPermArena(), ifo.bookName);
    book->stss = str::Dup(GetPermArena(), ifo.sametypesequence);

    bool ok = false;
    auto cleanup = [&]() {
        CloseBook(book);
        delete book;
    };

    if (isDz) {
        Str dz = DecompressDictDz(dzPath, dictSize);
        if (len(dz) == 0) {
            cleanup();
            return false; // DecompressDictDz set the error
        }
        book->dzData = (const u8*)dz.s;
        book->dictSize = len(dz);
    } else {
        book->dictFile = file::OpenReadOnly(dictPath);
        if (book->dictFile == file::kInvalidFileHandle) {
            SetErrorTemp(fmt("failed to open '%s'", dictPath));
            cleanup();
            return false;
        }
        book->dictSize = dictSize;
    }

    // load and parse the .idx
    auto idxData = AllocArray<u8>(GetPermArena(), (int)idxSize);
    if (!idxData) {
        SetErrorTemp(fmt("out of memory for '%s' (%lld bytes)", idxPath, idxSize));
        cleanup();
        return false;
    }
    if (!file::ReadN(idxPath, idxData, (size_t)idxSize)) {
        SetErrorTemp(fmt("failed to read '%s'", idxPath));
        cleanup();
        return false;
    }
    book->idxData = idxData;
    book->idxLen = (int)idxSize;
    if (!ParseIdx(book, ifo)) {
        SetErrorTemp(fmt("'%s' has a truncated or corrupt index", idxPath));
        cleanup();
        return false;
    }
    if (ifo.wordCount >= 0 && (i64)len(book->entries) != ifo.wordCount) {
        // tolerated (some dictionaries are sloppy) but loud
        logf("DictLookup: '%s' declares %lld words, index has %d\n", ifoPath, ifo.wordCount, len(book->entries));
    }
    VecSort(book->entries, CmpIdxEntry);

    VecAppend(gBooks, book);
    Str articlesIn = isDz ? StrL("memory (.dict.dz)") : dictPath;
    logf("DictLookup: loaded '%s' (%s), %d words, articles in %s\n", book->bookName, ifoPath,
         len(book->entries), articlesIn);
    ok = true;
    return ok;
}

// --- lookup ------------------------------------------------------------------

// binary search for the first entry whose folded word is >= query
static int FindFirstGE(DictBook* book, const u8* query, int queryLen) {
    int lo = 0;
    int hi = len(book->entries);
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        IdxEntry& e = book->entries[mid];
        if (CmpWords(e.word, e.wordLen, query, queryLen) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static void AppendMatches(DictBook* book, IfoInfo& ifo, const u8* query, int queryLen, Vec<DictEntry>& out) {
    int i = FindFirstGE(book, query, queryLen);
    // the table is sorted, so every equal-fold word is a consecutive run
    while (i < len(book->entries)) {
        IdxEntry& e = book->entries[i];
        if (CmpWords(e.word, e.wordLen, query, queryLen) != 0) {
            break;
        }
        Str def = ReadArticle(book, ifo, e);
        if (len(def) > 0) {
            DictEntry de;
            de.dictName = book->bookName;
            de.definition = def;
            VecAppend(out, de);
        }
        i++;
    }
}

// the lookup candidates: the word as-is, then light de-inflections. Folded
// compares make the exact case-insensitive match, so the lowercased form is
// only needed as a stem base. The strings are allocated in a (perma or temp
// arena) and live as long as the caller needs them
static void BuildCandidates(Str wordIn, Vec<Str>& out, Arena* a) {
    Str word = wordIn;
    str::TrimWSInPlace(word, str::TrimOpt::Both);
    if (len(word) == 0) {
        return;
    }
    VecAppend(out, str::Dup(a, word));
    Str lower = str::ToLowerInPlace(str::DupTemp(word));
    VecAppend(out, str::Dup(a, lower));
    // 2..: strip possessive 's and the common inflections, always from the
    // lowercased form. A stem shorter than 3 characters is not worth a lookup
    struct Suffix {
        const char* s;
        int len;
    };
    Suffix suffixes[] = {
        {"'s", 2}, {"s", 1}, {"es", 2}, {"ed", 2}, {"ing", 3},
    };
    for (Suffix& suf : suffixes) {
        if (len(lower) - suf.len < 3) {
            continue;
        }
        if (str::EndsWith(lower, Str(suf.s, suf.len))) {
            // "es"/"ed"/"ing" also drop a doubled trailing consonant
            // (stopped -> stop,studied->studi is not handled: needs more rules)
            int stemLen = len(lower) - suf.len;
            if (suf.len > 1 && stemLen >= 2 && lower.s[stemLen - 1] == lower.s[stemLen - 2] &&
                str::IsAlNum(lower.s[stemLen - 1])) {
                stemLen--;
            }
            VecAppend(out, str::Dup(a, Str(lower.s, stemLen)));
        }
    }
}

bool DictLookupWord(const char* wordIn, Vec<DictEntry>& out) {
    if (!wordIn || !gLoaded) {
        return false;
    }
    Str word(wordIn);
    str::TrimWSInPlace(word, str::TrimOpt::Both);
    if (len(word) == 0 || len(word) > 256) {
        return false;
    }

    // the candidates and their per-book IfoInfo scratch live in the temp
    // arena; the results handed out are perma
    AutoArenaSavepoint sp;
    Arena* a = GetTempArena();
    Vec<Str> candidates;
    BuildCandidates(word, candidates, a);

    bool found = false;
    for (DictBook* book : gBooks) {
        IfoInfo ifoScratch; // DecodeArticle only reads bookName-independent
                            // fields; we keep a per-call copy because books
                            // don't store their IfoInfo
        // the article decoder needs version/idxoffsetbits only for parsing;
        // what it actually uses is sametypesequence, which we must keep per
        // book. Rebuild it from the stored perma bookname is not possible, so
        // LoadOneDict keeps the sametypesequence in the book (see below)
        ifoScratch.sametypesequence = book->stss;
        for (Str& c : candidates) {
            if (len(c) == 0) {
                continue;
            }
            int before = len(out);
            AppendMatches(book, ifoScratch, (const u8*)c.s, len(c), out);
            if (len(out) > before) {
                found = true;
                break; // one hit per book is enough
            }
        }
    }
    return found;
}

// --- the entry points --------------------------------------------------------

int DictLoadDictionaries(const char* dirPath) {
    AutoUnlockMutex lock(&gDictLoadMutex);
    if (gLoaded) {
        return len(gBooks); // load once; a second call is a no-op
    }
    gLoaded = true; // set before parsing: a failure still counts as an attempt

    if (!dirPath) {
        SetErrorTemp(StrL("no dictionary directory given"));
        return 0;
    }
    Str dir(dirPath);
    if (!dir::Exists(dir)) {
        SetErrorTemp(fmt("dictionary directory '%s' does not exist", dir));
        return 0;
    }

    // collect the .ifo paths first: loading reads idx/dict of the same base,
    // and iterating while mutating gBooks would confuse DirIter's caller
    Vec<TempStr> ifoPaths;
    {
        AutoArenaSavepoint sp;
        DirIter di{dir};
        di.includeFiles = true;
        di.includeDirs = false;
        for (DirIterEntry* e : di) {
            if (e->isDir) {
                continue;
            }
            if (!str::EndsWithI(e->name, StrL(".ifo"))) {
                continue;
            }
            VecAppend(ifoPaths, str::DupTemp(e->filePath));
        }
    }

    int nLoaded = 0;
    for (TempStr p : ifoPaths) {
        if (LoadOneDict(p)) {
            nLoaded++;
        }
    }
    if (nLoaded == 0 && len(ifoPaths) > 0) {
        // SetErrorTemp was called by the last failing dictionary
        SetErrorTemp(fmt("none of the %d dictionaries in '%s' could be loaded; last error: %s", len(ifoPaths),
                         dir, Str(gLastError ? gLastError : "unknown")));
    }
    return nLoaded;
}

const char* DictLastError() {
    return gLastError ? gLastError : "";
}
