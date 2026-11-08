/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// Commands added by the mac "plus" work, implemented in OcrCmd.cpp,
// AiTocCmd.cpp and DictCmd.cpp. All three are invoked from ExecuteCmd
// (SumatraPDF.cpp) on the UI thread; they own their background threading.

struct MainWindow;

// renders the current page and OCRs it with the system Vision framework;
// the recognized text lands in the clipboard (CmdOcrPage)
void OcrCurrentPage(MainWindow* win);

// extracts text from the first pages of the document, asks an
// OpenAI-compatible chat API (configured via ai-toc.json) for an outline and
// shows it in the bookmarks sidebar (CmdAiTocBuildToc)
void AiTocBuildTocCommand(MainWindow* win);

// looks up the selected word in the offline StarDict dictionaries placed in
// <AppDataDir>/dict and shows the articles in the text viewer window
// (CmdDictLookupSelection)
void DictLookupSelectionCommand(MainWindow* win);
