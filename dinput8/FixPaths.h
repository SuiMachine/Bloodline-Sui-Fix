#pragma once
// ============================================================================
// FixPaths.h
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
// Stateless installer for the initPaths detour above.
class FixPaths
{
public:
    // Detour initPaths to BlInitPaths_Fixed. Returns false and patches
    // nothing unless the exe is loaded at its preferred base 0x400000
    // (2005 build without /DYNAMICBASE), where the absolute VA is valid.
    static bool Install();

    FixPaths() = delete;   // static-only: no state, no instances
};
#endif
