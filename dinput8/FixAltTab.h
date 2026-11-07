#pragma once
// ============================================================================
// FixAltTab.h
//
// Behind the [MAIN] AllowAltTab ini switch:
//
// 1. Focus-steal loop kill (VA 0x40994D: je -> jmp; applied by default)
//    The game's window procedure (VA 0x409932) checks on EVERY dispatched
//    message whether it is the foreground window and if not calls
//    SetActiveWindow + SetForegroundWindow to grab focus back. This is
//    why ALT+TAB did "nothing" (the switch was undone on the next game
//    message) and why the game "kept reading input" while apparently in
//    the background — with the steal loop alive, focus never actually
//    left the game. Converting the je into an unconditional jmp disables
//    the steal; the legitimate click-to-focus paths (VA 0x409DCF and
//    0x409DDD) keep working.
//
// 2. Non-exclusive mouse request (VA 0x426371: jnz -> jmp; opt-in only)
//    CMouse::init branches on its "exclusive input" argument; one path
//    requests DISCL_EXCLUSIVE|DISCL_FOREGROUND (push 5) for the mouse.
//    The keyboard never receives a SetCooperativeLevel call at all (it
//    runs on DirectInput's default: non-exclusive/background), so the
//    exclusive-keyboard theory behind this patch was wrong — the ALT+TAB
//    thief was always the WndProc focus-steal loop. Forcing the mouse's
//    non-exclusive modes additionally changed its acquisition behavior
//    and left the device returning zeros while focused (dx pinned at 0
//    with the game process foreground), so this patch is opt-in only.
// ============================================================================
#include <windows.h>

class FixAltTab
{
public:
    // Focus-steal patch is applied unconditionally (when AllowAltTab is
    // set); the non-exclusive-mouse patch only when nonExclusiveMouse is
    // true. Returns false unless every requested patch was applied (or
    // was already applied: idempotent); refuses a foreign exe build.
    static bool Install(bool nonExclusiveMouse = false);

    FixAltTab() = delete;   // static-only: no state, no instances
};
