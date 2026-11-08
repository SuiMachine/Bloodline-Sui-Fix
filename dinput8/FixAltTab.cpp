#include "FixAltTab.h"

// ============================================================================
// Internals
// ============================================================================
namespace {

    constexpr uintptr_t BL_PREFERRED_BASE = 0x400000;

    // Patch 2 — the focus-steal loop. Prologue of the window procedure
    // (VA 0x409932), runs before dispatching EVERY message:
    //   0x40993F:  call GetForegroundWindow
    //   0x409945:  mov  ecx, [0x6D1B6C]        ; game root object
    //   0x40994B:  cmp  eax, [ecx]             ; foreground == game hwnd?
    //   0x40994D:  je   0x40996D               ; skip the steal when equal
    //   0x40994F:  ...  SetActiveWindow + SetForegroundWindow(game)
    //   0x40996D:  <dispatch switch>
    // Converting the je into an unconditional jmp makes the dispatch switch
    // the only thing the prologue reaches, so the game stops re-grabbing
    // focus on every message. The cmp/jz pair occurs exactly once in the
    // image at this spot.
    constexpr uintptr_t VA_FOCUS_STEAL_JE = 0x40994D;
    constexpr BYTE      OPCODE_JE         = 0x74;

    constexpr BYTE      OPCODE_JMP_SHORT  = 0xEB;

    // Flip a single conditional branch opcode to an unconditional jmp.
    // Returns false without touching memory unless the site currently holds
    // the expected opcode (idempotent on re-install, refuses foreign builds).
    bool BranchToJmp(uintptr_t va, BYTE expectedOpcode)
    {
        BYTE *const site = (BYTE *)va;

        DWORD protect = 0;
        if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &protect))
            return false;

        const BYTE current = *site;
        if (current != OPCODE_JMP_SHORT && current != expectedOpcode)
        {
            VirtualProtect(site, 1, protect, &protect);
            return false;
        }
        *site = OPCODE_JMP_SHORT;
        VirtualProtect(site, 1, protect, &protect);
        return true;
    }

} // namespace

// ============================================================================
// FixAltTab::Install
// ============================================================================
bool FixAltTab::Install()
{
    if ((uintptr_t)GetModuleHandleA(nullptr) != BL_PREFERRED_BASE)
        return false;                       // image relocated; absolute VA invalid

    const bool focusOk = BranchToJmp(VA_FOCUS_STEAL_JE, OPCODE_JE);

    return focusOk;
}
