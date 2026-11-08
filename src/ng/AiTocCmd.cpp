/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// "Generate AI Table Of Contents" (CmdAiTocBuildToc): extracts the text of the
// document's first pages, sends it to an OpenAI-compatible chat API
// (configured in <AppDataDir>/ai-toc.json) and displays the returned outline
// in the bookmarks sidebar. See ng/aitoc/AiToc.h for the data half.
//
// The generated outline replaces win->tocFilteredTree, the same slot the
// bookmark filter uses, so typing in the sidebar filter afterwards falls back
// to the document's own outline (if any).

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
#include "TableOfContents.h"
#include "Notifications.h"
#include "SumatraPDF.h"
#include "Translations.h"
#include "gui/AppShell.h"
#include "gui/Sidebar.h"
#include "ng/aitoc/AiToc.h"
#include "PlusCmd.h"

// how many leading pages to send to the model: enough to cover the printed
// TOC of typical books and reports without blowing the prompt budget (the
// module caps the total at 48k chars)
constexpr int kAiTocFirstPages = 20;

struct AiTocJob {
    MainWindow* win = nullptr;
    WindowTab* tab = nullptr;      // to detect that the tab is still current
    DocController* ctrl = nullptr; // ...and still the same document
    EngineBase* engine = nullptr;  // clone, released by the worker
    AiTocConfig cfg;               // heap strings, freed here
    bool ok = false;
    Str error;           // heap, freed here
    Vec<AiTocItem> items; // heap titles, freed here
};

// frees everything the job owns. Safe on any thread: the strings are heap
// allocations, the engine clone its own ref
static void FreeAiTocJob(AiTocJob* job) {
    if (job->engine) {
        job->engine->Release();
    }
    str::FreePtr(&job->cfg.baseUrl);
    str::FreePtr(&job->cfg.apiKey);
    str::FreePtr(&job->cfg.model);
    str::FreePtr(&job->error);
    for (AiTocItem& it : job->items) {
        str::Free(it.title);
    }
    VecReset(job->items);
    delete job;
}

// UI thread: turn the generated outline into a TocTree and show it
static void AiTocFinishOnUi(AiTocJob* job) {
    MainWindow* win = job->win;
    bool valid = IsMainWindowValidAndNotClosing(win) && IsWindowTabValid(job->tab) &&
                 IsCurrentTab(win, job->tab) && job->tab->ctrl == job->ctrl;
    if (!valid) {
        FreeAiTocJob(job);
        return;
    }
    if (!job->ok) {
        ShowPlainNotification(win, fmt("%s: %s", Tr("AI table of contents failed"), job->error));
        FreeAiTocJob(job);
        return;
    }

    // make sure the sidebar's toc state is initialized for this window
    if (!win->tocLoaded) {
        LoadTocTree(win);
    }
    // free the previous (filtered or AI) tree the way ApplyTocFilter does:
    // nothing may still point into it when it is deleted
    SidebarSetTocSelection(win, nullptr);
    VecReset(win->tocMatchingItems);
    delete win->tocFilteredTree;
    win->tocFilteredTree = nullptr;

    // the root itself is invisible, the same shape every engine uses
    TocItem* root = AllocTocItem(nullptr, {}, 0);
    root->isOpenDefault = true;
    TocItem* prev = nullptr;
    for (AiTocItem& it : job->items) {
        TocItem* ti = AllocTocItem(nullptr, it.title, it.pageNo);
        ti->isOpenDefault = true;
        ti->parent = root;
        if (!root->child) {
            root->child = ti;
        } else {
            prev->next = ti;
        }
        prev = ti;
    }
    win->tocFilteredTree = new TocTree(root);
    AppShellInvalidate(win);
    ShowPlainNotification(win, Tr("AI table of contents generated"));
    FreeAiTocJob(job);
}

// background thread: never touches UI state, only the engine clone and curl
static void AiTocWorker(AiTocJob* job) {
    Vec<int> pages;
    int maxPage = job->engine->PageCount();
    for (int i = 1; i <= kAiTocFirstPages && i <= maxPage; i++) {
        VecAppend(pages, i);
    }
    job->ok = AiTocGenerate(job->engine, job->cfg, pages, job->items, job->error);
    uitask::Post(MkFunc0<AiTocJob>(AiTocFinishOnUi, job), "AiTocFinish");
}

void AiTocBuildTocCommand(MainWindow* win) {
    WindowTab* tab = win->CurrentTab();
    DisplayModel* dm = win->AsFixed(); // the current tab, if it holds a fixed-page document
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine) {
        ShowPlainNotification(win, Tr("AI table of contents is only available for fixed-page documents"));
        return;
    }
    AiTocConfig cfg;
    if (!AiTocLoadConfig(cfg)) {
        ShowPlainNotification(win, Tr("Create ai-toc.json with baseUrl, apiKey and model in the app data folder"));
        return;
    }

    auto* job = new AiTocJob;
    job->win = win;
    job->tab = tab;
    job->ctrl = tab->ctrl;
    job->cfg = cfg;
    // the engine has thread affinity: extract the text of the candidate pages
    // from a clone on the worker thread instead
    job->engine = engine->Clone();
    StartThread(MkFunc0<AiTocJob>(AiTocWorker, job), StrL("AiTocBuild"));
    ShowPlainNotification(win, Tr("Generating AI table of contents..."));
}
