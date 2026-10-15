#pragma once
// ============================================================================
// LoadCaching.h
//
// Memoizing detour for the Bloodline.exe resource resolver @ VA 0x4EA5A3:
//     int __cdecl ResolvePath(char* name, char* outDir, int type)
//         name   (in/out) — mutated in place (extension gets appended)
//         outDir (out)    — directory the resource was found in
//         type   (in)     — 0 = textures (.dds/.tga/.tif/.avi), 1 = sounds (.wav)
//
// The original does a recursive FindFirstFile walk of the game tree per
// lookup (Textures\ = 2,527 files / 1,434 dirs). This detour records each
// (type, name, resolver-cycle-state) -> (ret, outDir, mutated name) and
// replays it on the next identical call, helping speed up the loading
// ============================================================================
#include <windows.h>
#include <string>
#include <unordered_map>

typedef int(__cdecl *BlResolvePath_t)(char *name, char *outDir, int type);

extern "C" int __cdecl BlResolvePath_Hook(char *name, char *outDir, int type);

class LoadCaching
{
public:
    static LoadCaching &Instance();

    static constexpr uintptr_t RESOLVER_VA = 0x4EA5A3;
    static constexpr uintptr_t EXT_CYCLE_GVA = 0x742B6C; // resolver's global "next extension" index
    static constexpr int      HOOK_OVERRIDE_LEN = 9;       // 55 | 8B EC | 81 EC 10 04 00 00

    // Patch the resolver to jump to BlResolvePath_Hook and keep a callable
    // trampoline of the original for cache misses. Idempotent.
    bool Install();

    // Drop all cached outcomes (e.g. after installing a mod mid-session).
    void Clear();

    // Optional DebugView logging of every lookup.
    void SetTrace(bool on) { m_trace = on; }

    unsigned Hits()   const { return m_hits; }
    unsigned Misses() const { return m_misses; }
    size_t   Entries() const { return m_cache.size(); }

    // Called by BlResolvePath_Hook. Public so the hook needs no friendship.
    int Resolve(char *name, char *outDir, int type);

private:
    LoadCaching();
    ~LoadCaching();
    LoadCaching(const LoadCaching &);            // not copyable
    LoadCaching &operator=(const LoadCaching &); // not assignable

    struct Outcome {
        int         ret;       // exact return value of the original
        std::string outDir;    // what the original left in outDir
        std::string nameAfter; // the name as the original left it (ext appended)
    };

    static int ExtCycleState(); // reads the resolver global at 0x742B6C

    BlResolvePath_t m_original = nullptr;
    bool            m_installed = false;
    bool            m_trace = false;
    unsigned        m_hits = 0;
    unsigned        m_misses = 0;
    std::unordered_map<std::string, Outcome> m_cache;
    CRITICAL_SECTION m_lock;
};