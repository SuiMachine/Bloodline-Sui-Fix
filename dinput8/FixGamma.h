#pragma once
// ============================================================================
// FixGamma.h
//
// Post-process gamma correction for the game's OpenGL renderer, behind the
// [MAIN] Gamma ini value:
//     Gamma = 1.0            unchanged (also: hook not installed at all)
//     Gamma = 1.2            brighter (image raised to 1/1.2)
//     Gamma = 0.8            darker
// The full 0 value disables the feature.
//
// The game renders through OpenGL (Bloodline.exe imports 339 functions from
// opengl32.dll; the d3d8.dll!Direct3DCreate8 import is dead - zero call
// sites) and presents its double buffer through gdi32!SwapBuffers. The
// Detours detour below draws one fullscreen pass just before the swap: the
// backbuffer is copied into a private texture and redrawn through a tiny
// GL_ARB_fragment_program applying pow(color, 1/Gamma) per channel. This
// is a true image-space adjustment - the desktop gamma ramps, hardware
// palettes and the game's pixel format are never touched.
//
// All GL state the pass modifies is restored (classic state via
// glPushAttrib/glPopAttrib and matrix pushes; ARB program state is saved
// and restored manually because it is not covered by the attribute bits),
// and the game's own vertex/fragment programs are unbound for the duration
// of the quad so their transforms cannot affect the pass.
// ============================================================================
#include <windows.h>

class FixGamma
{
public:
    // Attach the SwapBuffers detour (call from inside an open
    // DetourTransactionBegin/Commit region, like the other fixes). The
    // gamma value is stored for the pass; 1.0 disables the effect but
    // keeps the pass cheap (copied image redrawn unchanged would be a
    // waste - callers are expected to not install on 1.0).
    static bool Install(double gamma);

    FixGamma() = delete;   // static-only: per-context state lives in the .cpp
};
