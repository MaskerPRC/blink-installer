// Bridges NSIS's own install progress into the installer page.
//
// NSIS has no API for observing install progress. The only way to see it is to
// subclass the progress bar it draws on its (hidden) inner dialog and watch the
// PBM_ messages go past, which is what this does.
#pragma once

#include <windows.h>

namespace bk {
namespace nsis {

// Takes over the NSIS shell window and keeps it out of sight for the whole
// run. Call once, as soon as our own window exists.
//
// Hiding it only from the page functions is not enough: NSIS re-shows and
// re-sizes its window when it moves to the instfiles page, so the stock
// "Completed" dialog pops up next to the real UI partway through the install.
void HideHostWindow(HWND parent);

// Subclasses the NSIS progress bar and detail list.
// Emits "progress", "log" and "finish" events to the page.
void BindProgress(HWND parent);

// Counts how many times the page has been shown. The auto-advance below uses
// it to tell whether NSIS actually moved on.
void NotePageShown();

// Restores the original window procedures. Safe to call when not bound.
void UnbindProgress();

}  // namespace nsis
}  // namespace bk
