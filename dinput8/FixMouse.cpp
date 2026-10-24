#include "FixMouse.h"
#include "hookFunctions.h"
#include <math.h>

// ============================================================================
// Internals
// ============================================================================
namespace {

    constexpr uintptr_t BL_PREFERRED_BASE = 0x400000;
    constexpr uintptr_t VA_MOUSE_UPDATE   = 0x425A70;   // CMouse::update (hook target)
    constexpr int      HOOK_OVERRIDE_LEN  = 6;          // whole instructions (see Install)

    constexpr int      ACTION_COUNT       = 10;         // move l/r/u/d, wheel u/d, 4 buttons
    constexpr size_t   ACTION_STRIDE      = 0x28;

    // Frame length the original response curve was tuned for (the game
    // shipped as vsync-locked exclusive fullscreen).
    constexpr double REF_FRAME_MS = 1000.0 / 60.0;

    // Response curve constants of the original (VA 0x425D0D..0x425D57):
    //   value = pow(|delta| / 80.0, 1.5) * 50000.0 / dt
    constexpr double CURVE_SCALE = 80.0;
    constexpr double CURVE_EXP   = 1.5;
    constexpr double CURVE_GAIN  = 50000.0;

    // ---- CMouse action entry: only the members update() touches ----
    // Ten of these live at CMouse+0x00..+0x188. Entries 0..5 are axes
    // (move left/right = X, move up/down = Y, wheel up/down = Z — the device
    // uses a custom DIDF_RELABS data format at VA 0x683604, so the state at
    // CMouse+0x19C is { LONG x, y, z; BYTE buttons[4]; }), entries 6..9 are
    // the mouse buttons.
    struct MouseAction
    {
        DWORD         isAxis;     // +0x00  nonzero: axis response, 0: button
        const BYTE   *button;     // +0x04  button byte in state (button entries)
        const LONG   *axis;       // +0x08  axis dword in state (axis entries)
        DWORD         direction;  // +0x0C  1: fires on negative, 0: on positive
        const char    *name;      // +0x10  display name
        DWORD         held;       // +0x14  held flag (dead-zone tracking)
        double        value;      // +0x18  response, read via GetActionFloat
        double        heldTime;   // +0x20
    };
    static_assert(sizeof(MouseAction) == ACTION_STRIDE, "action stride mismatch");

    typedef void(__fastcall *MouseUpdate_t)(void *self, int edxUnused, double dt);
    MouseUpdate_t TrueMouseUpdate = nullptr;

    // Linear axis-response multiplier set by Install(); the detour runs in
    // this translation unit, so a plain static beats plumbing it through
    // the fixed __fastcall signature the game calls.
    double g_sensitivity = 1.0;

} // namespace

// ============================================================================
// FixMouse::Install
// ============================================================================
bool FixMouse::Install(double sensitivity)
{
    g_sensitivity = min(max(sensitivity, 0.0001f), 5.f) * 0.1f;

    if (TrueMouseUpdate)
        return true;
    if ((uintptr_t)GetModuleHandleA(nullptr) != BL_PREFERRED_BASE)
        return false;                       // image relocated; absolute VA invalid

    // Hook() overwrites the first HOOK_OVERRIDE_LEN bytes of CMouse::update,
    // so the trampoline must re-execute them before jumping back. All 6
    // (push ebp | mov ebp,esp | sub esp,0xC) are position-independent, so a
    // flat copy followed by a jmp is a valid trampoline.
    BYTE *const trampoline = (BYTE *)VirtualAlloc(nullptr,
        HOOK_OVERRIDE_LEN + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline)
        return false;

    CopyMemory(trampoline, (void *)VA_MOUSE_UPDATE, HOOK_OVERRIDE_LEN);
    trampoline[HOOK_OVERRIDE_LEN] = 0xE9;   // jmp VA_MOUSE_UPDATE + HOOK_OVERRIDE_LEN
    *(DWORD *)(trampoline + HOOK_OVERRIDE_LEN + 1) =
        (DWORD)(VA_MOUSE_UPDATE + HOOK_OVERRIDE_LEN) - (DWORD)(trampoline + HOOK_OVERRIDE_LEN + 5);

    TrueMouseUpdate = (MouseUpdate_t)trampoline;
    Hook(VA_MOUSE_UPDATE, (void *)&BlMouseUpdate_Fixed, HOOK_OVERRIDE_LEN);
    return true;
}

// ============================================================================
// The detour — calls the original, then replaces only the axis responses.
// ============================================================================
extern "C" void __fastcall BlMouseUpdate_Fixed(void *self, int edxUnused, double dt)
{
    // Run the original first: it polls DirectInput and updates buttons,
    // wheel clicks, hold timers — and stores the frame-rate-coupled axis
    // responses that are replaced below.
    TrueMouseUpdate(self, edxUnused, dt);

    // The original divides by dt unconditionally; with dt <= 0 its results
    // are already pinned to the GetActionFloat() clamp, so leave them.
    if (!(dt > 0.0))
        return;

    MouseAction *const actions = (MouseAction *)self;

    for (int i = 0; i < ACTION_COUNT; ++i)
    {
        MouseAction *a = &actions[i];

        // Button entries (isAxis == 0) use the 0x80-bit path and keep the
        // values the original just computed.
        if (!a->isAxis || !a->axis)
            continue;

        const LONG delta = *a->axis;

        // Direction gate, mirroring the original: type 1 fires on negative
        // deltas, type 0 on positive, anything else stays silent.
        bool active;
        if (a->direction == 1)      active = (delta < 0);
        else if (a->direction == 0) active = (delta > 0);
        else                        active = false;
        if (!active)
        {
            a->value = 0.0;                 // same reset the original performs
            continue;
        }

        // THE FIX: normalize the per-frame count to a per-millisecond rate
        // before applying the response curve, so the response depends on
        // hand speed only. Bit-identical to the original at 60 fps with
        // g_sensitivity == 1.0; the sensitivity scales the result linearly
        // (after the curve, so it does not reshape it).
        const double countsPerMs = (double)delta / dt;
        a->value = pow(fabs(countsPerMs) * REF_FRAME_MS / CURVE_SCALE, CURVE_EXP)
                   * CURVE_GAIN / REF_FRAME_MS * g_sensitivity;
    }
}
