#include "FixPaths.h"

// ============================================================================
// Internals
// ============================================================================
namespace {

    constexpr uintptr_t BL_PREFERRED_BASE = 0x400000;

    // ---- image offsets (VA at preferred base 0x400000) ----
    constexpr uintptr_t VA_GLOBAL_GAMEPARAM = 0x743D40;  // CTEGameParam*
    constexpr uintptr_t VA_STRDUPN = 0x6613C0;   // game-heap string (re)alloc
    constexpr uintptr_t VA_STRCMP = 0x665350;   // custom compare
    constexpr uintptr_t VA_GETCWDSTR = 0x666D80;   // CWD as heap string
    constexpr uintptr_t VA_FATALBOX = 0x424739;   // "Adresar..." error box

    constexpr uintptr_t STR_NENI = 0x6CB544;
    constexpr uintptr_t STR_WORKDIR_KEY = 0x6CB54C;
    constexpr uintptr_t STR_PATHS = 0x6CB558;
    constexpr uintptr_t STR_INI_WORK = 0x6CB534;
    constexpr uintptr_t STR_NENI2 = 0x6CB570;
    constexpr uintptr_t STR_RUNDIR_KEY = 0x6CB578;
    constexpr uintptr_t STR_PATHS2 = 0x6CB580;
    constexpr uintptr_t STR_INI_RUN = 0x6CB560;
    constexpr uintptr_t STR_ERRTEXT_NOINI = 0x6CB50C;    // "...installed"
    constexpr uintptr_t STR_ERRTITLE1 = 0x6CB4FC;
    constexpr uintptr_t STR_ERRTEXT_BAD = 0x6CB5A8;    // "...installed!"
    constexpr uintptr_t STR_ERRTITLE2 = 0x6CB598;
    constexpr uintptr_t STR_ADRESAR = 0x6CB5D0;    // "Adresar\r\n%s\r\nnelze vytvorit!"

    uintptr_t BlVA(uintptr_t preferredVA)
    {
        return (uintptr_t)GetModuleHandleA(nullptr) + (preferredVA - BL_PREFERRED_BASE);
    }

    // ---- CTEGameParam: only the members initPaths touches ----
    struct CTEGameParam
    {
        unsigned char pad[0xA4];
        char *m_runDir;        // +0xA4  heap, ends WITH '\'
        char *m_workDir;       // +0xA8  heap, ends WITH '\'
        char          m_workDirCopy[0x504]; // +0xAC  inline copy, ends WITH '\'
    };

    CTEGameParam *GameParam()
    {
        return *(CTEGameParam **)BlVA(VA_GLOBAL_GAMEPARAM);
    }

    // ---- original helpers, called by address ----
    typedef char *(__cdecl *DupStr_t)  (char *oldPtr, int newLen);
    typedef int(__cdecl *StrCmp_t)  (const char *a, const char *b);
    typedef char *(__cdecl *GetCwd_t)  (int a, int b);
    typedef void(__cdecl *FatalBox_t)(const char *fmt, const char *path);

    DupStr_t   DupStr() { return (DupStr_t)BlVA(VA_STRDUPN); }
    StrCmp_t   StrCmp_() { return (StrCmp_t)BlVA(VA_STRCMP); }
    GetCwd_t   GetCwd() { return (GetCwd_t)BlVA(VA_GETCWDSTR); }
    FatalBox_t FatalBox() { return (FatalBox_t)BlVA(VA_FATALBOX); }

    // ---- the exe's own string literals (byte-faithful, no codepage issues) ----
    const char *L(const uintptr_t va) { return (const char *)BlVA(va); }

} // namespace

// ============================================================================
// bl::IsAtPreferredBase
// ============================================================================
namespace bl {
    bool IsAtPreferredBase()
    {
        return (uintptr_t)GetModuleHandleA(nullptr) == BL_PREFERRED_BASE;
    }
} // namespace bl

// ============================================================================
// The detour — mirrors the original at 0x5E9D7D instruction for instruction,
// except prefix[i] = '\0' in the mkdir-p loop.
// ============================================================================
extern "C" unsigned char __cdecl BlInitPaths_Fixed(void * /*hUnused — never read by the original*/)
{
    char     windirIni[MAX_PATH];
    char     exeDir[0x200];       // original buffer: [ebp-0xA90]
    char     workDir[0x504];       // [ebp-0x58C], ini size 0x503
    char     runDir[0x504];        // [ebp-0x19A0], ini size 0x503
    char     prefix[0x504];        // [ebp-0x149C]
    OFSTRUCT ofs;
    size_t   n;

    // ---- %windir%\Bloodline.ini ----
    windirIni[0] = '\0';
    GetWindowsDirectoryA(windirIni, MAX_PATH);
    lstrcatA(windirIni, "\\Bloodline.ini");

    // ---- current working directory, trailing '\' stripped ----
    lstrcpyA(exeDir, GetCwd()(0, 0));
    n = strlen(exeDir);
    if (n > 0 && exeDir[n - 1] == '\\')
        exeDir[n - 1] = '\0';

    CTEGameParam *gp = GameParam();

    // ---- does C:\Windows\Bloodline.ini exist? ----
    ofs.cBytes = sizeof(OFSTRUCT);
    ofs.fFixedDisk = 1;
    ofs.nErrCode = 0;
    if (OpenFile(windirIni, &ofs, OF_EXIST) == HFILE_ERROR)
    {
        gp->m_runDir = DupStr()(gp->m_runDir, lstrlenA(exeDir) + 1);
        lstrcpyA(gp->m_runDir, exeDir);
        MessageBoxA(nullptr, L(STR_ERRTEXT_NOINI), L(STR_ERRTITLE1), 0);
        return 0;
    }

    // ---- read PATHS (relative ini name resolves against %windir%) ----
    GetPrivateProfileStringA(L(STR_PATHS), L(STR_WORKDIR_KEY), L(STR_NENI),
        workDir, 0x503, L(STR_INI_WORK));
    GetPrivateProfileStringA(L(STR_PATHS2), L(STR_RUNDIR_KEY), L(STR_NENI2),
        runDir, 0x503, L(STR_INI_RUN));

    n = strlen(workDir);
    if (n > 0 && workDir[n - 1] == '\\') workDir[n - 1] = '\0';
    n = strlen(runDir);
    if (n > 0 && runDir[n - 1] == '\\') runDir[n - 1] = '\0';

    // ---- validation (argument order mirrors the original's custom strcmp) ----
    if (StrCmp_()(workDir, L(STR_NENI)) == 0
        || StrCmp_()(runDir, L(STR_NENI2)) == 0
        || StrCmp_()(exeDir, runDir) != 0)        // RUN_DIR must equal the CWD
    {
        gp->m_runDir = DupStr()(gp->m_runDir, lstrlenA(exeDir) + 1);
        lstrcpyA(gp->m_runDir, exeDir);
        MessageBoxA(nullptr, L(STR_ERRTEXT_BAD), L(STR_ERRTITLE2), 0);
        return 0;
    }

    // ---- store into the object ----
    gp->m_workDir = DupStr()(gp->m_workDir, lstrlenA(workDir) + 1);
    lstrcpyA(gp->m_workDir, workDir);
    lstrcpyA(gp->m_workDirCopy, workDir);            // inline copy at +0xAC

    gp->m_runDir = DupStr()(gp->m_runDir, lstrlenA(exeDir) + 1);
    lstrcpyA(gp->m_runDir, exeDir);

    // ---- mkdir -p over WORK_DIR — THE FIX ----
    const size_t wdLen = strlen(gp->m_workDir);
    for (size_t i = 1; i < wdLen; ++i)
    {
        if (gp->m_workDir[i] == '\\')
        {
            memcpy(prefix, gp->m_workDir, i);
            prefix[i] = '\0';                       // <<< original omits this
            CreateDirectoryA(prefix, nullptr);
        }
    }

    // ---- switch CWD to workDir, create it if missing ----
    if (!SetCurrentDirectoryA(gp->m_workDir))
    {
        if (!CreateDirectoryA(gp->m_workDir, nullptr))
        {
            FatalBox()(L(STR_ADRESAR), workDir);
            return 0;
        }
        SetFileAttributesA(workDir, FILE_ATTRIBUTE_NORMAL);
    }

    // ---- ensure trailing backslashes (engine expects "X:\path\" format) ----
    n = strlen(gp->m_runDir);
    if (n > 0 && gp->m_runDir[n - 1] != '\\')
    {
        gp->m_runDir = DupStr()(gp->m_runDir, lstrlenA(gp->m_runDir) + 2);
        lstrcatA(gp->m_runDir, "\\");
    }
    n = strlen(gp->m_workDir);
    if (n > 0 && gp->m_workDir[n - 1] != '\\')
    {
        gp->m_workDir = DupStr()(gp->m_workDir, lstrlenA(gp->m_workDir) + 2);
        lstrcatA(gp->m_workDir, "\\");
        lstrcatA(gp->m_workDirCopy, "\\");
    }
    return 1;
}