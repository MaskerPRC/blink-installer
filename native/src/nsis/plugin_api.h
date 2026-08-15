// NSIS plug-in ABI.
//
// Derived from the NSIS SDK's exdll headers (zlib/libpng licence, (C) Nullsoft
// and contributors) and reduced to what this plugin uses.
//
// Unicode only: a Unicode makensis is required, so NSIS strings are wchar_t and
// this file declares them that way rather than as bytes to be cast at each use.
// Such a cast appears to work right up until g_stringsize is consulted, at
// which point the element size is wrong and the symptom is far from the cause.
#pragma once

#include <windows.h>

#include <string>

#include "log.h"

namespace bk {
namespace nsis {

enum NSPIM {
  NSPIM_UNLOAD,     // last message a plugin receives; do final cleanup
  NSPIM_GUIUNLOAD,  // sent after .onGUIEnd
};

using PluginCallback = UINT_PTR(*)(NSPIM);

struct exec_flags_t {
  int autoclose;
  int all_user_var;
  int exec_error;
  int abort;
  int exec_reboot;
  int reboot_called;
  int XXX_cur_insttype;
  int plugin_api_version;
  int silent;
  int instdir_error;
  int rtl;
  int errlvl;
  int alter_reg_view;
  int status_update;
};

struct extra_parameters {
  exec_flags_t* exec_flags;
  int(__stdcall* ExecuteCodeSegment)(int, HWND);
  void(__stdcall* validate_filename)(wchar_t*);
  int(__stdcall* RegisterPluginCallback)(HMODULE, PluginCallback);
};

struct stack_t {
  stack_t* next;
  wchar_t text[1];  // actually g_stringsize wide chars
};

// Bound by NSIS_INIT() at the top of every exported function.
extern unsigned int g_stringsize;
extern stack_t** g_stacktop;
extern wchar_t* g_variables;
extern extra_parameters* g_extra;
extern HWND g_parent;

// Every NSIS export must call this first: the stack pointer is passed per-call
// and nothing works if it is not rebound.
//
// Logging is initialised here rather than in one designated entry point,
// because scripts legitimately call RegisterAbility and SetConfig before
// InitWindow — and a diagnostic that only works after the window exists is
// useless for diagnosing a window that never appeared.
#define NSIS_INIT()                         \
  do {                                      \
    ::bk::nsis::g_stringsize = string_size; \
    ::bk::nsis::g_stacktop = stacktop;      \
    ::bk::nsis::g_variables = variables;    \
    ::bk::nsis::g_extra = extra;            \
    ::bk::nsis::g_parent = hwndParent;      \
    ::bk::LogInit();                        \
  } while (0)

// Pops the top of the NSIS stack. Returns false when the stack is empty.
bool PopString(std::wstring* out);
// Convenience: pops and converts to UTF-8, empty string when the stack is dry.
std::string PopUtf8();

void PushString(const std::wstring& value);
void PushUtf8(const std::string& value);

// Reads/writes $0..$9, $R0..$R9 and the built-in variables by index.
std::wstring GetUserVariable(int index);
void SetUserVariable(int index, const std::wstring& value);

// Well-known variable indices used by this plugin.
enum Variable {
  kVarInstDir = 25,  // $INSTDIR
};

// Signature every NSIS-callable export must have.
#define NSIS_EXPORT(name)                                                  \
  extern "C" __declspec(dllexport) void __cdecl name(                      \
      HWND hwndParent, int string_size, wchar_t* variables,                \
      ::bk::nsis::stack_t** stacktop, ::bk::nsis::extra_parameters* extra)

}  // namespace nsis
}  // namespace bk
