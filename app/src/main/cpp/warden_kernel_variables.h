// The kernel's exported variables, and the import records that point at them. See
// warden_kernel_variables.cc.
#ifndef WARDEN_KERNEL_VARIABLES_H_
#define WARDEN_KERNEL_VARIABLES_H_

#include <cstdint>
#include <vector>

#include "warden_xex.h"

namespace xe {
class Memory;
}

namespace warden {

// Gives every imported variable a home in guest memory with the value the console's
// kernel would have, and writes its address into the import record the title reads it
// through. Function records are left alone: the recompiler bound those. Called once,
// after the image is in guest memory and before any guest code runs.
void PatchImportVariables(xe::Memory& memory, const std::vector<XexImport>& imports);

// Where a kernel variable lives, by its xboxkrnl ordinal; 0 if it was not imported.
uint32_t KernelVariableAddress(uint32_t ordinal);

}  // namespace warden

#endif  // WARDEN_KERNEL_VARIABLES_H_
