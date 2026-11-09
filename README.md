# SumatraReader

**A small, offline-first ebook and document reader for macOS — PDF is only one of the formats it opens, the Chinese UI actually works, and Intel and Apple Silicon share one binary.**

**Modified from [SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf)** — specifically the in-progress cross-platform `src/ng/` rewrite. This is an unofficial community fork, not affiliated with or endorsed by the SumatraPDF authors.

[简体中文说明](README.zh-CN.md) · [Upstream](https://github.com/sumatrapdfreader/sumatrapdf) · [Build notes](README-mac.md) · License: GPLv3

## Why "Reader", not "PDF"

The upstream name undersells the program. SumatraPDF has long been a general-purpose ebook and document reader: PDF is one format among many. This fork keeps that breadth, so the rename to **SumatraReader** makes the name match what the app actually does instead of implying a single-format PDF viewer.

These are not just Finder file associations — the `ng` build compiles the corresponding engines into the application (`src/ng/EngineCreate.cpp`, `src/base/GuessFileType.h`):

| Family | Formats with a compiled engine |
| --- | --- |
| Fixed-layout documents | PDF, XPS / OXPS, DjVu, PostScript, DVI, CAJ (CNKI), Markdown, Office Open XML (docx / xlsx / pptx, via MuPDF) |
| Ebooks and reflowable text | EPUB, MOBI / PalmDoc, FB2 / FB2Z, CHM, LIT, RTF, TXT, HTML |
| Comics and image books | CBZ, CBR, CB7, CBT, plus standalone images (PNG, JPEG, GIF, TIFF, BMP, WebP, JPEG XL, JP2, HEIC, AVIF, SVG) |

Two caveats, stated explicitly:

- The table describes **code-level support**: engines compiled into the binary plus the file-type table. Per-format opening tests have not been run for every entry; PDF and EPUB are the formats verified against real books so far.
- OFD is registered with Finder and has a code path into the MuPDF engine (`FileType::Ofd`), but its rendering on macOS has **not** been verified. It is therefore not claimed as a supported format.

Upstream source filenames such as `SumatraPDF.cpp` keep their old names so the tree stays mergeable with upstream; only user-visible identity was renamed.

## Why this fork exists

SumatraPDF is a Windows application. The `src/ng/` rewrite is intended to be cross-platform, but no macOS build is published. A clean `ng` build on macOS also exposes three problems that make the Chinese UI look broken:

1. `cmd/trans-dl.ts` requires `TRANS_UPLOAD_SECRET`. Without it the generated `.work/translations.txt` is empty, is embedded anyway, and `Translations.cpp` silently falls back to English — switching the UI to 中文 does nothing, with no warning at build or run time.
2. `DetectUserLang()` on non-Windows passes `zh_CN` / `zh-Hans-CN` into a language table where Simplified Chinese is named `cn`, so a Chinese macOS starts in English.
3. Even with the right code, a process launched from Finder or the Dock has no `$LANG` at all (launchd does not inherit a login shell's environment), so an environment-only check always reads nothing.

SumatraReader fixes all three. Issues 2 and 3 were [reported upstream](https://github.com/sumatrapdfreader/sumatrapdf/issues/6513) and fixed by the maintainer in `3c9b393` (the issue is closed); issue 1 is still open upstream — a missing secret still yields a silently empty translation table. This fork instead ships its own translation data and has `verify_build.py` assert that the table is non-empty and has the expected key count, so an empty one cannot pass the build.

The scope stays deliberately narrow: macOS 13–26, one universal binary for Intel and Apple Silicon, English + 简体中文, plus a few reader features that matter for Chinese books.

## What changed

| Area | What this fork does |
| --- | --- |
| Product identity | Renamed to **SumatraReader** everywhere a user can see it: the `.app`, macOS app menu, window titles, About box, settings file, app data folder and PDF `CreatorApp` metadata. Kept "based on SumatraPDF" attribution in the UI and docs. |
| Chinese UI | Rebuilds `translations.txt` (1,208 en→cn pairs) from the public apptranslator history, maps `zh*` locales to `cn`, ships only `SUMATRA_LANGS=en,cn`, and guards new strings with a runtime translation test. Renamed UI strings reuse upstream translations automatically. |
| Language on first launch | Reads `CFLocaleCopyPreferredLanguages()` first, then falls back to `LC_ALL` / `LC_MESSAGES` / `LANG`, and only then to `en`. Each locale goes through an alias table and trailing-part stripping: `zh-Hans-CN` → `cn`, `en_GB.UTF-8` → `en`, while `fr-FR` / `C` / `POSIX` count as "no opinion" rather than being guessed as English. |
| Two CPUs, one bundle | `-arch universal` builds x86_64 and arm64 slices separately and `lipo`s them into a single `.app`. tesseract's x86 SIMD kernels and dav1d's CPU detection are dispatched per architecture rather than assumed from the host. |
| macOS target | Deployment target `13.0` (`minos 13.0`) for the binary and embedded data, ad-hoc code signature, and Finder document-type registration including OFD. |
| OCR | Document ▸ OCR current page. On-device Apple Vision, Chinese + English, result copied to the clipboard. |
| AI table of contents | Generates bookmarks for PDFs that have none; reads `ai-toc.json` (`baseUrl` / `apiKey` / `model`) from the app data dir. Requires an LLM API key. |
| Offline dictionary | Look up the selected word in StarDict dictionaries (`.ifo` / `.idx` / `.dict(.dz)`) placed in `<AppDataDir>/dict`. |
| Offline manual | A 5-page bilingual manual embedded in the app, reachable from Help. Sources live in `docs/manual/`, so a fresh clone needs no external website checkout. |

## One name, one source of truth

The rename is in the code, not just in this README:

- `cmd/helper/ng-app-config.ts` defines `APP_NAME = "SumatraReader"` and `BUNDLE_ID = "org.sumatrareader.SumatraReader"` for the build targets, executable, bundle and Info.plist.
- `src/ng/Version.h` defines `kAppName` for the C++ side (app menu, window titles, About box, app data dir, settings file name).
- `tools/verify_build.py` asserts the two stay equal and checks the built `Info.plist` against them.

Result: `out/mac/rel/SumatraReader.app`, bundle id `org.sumatrareader.SumatraReader`, app data in `~/Library/Application Support/SumatraReader/`, settings in `SumatraReader-settings.txt`. On upgrade from a pre-rename build, the old `SumatraPDF` data directory is reused so settings, favorites and history are not lost.

Upstream names are deliberately preserved where they are not user-facing: copyright headers, upstream URLs, the apptranslator application id, and source filenames such as `SumatraPDF.cpp` / `SumatraPDF.icns`. This keeps the tree mergeable with upstream. The original upstream README is archived as `UPSTREAM-README.md`.

## Status

Automated verification passes: all 39 static checks in `tools/verify_build.py` (both architectures, `minos 13.0`, bundle identity, signature, 1,208 translation keys matching the committed snapshot, 41 embedded manual entries plus byte-identical staging against `docs/manual/`), the translation runtime test (17 locale cases), Chinese strings for all newly added menu items, real books opened without crashes, and an end-to-end Vision OCR test that recognised 37 lines of Chinese on a real page.

Still needs human verification: GUI menu clicks end to end, AI table of contents with a real API key, dictionary lookup with a real StarDict dictionary, and the arm64 slice running on real Apple Silicon — this machine is Intel, so arm64 is verified only as far as `lipo -archs`, matching `minos` on both slices, and the full static check suite.

Not supported yet: macOS 27, languages other than en/cn, Windows. These are candidates for later work, not promises.

## Build from source

Requirements: macOS, Xcode Command Line Tools, [bun](https://bun.sh).

```sh
SUMATRA_LANGS=en,cn bun cmd/ng-build.ts -mac -rel    # universal (default) → out/mac/rel/SumatraReader.app
bun cmd/ng-build.ts -mac -rel -arch arm64            # Apple Silicon slice only
bun cmd/ng-build.ts -mac -rel -clean                 # full rebuild
```

The build invokes clang++ directly (no generated Xcode project). Vendored dependencies in `ext/` are compiled on the first run and reused per architecture afterwards. Build identifiers and the history of local builds are managed automatically; see `README-mac.md`.

Checks:

```sh
python3 tools/verify_build.py --archs x86_64,arm64
bash tools/trans_runtime_test.sh
bash tools/ocr_endtoend_test.sh path/to/book.pdf
```

## Acknowledgments

This fork stands on other people's work. Thanks to:

- **Krzysztof J. Kowalczyk and every SumatraPDF contributor** (full list in `AUTHORS`) — for years of work on the reader and for the `src/ng/` rewrite this project builds on.
- **The volunteer translators at [apptranslator.org](https://www.apptranslator.org/)** — the 1,208 English→Chinese strings shipped here come from their community translation history.
- **The MuPDF / Artifex team and the authors of every library in `ext/`** — they are what makes PDF, EPUB, DjVu, comics and the rest renderable at all.
- **[SumatraPDF plus](https://github.com/dengxibo/sumatrapdf-plus)** — for the feature ideas behind OCR, AI table of contents and dictionary lookup, reimplemented here for the `ng` / macOS stack.
- **Apple's Vision framework** — for on-device, offline Chinese text recognition, with no data leaving the Mac.
- **Everyone who filed an issue, sent feedback or lent a real book for testing** — the report that "switching to Chinese does nothing" is where this fork started.
- **An AI coding assistant (WorkBuddy Agent)** — most of the macOS porting work here was done in pair with it: build debugging, the Chinese UI, the product rename, the verification scripts and these docs. Tools do not sign commits, but the help was real; the direction and every acceptance check were the maintainer's, and so are the remaining bugs.

And thanks to upstream for releasing the code under GPLv3, which is what makes a fork like this possible in the first place. If someone should be named here and is missing, please open an issue.

## Credits and license

- **Modified from [SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf)** by the SumatraPDF authors and contributors (see `AUTHORS`), GPLv3 (`COPYING`); some parts under BSD (`COPYING.BSD`).
- Feature ideas selectively referenced from [SumatraPDF plus](https://github.com/dengxibo/sumatrapdf-plus). The OCR, AI TOC and dictionary code in this fork was written against this fork's `src/ng/` + macOS stack.
- UI strings come from the [apptranslator.org](https://www.apptranslator.org/) community translation project.
- MuPDF and the other vendored libraries in `ext/` keep their respective licenses.

This fork is distributed under **GPLv3**, the same license as upstream. All modifications are contained in this repository. SumatraReader is not affiliated with, endorsed by, or a replacement for the official SumatraPDF.
