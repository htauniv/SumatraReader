/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// "OCR Current Page" (CmdOcrPage): renders the current page at 150 DPI,
// recognizes its text with the macOS Vision framework on a background thread
// and copies the result to the clipboard. Integration notes in
// integrate/ocr.md (repo root).

#include "gui/GpuiBridge.h"
#include "base/File.h"
#include "base/Pixmap.h"
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
#include "Notifications.h"
#include "SumatraPDF.h"
#include "Translations.h"
#include "ng/ocr/Ocr.h"
#include "PlusCmd.h"

struct OcrJob {
    MainWindow* win = nullptr;
    Pixmap* px = nullptr; // command thread's BGRA8 page render; owned by the job
    bool ok = false;
    Str text; // recognized text, heap-owned, freed on the UI thread
};

// UI thread: report the outcome and free the job
static void OcrFinishOnUi(OcrJob* job) {
    MainWindow* win = job->win;
    if (!IsMainWindowValidAndNotClosing(win)) {
        str::FreePtr(&job->text);
        delete job;
        return;
    }
    if (!job->ok) {
        ShowPlainNotification(win, Tr("OCR failed"));
    } else if (len(job->text) == 0) {
        ShowPlainNotification(win, Tr("No text recognized"));
    } else {
        CopyTextToClipboard(win, job->text);
        ShowPlainNotification(win, Tr("Recognized text copied to clipboard"));
    }
    str::FreePtr(&job->text);
    delete job;
}

// background thread: OCR only (OcrPageText serializes internally, so repeated
// triggers queue up instead of racing)
static void OcrPageWorker(OcrJob* job) {
    str::Builder text;
    job->ok = OcrPageText(job->px->data, job->px->width, job->px->height, 4, text);
    FreePixmap(job->px);
    job->px = nullptr;
    if (job->ok) {
        job->text = str::Dup(ToStr(text));
    }
    uitask::Post(MkFunc0<OcrJob>(OcrFinishOnUi, job), "OcrFinish");
}

void OcrCurrentPage(MainWindow* win) {
    DisplayModel* dm = win ? win->AsFixed() : nullptr;
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine) {
        ShowPlainNotification(win, Tr("OCR failed"));
        return;
    }
    int pageNo = dm->CurrentPageNo();
    RectF rect = engine->PageMediabox(pageNo);
    constexpr float kOcrDpi = 150.f;
    RenderPageArgs args(pageNo, kOcrDpi / 72.f, dm->GetRotation(), &rect, RenderTarget::Export);
    Pixmap* px = engine->RenderPage(args);
    if (!px) {
        ShowPlainNotification(win, Tr("OCR failed"));
        return;
    }
    // takes ownership of px: converts in place and frees the original on failure
    px = PixmapToBgra(px);
    if (!px) {
        ShowPlainNotification(win, Tr("OCR failed"));
        return;
    }
    auto* job = new OcrJob;
    job->win = win;
    job->px = px; // ownership moves to the worker
    StartThread(MkFunc0<OcrJob>(OcrPageWorker, job), StrL("OcrCurrentPage"));
}
