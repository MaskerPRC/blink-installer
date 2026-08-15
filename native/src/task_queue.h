// Cross-thread marshaling onto the UI thread.
//
// Why this exists: miniblink (like every browser engine) may only be touched
// from the thread that created its window. NSIS, meanwhile, runs install
// Sections on a worker thread while the UI thread pumps messages — so any
// ability invoked from a Section has to hop threads before it can talk to JS.
//
// The code this replaces did that with `while (m_usingShareDataCache);` over a
// non-atomic bool: a busy-wait that both burned CPU and did not actually
// establish mutual exclusion. This is a proper queue drained by a message-only
// window on the UI thread.
#pragma once

#include <windows.h>

#include <functional>

namespace bk {

class TaskQueue {
 public:
  static TaskQueue& Get();

  // Must be called from the thread that will own the UI. Creates the
  // message-only window used to wake the queue.
  bool InitOnUiThread();
  void Shutdown();

  bool IsUiThread() const;

  // Fire-and-forget. Safe from any thread; returns immediately.
  void Post(std::function<void()> task);

  // Runs `task` on the UI thread and blocks until it finishes. Executes inline
  // when already on the UI thread, so it cannot self-deadlock.
  //
  // Returns false if the UI thread did not drain the queue within timeout_ms —
  // the task is abandoned rather than hanging the installer forever. The
  // shared state outlives the caller, so a late-running task is still safe.
  bool Send(std::function<void()> task, DWORD timeout_ms = 30000);

 private:
  TaskQueue() = default;
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  void Drain();

  HWND window_ = nullptr;
  DWORD ui_thread_id_ = 0;
};

}  // namespace bk
