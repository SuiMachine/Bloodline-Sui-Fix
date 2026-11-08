#pragma once
// ============================================================================
// FixAltTab.h
//
// Behind the [MAIN] AllowAltTab ini switch:
//
// Focus-steal loop kill (VA 0x40994D: je -> jmp; applied by default)
// The game's window procedure (VA 0x409932) checks on EVERY dispatched
// message whether it is the foreground window and if not calls
// SetActiveWindow + SetForegroundWindow to grab focus back. This is
// why ALT+TAB did "nothing" (the switch was undone on the next game
// message) and why the game "kept reading input" while apparently in
// the background — with the steal loop alive, focus never actually
// left the game. Converting the je into an unconditional jmp disables
// the steal; the legitimate click-to-focus paths (VA 0x409DCF and
// 0x409DDD) keep working.
//
// ============================================================================
#include <windows.h>

class FixAltTab
{
public:
    // Focus-steal patch is applied unconditionally (when AllowAltTab is
    // set); the non-exclusive-mouse patch only when nonExclusiveMouse is
    // true. Returns false unless every requested patch was applied (or
    // was already applied: idempotent); refuses a foreign exe build.
    static bool Install();

    FixAltTab() = delete;   // static-only: no state, no instances
};
