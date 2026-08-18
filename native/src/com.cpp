#include "com.h"

#include <windows.h>
#include <objbase.h>

#include "log.h"

namespace bk {
namespace {

// Whether this translation unit's CoInitializeEx is the one holding a
// reference, and so whether it owes a CoUninitialize.
bool g_owns_com = false;
bool g_attempted = false;

}  // namespace

void InitUiThreadCom() {
  if (g_attempted) return;
  g_attempted = true;

  // DISABLE_OLE1DDE: no DDE broadcast on startup, which nothing here wants and
  // which can stall while other windows answer it.
  const HRESULT hr =
      ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  if (hr == S_OK) {
    g_owns_com = true;
    BK_LOG(Info, "COM initialized (STA)");
    return;
  }
  if (hr == S_FALSE) {
    // Already initialized on this thread and compatible; our call added a
    // reference, so we still owe the matching uninitialize.
    g_owns_com = true;
    BK_LOG(Info, "COM already initialized on this thread (STA)");
    return;
  }
  if (hr == RPC_E_CHANGED_MODE) {
    // Someone else made this thread multi-threaded. Not ours to change, and
    // no reference was taken, so there is nothing to release later. Shell
    // dialogs are unhappy in an MTA, so say so rather than let the folder
    // picker quietly degrade.
    BK_LOG(Warn,
           "COM on this thread is MTA, not STA; shell dialogs may fall back "
           "to their legacy form");
    return;
  }
  BK_LOGF(Warn, "CoInitializeEx failed (hr=0x%08lx)", static_cast<unsigned long>(hr));
}

void ShutdownUiThreadCom() {
  if (!g_owns_com) return;
  g_owns_com = false;
  ::CoUninitialize();
}

}  // namespace bk
