#include "taskbar.h"

#include <shobjidl.h>

#include "log.h"

namespace bk {
namespace {

ITaskbarList3* g_taskbar = nullptr;
bool g_tried = false;

ITaskbarList3* Taskbar() {
  if (g_tried) return g_taskbar;
  g_tried = true;

  // Absent before Windows 7. Failure here is expected and not worth warning
  // about: the installer simply has no taskbar progress.
  const HRESULT hr = ::CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL,
                                        IID_PPV_ARGS(&g_taskbar));
  if (FAILED(hr)) {
    BK_LOGF(Debug, "no ITaskbarList3 (0x%08lx); taskbar progress disabled",
            static_cast<unsigned long>(hr));
    g_taskbar = nullptr;
  }
  return g_taskbar;
}

}  // namespace

void SetTaskbarProgress(HWND hwnd, int percent, bool indeterminate) {
  ITaskbarList3* taskbar = Taskbar();
  if (!taskbar || !hwnd) return;

  if (indeterminate) {
    taskbar->SetProgressState(hwnd, TBPF_INDETERMINATE);
    return;
  }
  if (percent < 0) {
    taskbar->SetProgressState(hwnd, TBPF_NOPROGRESS);
    return;
  }
  if (percent > 100) percent = 100;
  taskbar->SetProgressState(hwnd, TBPF_NORMAL);
  taskbar->SetProgressValue(hwnd, static_cast<ULONGLONG>(percent), 100);
}

void ReleaseTaskbar() {
  if (g_taskbar) {
    g_taskbar->Release();
    g_taskbar = nullptr;
  }
  g_tried = false;
}

}  // namespace bk
