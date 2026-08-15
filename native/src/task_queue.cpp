#include "task_queue.h"

#include <deque>
#include <memory>
#include <mutex>

#include "log.h"

namespace bk {
namespace {

constexpr UINT kMsgDrain = WM_APP + 0x51;
constexpr wchar_t kWindowClass[] = L"BlinkkitTaskQueue";

std::mutex g_mutex;
std::deque<std::function<void()>> g_tasks;

// Shared between the caller blocked in Send() and the UI thread running the
// task, so a task that arrives after the caller timed out still has valid
// state to write into.
struct SyncState {
  HANDLE done = nullptr;
  ~SyncState() {
    if (done) ::CloseHandle(done);
  }
};

}  // namespace

TaskQueue& TaskQueue::Get() {
  static TaskQueue instance;
  return instance;
}

LRESULT CALLBACK TaskQueue::WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam) {
  if (msg == kMsgDrain) {
    TaskQueue::Get().Drain();
    return 0;
  }
  return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool TaskQueue::InitOnUiThread() {
  if (window_) return true;

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &TaskQueue::WndProc;
  wc.hInstance = ::GetModuleHandleW(nullptr);
  wc.lpszClassName = kWindowClass;
  // Ignore "already registered" so a second installer page in the same process
  // does not fail here.
  ::RegisterClassExW(&wc);

  window_ = ::CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                              nullptr, wc.hInstance, nullptr);
  if (!window_) {
    BK_LOGF(Error, "TaskQueue: CreateWindowEx failed: %lu", ::GetLastError());
    return false;
  }

  ui_thread_id_ = ::GetCurrentThreadId();
  return true;
}

void TaskQueue::Shutdown() {
  if (window_) {
    ::DestroyWindow(window_);
    window_ = nullptr;
  }
  ui_thread_id_ = 0;

  std::lock_guard<std::mutex> lock(g_mutex);
  g_tasks.clear();
}

bool TaskQueue::IsUiThread() const {
  return ui_thread_id_ != 0 && ::GetCurrentThreadId() == ui_thread_id_;
}

void TaskQueue::Drain() {
  // Take the whole batch under the lock, then run outside it: a task is allowed
  // to enqueue more work without deadlocking.
  std::deque<std::function<void()>> batch;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    batch.swap(g_tasks);
  }
  for (auto& task : batch) {
    task();
  }
}

void TaskQueue::Post(std::function<void()> task) {
  if (!window_) {
    BK_LOG(Warn, "TaskQueue::Post before init — task dropped");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_tasks.push_back(std::move(task));
  }
  ::PostMessageW(window_, kMsgDrain, 0, 0);
}

bool TaskQueue::Send(std::function<void()> task, DWORD timeout_ms) {
  if (IsUiThread()) {
    task();
    return true;
  }
  if (!window_) {
    BK_LOG(Warn, "TaskQueue::Send before init — task dropped");
    return false;
  }

  auto state = std::make_shared<SyncState>();
  state->done = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!state->done) {
    BK_LOGF(Error, "TaskQueue: CreateEvent failed: %lu", ::GetLastError());
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_tasks.push_back([task = std::move(task), state]() {
      task();
      ::SetEvent(state->done);
    });
  }
  ::PostMessageW(window_, kMsgDrain, 0, 0);

  const DWORD wait = ::WaitForSingleObject(state->done, timeout_ms);
  if (wait == WAIT_OBJECT_0) return true;

  BK_LOGF(Error,
          "TaskQueue::Send timed out after %lu ms — UI thread is not pumping "
          "messages; abandoning task",
          timeout_ms);
  return false;
}

}  // namespace bk
