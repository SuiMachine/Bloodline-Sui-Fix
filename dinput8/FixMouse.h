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
    static bool Install();

    FixMouse() = delete;   // static-only: no state, no instances
};
#endif
