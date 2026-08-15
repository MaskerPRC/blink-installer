// Lets the installer page call back into NSIS script functions.
//
// The script registers a function by name:
//
//     GetFunctionAddress $0 InstallDriver
//     blinkkit::RegisterAbility "installDriver" $0
//
// and the page calls it by that name. The address never leaves C++.
//
// Keeping the address on this side is the whole design. The obvious shortcut —
// putting it in the shared config store and letting the page pass the number
// back — hands an arbitrary code offset to whatever can write a config key, and
// `ExecuteCodeSegment` will jump to it. Names in, addresses stay here.
#pragma once

#include <string>
#include <vector>

namespace bk {
namespace nsis {

// `address` is what GetFunctionAddress produced (NSIS returns offset+1).
void RegisterFunction(const std::string& name, int address);

bool HasFunction(const std::string& name);
std::vector<std::string> RegisteredFunctions();

// Executes the named function's code segment. Returns false with a reason when
// the name was never registered.
bool CallFunction(const std::string& name, std::string* error);

void ClearFunctions();

}  // namespace nsis
}  // namespace bk
