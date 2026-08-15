#include "nsis/plugin_api.h"

#include <vector>

#include "strings.h"

namespace bk {
namespace nsis {

unsigned int g_stringsize = 0;
stack_t** g_stacktop = nullptr;
wchar_t* g_variables = nullptr;
extra_parameters* g_extra = nullptr;
HWND g_parent = nullptr;

bool PopString(std::wstring* out) {
  if (!g_stacktop || !*g_stacktop) return false;

  stack_t* item = *g_stacktop;
  // The buffer is g_stringsize wide chars but is not guaranteed to be
  // terminated at that boundary, so bound the read explicitly.
  const size_t capacity = g_stringsize ? g_stringsize : 1;
  size_t length = 0;
  while (length < capacity && item->text[length] != L'\0') ++length;
  out->assign(item->text, length);

  *g_stacktop = item->next;
  ::GlobalFree(reinterpret_cast<HGLOBAL>(item));
  return true;
}

std::string PopUtf8() {
  std::wstring value;
  if (!PopString(&value)) return std::string();
  return WideToUtf8(value);
}

void PushString(const std::wstring& value) {
  if (!g_stacktop) return;

  const size_t capacity = g_stringsize ? g_stringsize : 1;
  auto* item = static_cast<stack_t*>(::GlobalAlloc(
      GPTR, sizeof(stack_t) + capacity * sizeof(wchar_t)));
  if (!item) return;

  // Truncate rather than overflow: NSIS strings have a fixed width chosen at
  // compile time of the installer.
  const size_t count = value.size() < capacity - 1 ? value.size() : capacity - 1;
  ::memcpy(item->text, value.c_str(), count * sizeof(wchar_t));
  item->text[count] = L'\0';

  item->next = *g_stacktop;
  *g_stacktop = item;
}

void PushUtf8(const std::string& value) {
  PushString(Utf8ToWide(value));
}

std::wstring GetUserVariable(int index) {
  if (!g_variables || index < 0) return std::wstring();
  return std::wstring(g_variables + index * g_stringsize);
}

void SetUserVariable(int index, const std::wstring& value) {
  if (!g_variables || index < 0) return;
  wchar_t* slot = g_variables + index * g_stringsize;
  const size_t count =
      value.size() < g_stringsize - 1 ? value.size() : g_stringsize - 1;
  ::memcpy(slot, value.c_str(), count * sizeof(wchar_t));
  slot[count] = L'\0';
}

}  // namespace nsis
}  // namespace bk
