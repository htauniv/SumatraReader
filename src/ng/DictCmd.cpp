/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// "Lookup Selected Word" (CmdDictLookupSelection): looks up the word the user
// has selected in the offline StarDict dictionaries placed in
// <AppDataDir>/dict and shows the articles in the text viewer window. The
// StarDict parsing lives in ng/dict/DictLookup.cpp (see DictLookup.h for the
// data model and threading notes).
//
// Dictionary loading happens on first use, in the background (a large
// .dict.dz can take a moment to decompress); results arrive via uitask.

#include "gui/GpuiBridge.h"
#include "base/File.h"
#include "base/UITask.h"

#include "gui/UIModels.h"

#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "DisplayModel.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Selection.h"
#include "Notifications.h"
#include "AppTools.h"
#include "SumatraPDF.h"
#include "Translations.h"
#include "TextViewWnd.h"
#include "ng/dict/DictLookup.h"
#include "PlusCmd.h"

struct DictJob {
    MainWindow* win = nullptr;
    Str word;        // heap, freed by the finisher
    bool ok = false;
    int dictCount = 0;
    Vec<DictEntry> entries; // strings are perma-arena, never freed
};

// UI thread: display the articles or explain why there are none
static void DictFinishOnUi(DictJob* job) {
    MainWindow* win = job->win;
    if (!IsMainWindowValidAndNotClosing(win)) {
        str::FreePtr(&job->word);
        delete job;
        return;
    }
    if (!job->ok) {
        if (job->dictCount == 0) {
            // no (usable) dictionaries: point the user at the folder
            const char* err = DictLastError();
            if (err && *err) {
                ShowPlainNotification(win, fmt("%s: %s", Tr("No dictionaries found in the dict folder"), Str(err)));
            } else {
                ShowPlainNotification(win, Tr("No dictionaries found in the dict folder"));
            }
        } else {
            ShowPlainNotification(win, Tr("No dictionary entry found"));
        }
        str::FreePtr(&job->word);
        delete job;
        return;
    }

    str::Builder text;
    bool first = true;
    for (DictEntry& e : job->entries) {
        if (!first) {
            text.Append(StrL("\n\n"));
        }
        first = false;
        text.Append(fmt("%s\n", e.dictName));
        text.Append(StrL("----------------------------------------\n"));
        text.Append(e.definition);
    }
    TempStr title = fmt(Tr("Dictionary: %s").s, job->word);
    ShowTextInWindow(win, title, ToStr(text));
    str::FreePtr(&job->word);
    delete job;
}

// background thread: load the dictionaries once, then look the word up
static void DictWorker(DictJob* job) {
    TempStr dir = GetPathInAppDataDirTemp(StrL("dict"));
    if (len(dir) > 0) {
        job->dictCount = DictLoadDictionaries(dir.s);
    }
    if (job->dictCount > 0) {
        job->ok = DictLookupWord(job->word.s, job->entries);
    }
    uitask::Post(MkFunc0<DictJob>(DictFinishOnUi, job), "DictFinish");
}

// the lookup target: the selected text trimmed to its first word. StarDict
// indexes single words; a multi-word selection is searched as its first word
void DictLookupSelectionCommand(MainWindow* win) {
    WindowTab* tab = win->CurrentTab();
    if (!tab) {
        return;
    }
    bool isTextOnly = false;
    TempStr sel = GetSelectedTextTemp(tab, StrL(" "), isTextOnly);
    Str word = str::DupTemp(sel);
    str::TrimWSInPlace(word, str::TrimOpt::Both);
    // cut at the first whitespace run
    int cut = len(word);
    for (int i = 0; i < len(word); i++) {
        char c = word.s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            cut = i;
            break;
        }
    }
    if (cut == 0) {
        ShowPlainNotification(win, Tr("Select a word first"));
        return;
    }
    word = Str(word.s, cut);

    auto* job = new DictJob;
    job->win = win;
    job->word = str::Dup(word); // survives past the temp arena
    StartThread(MkFunc0<DictJob>(DictWorker, job), StrL("DictLookup"));
}
