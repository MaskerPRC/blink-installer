// COM apartment for the UI thread.
//
// Two things here need COM: the shell folder picker (IFileOpenDialog) and the
// taskbar progress indicator (ITaskbarList3). Neither used to initialize it,
// which did not fail loudly — it made the behaviour depend on whether some
// other component in the process happened to have initialized COM on this
// thread first. When something had, the folder picker was the modern explorer
// dialog; when nothing had, CoCreateInstance returned CO_E_NOTINITIALIZED and
// the picker silently fell back to the Windows 95 tree box. Same build, same
// machine, different answer depending on timing.
//
// Apartment-threaded because that is what shell dialogs require; they run a
// modal message loop and expect to own the thread while they do.
#pragma once

namespace bk {

// Idempotent. Safe to call when another component already initialized COM on
// this thread — that case is recorded and left alone rather than overridden.
void InitUiThreadCom();

// Undoes InitUiThreadCom, and only if this process was the one that performed
// it. Call once, at plugin unload.
void ShutdownUiThreadCom();

}  // namespace bk
