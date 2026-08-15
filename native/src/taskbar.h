// Taskbar button progress (the green fill behind the icon on the taskbar).
//
// Kept in one place so the single ITaskbarList3 instance is created once and
// released on shutdown. Creating it inside the progress handler means a COM
// activation on every tick of the progress bar.
#pragma once

#include <windows.h>

namespace bk {

// `percent` in [0,100]; pass -1 together with indeterminate=false to clear the
// indicator. Safe to call before the taskbar interface is available (pre-Win7),
// where it is a no-op.
void SetTaskbarProgress(HWND hwnd, int percent, bool indeterminate);

void ReleaseTaskbar();

}  // namespace bk
