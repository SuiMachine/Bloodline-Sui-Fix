#include "FixGamma.h"
#include <GL/gl.h>
#include <detours.h>
#include <cstdio>
#include <cstring>

// ============================================================================
// GL constants and types not in the GL 1.1 header (glext.h values).
// ============================================================================
namespace {

    constexpr GLenum GL_VERTEX_PROGRAM_ARB            = 0x8624;
    constexpr GLenum GL_VERTEX_PROGRAM_BINDING_ARB    = 0x8642;
    constexpr GLenum GL_FRAGMENT_PROGRAM_ARB          = 0x8804;
    constexpr GLenum GL_FRAGMENT_PROGRAM_BINDING_ARB  = 0x88F6;
    constexpr GLenum GL_PROGRAM_FORMAT_ASCII_ARB      = 0x8875;
    constexpr GLenum GL_TEXTURE0_ARB                  = 0x84C0;

    typedef void   (APIENTRY *PFNGLENABLEPCARBPROC)(GLenum);
    typedef void   (APIENTRY *PFNGLGENPROGRAMSARBPROC)(GLsizei, GLuint *);
    typedef void   (APIENTRY *PFNGLDELETEPROGRAMSARBPROC)(GLsizei, const GLuint *);
    typedef void   (APIENTRY *PFNGLBINDPROGRAMARBPROC)(GLenum, GLuint);
    typedef void   (APIENTRY *PFNGLPROGRAMSTRINGARBPROC)(GLenum, GLenum, GLsizei, const GLvoid *);
    typedef void   (APIENTRY *PFNGLPROGRAMLOCALPARAM4FARBPROC)(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
    typedef void   (APIENTRY *PFNGLACTIVETEXTUREARBPROC)(GLenum);

    // ---- state -------------------------------------------------------------
    double g_gamma = 1.0;          // ini value; pass applies pow(color, 1/gamma)
    bool   g_armed = false;        // resources valid for g_ctx
    HGLRC  g_ctx   = nullptr;      // context the resources below belong to

    PFNGLGENPROGRAMSARBPROC        g_genPrograms      = nullptr;
    PFNGLBINDPROGRAMARBPROC        g_bindProgram      = nullptr;
    PFNGLPROGRAMSTRINGARBPROC      g_programString    = nullptr;
    PFNGLPROGRAMLOCALPARAM4FARBPROC g_programLocal4f = nullptr;
    PFNGLACTIVETEXTUREARBPROC      g_activeTexture   = nullptr;

    GLuint g_program = 0;
    GLuint g_texture = 0;
    int    g_texW = 0, g_texH = 0;
    bool   g_loggedSetup = false;   // one-time diagnostics on failure

    // One dedicated texture coordinate set: uv (0,0)..(1,1) across the quad.
    const char *const GAMMA_PROGRAM =
        "!!ARBfp1.0\n"
        "PARAM exponent = program.local[0];\n"
        "TEMP scene;\n"
        "TEX scene.xyzw, fragment.texcoord[0], texture[0], 2D;\n"
        "POW result.color.x, scene.x, exponent.x;\n"
        "POW result.color.y, scene.y, exponent.x;\n"
        "POW result.color.z, scene.z, exponent.x;\n"
        "MOV result.color.w, scene.w;\n"
        "END\n";

    void LogOnce(const char *msg)
    {
        if (g_loggedSetup)
            return;
        g_loggedSetup = true;
        printf("FixGamma: %s\n", msg);
        OutputDebugStringA("FixGamma(fn): see console message\n");
    }

    bool ResolveProcs()
    {
        if (!g_genPrograms)     g_genPrograms     = (PFNGLGENPROGRAMSARBPROC)wglGetProcAddress("glGenProgramsARB");
        if (!g_bindProgram)     g_bindProgram     = (PFNGLBINDPROGRAMARBPROC)wglGetProcAddress("glBindProgramARB");
        if (!g_programString)   g_programString   = (PFNGLPROGRAMSTRINGARBPROC)wglGetProcAddress("glProgramStringARB");
        if (!g_programLocal4f)  g_programLocal4f = (PFNGLPROGRAMLOCALPARAM4FARBPROC)wglGetProcAddress("glProgramLocalParameter4fARB");
        if (!g_activeTexture)   g_activeTexture   = (PFNGLACTIVETEXTUREARBPROC)wglGetProcAddress("glActiveTextureARB");
        return g_genPrograms && g_bindProgram && g_programString &&
               g_programLocal4f /* glActiveTextureARB optional (multitexture) */;
    }

    // Create the per-context resources. If the game ever switches contexts
    // the old resources are simply abandoned (one GL context is the norm).
    bool ArmForContext(HGLRC ctx)
    {
        g_ctx   = ctx;
        g_armed = false;

        if (!ResolveProcs())
        {
            LogOnce("GL_ARB_fragment_program unavailable - gamma pass disabled");
            return false;
        }

        while (glGetError() != GL_NO_ERROR) {}      // drain pre-existing errors

        glGetError();                                // start clean from here
        g_genPrograms(1, &g_program);
        g_bindProgram(GL_FRAGMENT_PROGRAM_ARB, g_program);
        g_programString(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
                         (GLsizei)strlen(GAMMA_PROGRAM), GAMMA_PROGRAM);

        glGenTextures(1, &g_texture);
        g_texW = g_texH = 0;                         // (re)allocated on first pass

        const GLenum err = glGetError();
        if (err != GL_NO_ERROR || g_program == 0 || g_texture == 0)
        {
            LogOnce("fragment program rejected by the driver - gamma pass disabled");
            return false;
        }

        g_armed = true;
        return true;
    }

    // The pass proper: copy the backbuffer into the private texture and
    // redraw it through the gamma fragment program.
    void GammaPass()
    {
        const HGLRC ctx = wglGetCurrentContext();
        if (!ctx || (ctx != g_ctx && !ArmForContext(ctx)) || (!g_armed && !ArmForContext(ctx)))
            return;

        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        const int w = vp[2], h = vp[3];
        if (w <= 0 || h <= 0)
            return;

        // ARB program state is NOT covered by glPushAttrib: save manually.
        const GLboolean vpEnabled  = glIsEnabled(GL_VERTEX_PROGRAM_ARB);
        const GLboolean fpEnabled  = glIsEnabled(GL_FRAGMENT_PROGRAM_ARB);
        GLint vpBinding = 0, fpBinding = 0;
        glGetIntegerv(GL_VERTEX_PROGRAM_BINDING_ARB, &vpBinding);
        glGetIntegerv(GL_FRAGMENT_PROGRAM_BINDING_ARB, &fpBinding);

        // Matrix stacks are not attribute bits either: push now, pop after.
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();

        glPushAttrib(GL_ALL_ATTRIB_BITS);

        // Keep the game's vertex programs off our quad.
        glDisable(GL_VERTEX_PROGRAM_ARB);

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);

        // Texture resources on unit 0 (restore via attrib + binding below).
        if (g_activeTexture)
            g_activeTexture(GL_TEXTURE0_ARB);
        glBindTexture(GL_TEXTURE_2D, g_texture);
        if (w != g_texW || h != g_texH)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            g_texW = w;
            g_texH = h;
        }
        glEnable(GL_TEXTURE_2D);

        // Snapshot the finished frame into our texture (reads the current
        // read buffer - the backbuffer while a double buffer is pending).
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], w, h);

        // Fullscreen redraw through the program: pow(color, 1/gamma).
        g_bindProgram(GL_FRAGMENT_PROGRAM_ARB, g_program);
        const GLfloat e = (GLfloat)(1.0 / g_gamma);
        g_programLocal4f(GL_FRAGMENT_PROGRAM_ARB, 0, e, e, e, e);
        glEnable(GL_FRAGMENT_PROGRAM_ARB);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, (GLdouble)w, 0.0, (GLdouble)h, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2i(0, 0);
            glTexCoord2f(1.0f, 0.0f); glVertex2i(w, 0);
            glTexCoord2f(1.0f, 1.0f); glVertex2i(w, h);
            glTexCoord2f(0.0f, 1.0f); glVertex2i(0, h);
        glEnd();

        // Restore everything. glPopAttrib covers all classic state (the
        // enables, the texture bindings, the blend/depth/alpha settings);
        // program and matrix state is restored manually.
        glPopAttrib();

        g_bindProgram(GL_FRAGMENT_PROGRAM_ARB, (GLuint)fpBinding);
        g_bindProgram(GL_VERTEX_PROGRAM_ARB, (GLuint)vpBinding);
        if (fpEnabled) glEnable(GL_FRAGMENT_PROGRAM_ARB); else glDisable(GL_FRAGMENT_PROGRAM_ARB);
        if (vpEnabled) glEnable(GL_VERTEX_PROGRAM_ARB); else glDisable(GL_VERTEX_PROGRAM_ARB);

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();

        // Do not let our own stray GL errors leak into the game's error
        // queries (drain everything we may have produced).
        if (glGetError() != GL_NO_ERROR)
            while (glGetError() != GL_NO_ERROR) {}
    }

    // ---- hotkey adjustment ---------------------------------------------------
    // Shift + "+" (VK_OEM_PLUS, i.e. the unshifted "=" key, or the numpad
    // VK_ADD) raises the gamma by 5%, Shift + "-" lowers it by 5%; the
    // value is clamped to [0.3, 4.0] and written back to dinput8.ini so
    // sessions stay consistent. Polled once per frame from the
    // SwapBuffers detour; keys are edge-detected and ignored while
    // another process owns the foreground.
    bool   s_plusDown  = false;
    bool   s_minusDown = false;
    double s_gammaMin  = 0.30;
    double s_gammaMax  = 4.00;

    void PersistGamma()
    {
        char ini[MAX_PATH];
        HMODULE self = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)&PersistGamma, &self) ||
            !GetModuleFileNameA(self, ini, MAX_PATH))
            return;
        char *const slash = strrchr(ini, '\\');
        if (!slash)
            return;
        *slash = '\0';
        strcat_s(ini, sizeof(ini), "\\dinput8.ini");

        char val[32];
        sprintf_s(val, sizeof(val), "%.3f", g_gamma);
        WritePrivateProfileStringA("MAIN", "Gamma", val, ini);
    }

    void PollHotkeys()
    {
        DWORD fgPid = 0;
        const HWND fg = GetForegroundWindow();
        GetWindowThreadProcessId(fg, &fgPid);
        if (fg == NULL || fgPid != GetCurrentProcessId())
        {
            s_plusDown = s_minusDown = false;   // no key state across focus
            return;
        }

        if (!(GetAsyncKeyState(VK_SHIFT) & 0x8000))
        {
            s_plusDown = s_minusDown = false;
            return;
        }

        const bool plus  = (GetAsyncKeyState(VK_OEM_PLUS)   & 0x8000) != 0 ||
                           (GetAsyncKeyState(VK_ADD)       & 0x8000) != 0;
        const bool minus = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0 ||
                           (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0;

        if (plus && !s_plusDown && g_gamma < s_gammaMax)
        {
            g_gamma = (g_gamma * 1.05 < s_gammaMax) ? g_gamma * 1.05 : s_gammaMax;
            PersistGamma();
        }
        if (minus && !s_minusDown && g_gamma > s_gammaMin)
        {
            g_gamma = (g_gamma / 1.05 > s_gammaMin) ? g_gamma / 1.05 : s_gammaMin;
            PersistGamma();
        }

        s_plusDown = plus;
        s_minusDown = minus;
    }

    BOOL (WINAPI *TrueSwapBuffers)(HDC hdc) = SwapBuffers;

    BOOL WINAPI DetourSwapBuffers(HDC hdc)
    {
        PollHotkeys();
        if (g_gamma != 1.0)
            GammaPass();
        return TrueSwapBuffers(hdc);
    }

} // namespace

// ============================================================================
// FixGamma::Install
// ============================================================================
bool FixGamma::Install(double gamma)
{
    g_gamma = (gamma > 0.0) ? gamma : 1.0;
    DetourAttach(&(PVOID&)TrueSwapBuffers, DetourSwapBuffers);
    return true;
}
