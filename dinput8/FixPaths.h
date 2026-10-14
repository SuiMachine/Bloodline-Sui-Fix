#pragma once
// ============================================================================
// BloodlineInitFix.h
//
// Replacement for Bloodline.exe!CTEGameParam::initPaths  (VA 0x5E9D7D, x86).
//
// Fixes the "garbage folder" bug: the original walks WORK_DIR and calls
// CreateDirectoryA on every backslash-delimited prefix, but copies the
// prefix with unterminated strncpy() into an uninitialized stack buffer,
// so folder names get trailing stack junk (e.g. "čvxE–" folders).
//
// Behavior is otherwise 1:1 with the original (same error boxes, same
// globals written, same trailing-backslash string formats).
// ============================================================================
#include <windows.h>
#include "hookFunctions.h"

#ifdef __cplusplus
extern "C" {
#endif

    // Signature of the original at 0x5E9D7D:
    //   unsigned char __cdecl initPaths(void* hUnused)
    // (single dword arg, never read by the original; return in AL, caller
    //  masks with 0xFF — see the call site at VA 0x5EA4C3.)
    // Fully reimplements the original; never calls it.
    unsigned char __cdecl BlInitPaths_Fixed(void *hUnused);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace bl {

    // The exe is a 2005 build without /DYNAMICBASE and loads at its preferred
    // ImageBase 0x400000. Verify before installing any hook.
    bool IsAtPreferredBase();

} // namespace bl
#endif