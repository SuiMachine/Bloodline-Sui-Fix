#pragma once
// ============================================================================
// FixMouse.h
//
// Detour for Bloodline.exe!CMouse::update  (VA 0x425A70, x86 thiscall:
//   void thiscall CMouse::update(double dt), dt in milliseconds).
//
// Fixes the frame-rate-coupled camera rotation speed.
//
// The original applies its mouse response curve to the RAW per-frame count:
//     value = pow(|delta| / 80.0, 1.5) * 50000.0 / dt      (VA 0x425D0D)
// Because the per-frame count itself grows with the frame time, this does
// not normalize anything: after the camera multiplies by dt again
// (VA 0x42A22F:  turn = intensity * dt * 0.02), the resulting turn rate is
// proportional to sqrt(frame time). The camera spins up to ~1.4x faster at
// 30 fps and ~2.5x faster at 10 fps than at 60 fps, and every frame hitch
// turns into a (d^1.5) lurch. The game was tuned for a vsync-locked 60 fps,
// so on modern machines with uncapped/unstable frame rates the look speed
// "sometimes accelerates massively".
//
// The detour runs the original first (so polling, buttons, wheel clicks and
// hold timers are untouched), then recomputes only the axis responses,
// normalizing the count to a per-millisecond rate before the curve:
//     value = pow(|delta| / dt * REF / 80.0, 1.5) * 50000.0 / REF
// with REF = 1000/60 ms, which is bit-identical to the original at a steady
// 60 fps and frame-rate-independent everywhere else.
//
// Sensitivity: Install(sensitivity) stores a linear multiplier that is
// applied to the axis responses AFTER the response curve, so it scales the
// look speed without changing the curve's shape (1.0 = original behavior).
//
// Menu: the in-game menu loop is the one caller of the device-update
// dispatcher that passes a synthetic dt of exactly 1.0 ms (VA 0x5F16DD)
// instead of a real frame time. The detour detects that value and
// substitutes the measured elapsed time between calls for the rate
// normalization, so the camera visible behind the menu tracks at the same
// speed as gameplay instead of ~68x faster.
//
// Focus gate (runs while [MAIN] AllowAltTab is set): the game's mouse
// device is bound, via its DirectInput cooperative level, to the game's
// invisible internal root window — not the rendered surface. Foreground-
// cooperative devices only stay acquired while THAT window is foreground.
// Vanilla kept it there by stealing focus in its window procedure on
// every message (which made ALT+TAB impossible); FixAltTab's patch kills
// the steal, and this detour restores its load-bearing half: while the
// game process owns the foreground, the root window is promoted back on
// every poll; while another process has the foreground (alt-tab, a
// debugger session), every action response is zeroed so nothing leaks
// through. Focus transitions are appended to dinput8_fixmouse.log next
// to the DLL, naming whichever window holds the foreground.
// ============================================================================
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

    // __fastcall mirrors the original thiscall: ECX = this (EDX is unused by
    // the original), the double argument arrives on the stack and is cleaned
    // by the original's `ret 8`.
    void __fastcall BlMouseUpdate_Fixed(void *self, int edxUnused, double dt);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// Stateless installer for the CMouse::update detour above.
class FixMouse
{
public:
    // Detour CMouse::update to BlMouseUpdate_Fixed. Returns false and
    // patches nothing unless the exe is loaded at its preferred base
    // 0x400000 (2005 build without /DYNAMICBASE), where the absolute VA
    // is valid.
    //
    // sensitivity: linear axis-response multiplier used by the detour
    // (must be called before the detour can ever run, i.e. before the
    // game's first CMouse::update). Non-positive values are ignored and
    // 1.0 is used instead.
    static bool Install(double sensitivity = 1.0,
                         bool gateWhenUnfocused = true);

    FixMouse() = delete;   // static-only: no state, no instances
};
#endif
