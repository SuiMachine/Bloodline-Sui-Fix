#include "FixMouse.h"
#include "hookFunctions.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <cstdio>

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

    // Whether the detour should zero the responses while the game process
    // is not the foreground one (see the focus gate in the detour).
    bool g_gateEnabled = true;

    // The game's internal root window: [0x6D1B6C] points at the root
    // object whose +0 is the window handle that the game's old WndProc
    // steal loop kept foregrounded (verified in the WndProc disassembly:
    // it is compared against GetForegroundWindow). The DirectInput
    // devices' cooperative level is bound to a window of this kind, not
    // to the rendered surface. Guarded: init order and cross-build memory
    // are not trusted.
    HWND RootWindow()
    {
        __try
        {
            HWND *const rootObj = *(HWND **)(uintptr_t)0x6D1B6C;
            if (!rootObj)
                return NULL;
            const HWND hwnd = *rootObj;
            if (!hwnd || !IsWindow(hwnd))
                return NULL;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            return (pid == GetCurrentProcessId()) ? hwnd : NULL;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return NULL;
        }
    }

    // ---- Debug logger: appends to dinput8_fixmouse.log next to the DLL ----
    // printf needs a console and OutputDebugStringA needs a debugger or
    // DebugView; a file needs neither, which is what debugging this needs.
    void LogLine(const char *text)
    {
        static char s_path[MAX_PATH] = {};
        if (!s_path[0])
        {
            HMODULE self = nullptr;
            char dll[MAX_PATH];
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)&LogLine, &self) ||
                !GetModuleFileNameA(self, dll, MAX_PATH))
                return;
            char *const slash = strrchr(dll, '\\');
            if (!slash)
                return;
            *slash = '\0';
            sprintf_s(s_path, sizeof(s_path), "%s\\dinput8_fixmouse.log", dll);
        }
        FILE *f = nullptr;
        if (fopen_s(&f, s_path, "a") != 0 || !f)
            return;
        fwrite(text, 1, strlen(text), f);
        fclose(f);
    }

    // ---- Menu-path frame-time recovery ------------------------------------
    // The in-game menu loop is the one caller of the device-update
    // dispatcher (VA 0x426FB0) that does NOT pass a real frame time: it
    // pushes a synthetic double 1.0 (VA 0x5F16DD). See the detour below.
    double g_qpcToMs    = 0.0;   // QPC ticks -> ms, filled on first call
    double g_lastCallMs = -1.0; // QPC time of the previous detour call

    double QpcNowMs()
    {
        if (g_qpcToMs == 0.0)
        {
            LARGE_INTEGER freq;
            if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0)
                return -1.0;
            g_qpcToMs = 1000.0 / (double)freq.QuadPart;
        }
        LARGE_INTEGER now;
        if (!QueryPerformanceCounter(&now))
            return -1.0;
        return (double)now.QuadPart * g_qpcToMs;
    }

} // namespace

// ============================================================================
// FixMouse::Install
// ============================================================================
bool FixMouse::Install(double sensitivity, bool gateWhenUnfocused)
{
    g_sensitivity = min(max(sensitivity, 0.0001f), 5.f) * 0.1f;
    g_gateEnabled  = gateWhenUnfocused;

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

    // FOCUS GATE (AllowAltTab): half one of the merged focus fix — while
    // the foreground window belongs to another process (alt-tab, a
    // debugger session), zero every action response so nothing leaks
    // through, and log every verdict with the identity of the window that
    // holds the foreground. Half two (the root promotion) runs below once
    // the game process owns the foreground again.
    if (g_gateEnabled)
    {
        static bool s_lastFocused = true;
        static DWORD s_lastBeatTick = 0;

        DWORD fgPid = 0;
        const HWND fg = GetForegroundWindow();
        GetWindowThreadProcessId(fg, &fgPid);
        const bool focused = (fg != NULL && fgPid == GetCurrentProcessId());

        if (focused != s_lastFocused)
        {
            s_lastFocused = focused;

            char owner[192] = "";
            if (fg != NULL || fgPid != 0)
            {
                char title[96] = "";
                char exe[MAX_PATH] = "";
                if (fg != NULL)
                    GetWindowTextA(fg, title, sizeof(title));
                HANDLE h = NULL;
                if (fgPid != 0)
                    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, fgPid);
                if (h)
                {
                    DWORD exeLen = sizeof(exe);
                    if (QueryFullProcessImageNameA(h, 0, exe, &exeLen) && exeLen > 0)
                    {
                        const char *const base = strrchr(exe, '\\');
                        if (base)
                            memmove(exe, base + 1, exeLen - (base - exe));
                    }
                    CloseHandle(h);
                }
                sprintf_s(owner, sizeof(owner), "%s \"%s\"", exe[0] ? exe : "?", title);
            }

            char msg[320];
            if (focused)
                sprintf_s(msg, sizeof(msg),
                           "FixMouse: FOCUS GAINED fg=%08X fgPid=%u we=%u [%s]\n",
                           (unsigned)(uintptr_t)fg, fgPid, GetCurrentProcessId(), owner);
            else
                sprintf_s(msg, sizeof(msg),
                           "FixMouse: FOCUS LOST  fg=%08X fgPid=%u we=%u -> thief: %s\n",
                           (unsigned)(uintptr_t)fg, fgPid, GetCurrentProcessId(), owner);
            printf("%s", msg);
            OutputDebugStringA(msg);
            LogLine(msg);
        }

        if (!focused)
        {
            const DWORD now = GetTickCount();
            if (now - s_lastBeatTick >= 2000)
            {
                s_lastBeatTick = now;
                char msg[160];
                sprintf_s(msg, sizeof(msg),
                          "FixMouse: suppressed fg=%08X fgPid=%u we=%u\n",
                          (unsigned)(uintptr_t)fg, fgPid, GetCurrentProcessId());
                printf("%s", msg);
            }

            MouseAction *const quiet = (MouseAction *)self;
            for (int i = 0; i < ACTION_COUNT; ++i)
                quiet[i].value = 0.0;
            return;
        }

        // ROOT PROMOTION: the game's DirectInput devices are bound to its
        // internal root window — an invisible, empty-title window that is
        // NOT the rendered surface. Foreground-cooperative-level devices
        // only stay acquired while THAT window is the foreground window.
        // Vanilla kept it there by stealing focus on every window
        // message; suppressing that steal (FixAltTab) freed ALT+TAB but
        // silently killed all mouse input (device never acquires). While
        // the game process owns the foreground — established above —
        // promote the root window back, on our own poll cadence. This is
        // the load-bearing part of the old steal, but by construction it
        // can never fight another application: it only runs when the
        // foreground already belongs to the game.
        {
            const HWND root = RootWindow();
            static DWORD s_lastPromoteLogTick = 0;

            if (root && fg != root)
            {
                SetForegroundWindow(root);
                SetActiveWindow(root);

                const DWORD now = GetTickCount();
                if (now - s_lastPromoteLogTick >= 1000)
                {
                    s_lastPromoteLogTick = now;
                    char msg[160];
                    sprintf_s(msg, sizeof(msg),
                              "FixMouse: promoted root hwnd=%08X (fg was %08X)\n",
                              (unsigned)(uintptr_t)root, (unsigned)(uintptr_t)fg);
                    printf("%s", msg);
                    OutputDebugStringA(msg);
                    LogLine(msg);
                }
            }
        }
    }

    // NOTE: a "rescue" Acquire() of the mouse device was tried here and
    // REMOVED: calling into the DirectInput device from inside the detour
    // at an arbitrary poll crashed the process at startup (no debugger)
    // and left the device permanently dead under one. The click-death bug
    // (device unacquired by a click, never recovered by the game) is
    // still open; any retry must be evidence-gated and opt-in.

    // Timestamp the call so the menu case below can recover the real
    // frame length. prevMs is the previous call's time, whenever that was.
    const double nowMs  = QpcNowMs();
    const double prevMs = g_lastCallMs;
    if (nowMs >= 0.0)
        g_lastCallMs = nowMs;

    // ---- TEMP DIAGNOSTICS (in-game menu investigation) ----------------------
    // At most ~4 lines/sec, via printf and OutputDebugStringA (VS Output
    // window): the dt the caller claimed, the REAL gap since the previous
    // update call (QPC), the raw X delta this poll returned, and the
    // response the original just stored. Healthy = gap == dt == one frame.
    // How they diverge while the menu is open decides the fix:
    //   gap >> dt            counts accumulate between polls but the caller
    //                        still claims a frame-scale dt -> normalize by
    //                        the measured gap (dtForRate = max(dt, gap)).
    //   dt ~= gap >> frame   dt honestly covers the polling interval; the
    //                        amplification is on the consumer side.
    //   dt growing, gap ~ frame  the caller's clock itself is broken in
    //                        menu; trust the gap and note the growth law.
    {
        static double s_nextLogMs = -1.0;

        if (nowMs >= 0.0 && (s_nextLogMs < 0.0 || nowMs >= s_nextLogMs))
        {
            s_nextLogMs = nowMs + 250.0;

            MouseAction *const a0   = (MouseAction *)self;
            const LONG     rawDx    = (a0->axis ? *a0->axis : 0);
            const double   gapMs    = (prevMs >= 0.0 && nowMs > prevMs)
                                      ? nowMs - prevMs : -1.0;
            char buf[160];
            sprintf_s(buf, sizeof(buf),
                      "FixMouse: dt=%g gap=%g dx=%ld value=%g%s\n",
                      dt, gapMs, rawDx, a0->value,
                      (a0->value >= 400.0) ? " SAT" : "");
            printf("%s", buf);
            OutputDebugStringA(buf);
            LogLine(buf);
        }
    }

    // The original divides by dt unconditionally; with dt <= 0 its results
    // are already pinned to the GetActionFloat() clamp, so leave them.
    if (!(dt > 0.0))
        return;

    // A GUI-side loop (VA 0x5F16DD, exact purpose unknown — NOT the
    // in-game menu, verified at runtime) calls the dispatcher with a
    // synthetic dt of exactly 1.0 ms instead of a real frame time. Fed
    // through the per-ms normalization below that would become
    // (1000/60)^1.5 ~ 68x, so recover the real elapsed time since the
    // previous call instead. A genuine 1.0 ms gameplay frame measures
    // ~1.0 ms of elapsed time too, so this cannot misfire on the normal
    // path.
    double dtForRate = dt;
    if (dt == 1.0)
    {
        if (nowMs >= 0.0 && prevMs >= 0.0 && nowMs > prevMs)
            dtForRate = nowMs - prevMs;
        else
            dtForRate = REF_FRAME_MS;       // no usable measurement: assume 60 fps
    }

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
        // (after the curve, so it does not reshape it). dtForRate carries
        // the menu-path recovery above.
        const double countsPerMs = (double)delta / dtForRate;
        a->value = pow(fabs(countsPerMs) * REF_FRAME_MS / CURVE_SCALE, CURVE_EXP)
                   * CURVE_GAIN / REF_FRAME_MS * g_sensitivity;
    }
}
