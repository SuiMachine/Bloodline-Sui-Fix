#include "LoadCaching.h"
#include "hookFunctions.h"

extern "C" int __cdecl BlResolvePath_Hook(char *name, char *outDir, int type)
{
    return LoadCaching::Instance().Resolve(name, outDir, type);
}

LoadCaching &LoadCaching::Instance()
{
    static LoadCaching s_instance;
    return s_instance;
}

LoadCaching::LoadCaching() { InitializeCriticalSection(&m_lock); }
LoadCaching::~LoadCaching() { DeleteCriticalSection(&m_lock); }
bool LoadCaching::Install()
{
    if (m_installed)
        return true;
    if ((uintptr_t)GetModuleHandleA(nullptr) != 0x400000)
        return false;                       // image relocated; absolute VAs invalid

    // Hook() overwrites the first HOOK_OVERRIDE_LEN bytes of the resolver,
    // so the trampoline must re-execute them before jumping back. All 9
    // (push ebp | mov ebp,esp | sub esp,0x410) are position-independent,
    // so a flat copy followed by a jmp is a valid trampoline.
    BYTE *const trampoline = (BYTE *)VirtualAlloc(nullptr,
        HOOK_OVERRIDE_LEN + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline)
        return false;

    CopyMemory(trampoline, (void *)RESOLVER_VA, HOOK_OVERRIDE_LEN);
    trampoline[HOOK_OVERRIDE_LEN] = 0xE9;   // jmp RESOLVER_VA + HOOK_OVERRIDE_LEN
    *(DWORD *)(trampoline + HOOK_OVERRIDE_LEN + 1) =
        (DWORD)(RESOLVER_VA + HOOK_OVERRIDE_LEN) - (DWORD)(trampoline + HOOK_OVERRIDE_LEN + 5);

    Hook(RESOLVER_VA, (void *)&BlResolvePath_Hook, HOOK_OVERRIDE_LEN);

    m_original = (BlResolvePath_t)trampoline;
    m_installed = true;
    return true;
}

// The resolver image is verified to sit at its preferred base 0x400000 in
// Install(), so the absolute address is safe to read directly.
int LoadCaching::ExtCycleState()
{
    return *(int *)EXT_CYCLE_GVA;
}

void LoadCaching::Clear()
{
    EnterCriticalSection(&m_lock);
    m_cache.clear();
    LeaveCriticalSection(&m_lock);
}

int LoadCaching::Resolve(char *name, char *outDir, int type)
{
    // Not installed yet (shouldn't happen — hook only goes live at the end
    // of Install) — fail loudly rather than fake a result.
    if (!m_installed || !m_original)
    {
        OutputDebugStringA("[BlResolver] Resolve called before Install!\n");
        return 0;
    }
    if (!name || !outDir)
        return m_original(name, outDir, type);   // degenerate: behave exactly

    // Key: type | resolver extension-cycle state | name as passed in.
    // The cycle state is part of the original's observable behavior for
    // extension-less names (it decides which extension gets tried/appended
    // first), so it belongs in the key.
    std::string key;
    key.reserve(64 + lstrlenA(name));
    key += (char)('0' + type);
    key += '\x1F';
    {
        char st[16];
        wsprintfA(st, "%d", ExtCycleState());
        key += st;
    }
    key += '\x1F';
    key += name;

    EnterCriticalSection(&m_lock);

    auto it = m_cache.find(key);
    if (it != m_cache.end())
    {
        const Outcome &o = it->second;
        // Replay: same call site => same buffer capacities as when the
        // outcome was recorded, so these copies cannot overflow.
        lstrcpyA(outDir, o.outDir.c_str());
        lstrcpyA(name, o.nameAfter.c_str());
        ++m_hits;
        LeaveCriticalSection(&m_lock);

        if (m_trace)
        {
            char msg[512];
            wsprintfA(msg, "[LoadCaching] HIT  %s -> %s (ret=%d)\n", o.nameAfter.c_str(), o.outDir.c_str(), o.ret);
            OutputDebugStringA(msg);
        }
        return o.ret;
    }

    ++m_misses;
    LeaveCriticalSection(&m_lock);      // don't hold the lock during the
    // original's (long) directory walk
    const int ret = m_original(name, outDir, type);

    Outcome o;
    o.ret = ret;
    o.outDir = outDir;               // post-call state, verbatim
    o.nameAfter = name;              // post-mutation, verbatim

    if (m_trace)
    {
        char msg[512];
        wsprintfA(msg, "[LoadCaching] MISS %s -> %s (ret=%d)\n", o.nameAfter.c_str(), o.outDir.c_str(), o.ret);
        OutputDebugStringA(msg);
    }

    EnterCriticalSection(&m_lock);
    m_cache.emplace(std::move(key), std::move(o));
    LeaveCriticalSection(&m_lock);
    return ret;
}