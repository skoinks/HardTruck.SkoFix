// Start-up, configuration, patching and dinput forwarding.
#include "common.h"
#include "version.h"
#include <stdarg.h>

Config cfg;
char g_dir[MAX_PATH];
static char g_ini[MAX_PATH];
static FILE* g_log;

void Log(const char* fmt, ...) {
    if (!g_log) return;
    char line[1024];
    va_list a;
    va_start(a, fmt);
    _vsnprintf(line, sizeof line - 1, fmt, a);
    line[sizeof line - 1] = 0;
    va_end(a);
    fprintf(g_log, "%8u %s\n", (unsigned)GetTickCount(), line);     // one call, so that threads do not mix lines
    fflush(g_log);
}

// ---- patching -------------------------------------------------------------------
static bool Readable(uintptr_t va, size_t n) {
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery((void*)va, &m, sizeof m) && m.State == MEM_COMMIT &&
           !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
           va + n <= (uintptr_t)m.BaseAddress + m.RegionSize;
}
static int Hex(const char* s, int* out, int max) {      // -1 for ??
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ') s++;
        if (!*s) break;
        char h[3] = { s[0], s[1], 0 };
        out[n++] = *s == '?' ? -1 : (int)strtoul(h, nullptr, 16);
        s += 2;
    }
    return n;
}
bool BytesMatch(uintptr_t va, const char* p) {
    int b[32], n = Hex(p, b, 32);
    if (!n || !Readable(va, n)) return false;
    for (int i = 0; i < n; i++)
        if (b[i] >= 0 && At<uint8_t>(va + i) != b[i]) return false;
    return true;
}
bool WriteMem(uintptr_t va, const void* src, size_t n) {
    DWORD o;
    if (!VirtualProtect((void*)va, n, PAGE_EXECUTE_READWRITE, &o)) return false;
    memcpy((void*)va, src, n);
    VirtualProtect((void*)va, n, o, &o);
    FlushInstructionCache(GetCurrentProcess(), (void*)va, n);
    return true;
}
static bool Apply(const char* name, uintptr_t va, const char* expect, const uint8_t* bytes, int n) {
    if (!BytesMatch(va, expect)) { Log("[patch] %s at %08x: bytes differ, skipped", name, (unsigned)va); return false; }
    bool ok = WriteMem(va, bytes, n);
    Log("[patch] %s at %08x %s", name, (unsigned)va, ok ? "ok" : "failed");
    return ok;
}
bool Patch(const char* name, uintptr_t va, const char* expect, const char* bytes) {
    int b[32], n = Hex(bytes, b, 32);
    uint8_t out[32];
    for (int i = 0; i < n; i++) out[i] = (uint8_t)b[i];
    return Apply(name, va, expect, out, n);
}
bool Redirect(const char* name, uintptr_t va, const char* expect, int n, uint8_t op, const void* to) {
    uint8_t out[32];
    if (n < 5 || n > 32) return false;
    memset(out, 0x90, sizeof out);
    out[0] = op;
    int32_t rel = (int32_t)((uintptr_t)to - (va + 5));
    memcpy(out + 1, &rel, 4);
    return Apply(name, va, expect, out, n);
}
void* HookIAT(const char* dll, const char* fn, void* to) {
    auto b = (uint8_t*)GetModuleHandleA(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(b + nt->OptionalHeader.DataDirectory[1].VirtualAddress);
    for (; imp->Name; imp++) {
        if (_stricmp((char*)b + imp->Name, dll) || !imp->OriginalFirstThunk) continue;
        auto names = (uintptr_t*)(b + imp->OriginalFirstThunk);
        auto iat = (uintptr_t*)(b + imp->FirstThunk);
        for (; *names; names++, iat++) {
            if (*names & 0x80000000) continue;
            if (strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(b + *names))->Name, fn)) continue;
            void* old = (void*)*iat;
            return WriteMem((uintptr_t)iat, &to, sizeof to) ? old : nullptr;
        }
    }
    return nullptr;
}

// ---- configuration ----------------------------------------------------------------
static void Int(const char* sec, const char* key, int* v) { *v = (int)GetPrivateProfileIntA(sec, key, *v, g_ini); }
static void Str(const char* sec, const char* key, char* v, DWORD n) {
    char def[MAX_PATH];
    strncpy(def, v, MAX_PATH - 1);
    def[MAX_PATH - 1] = 0;
    GetPrivateProfileStringA(sec, key, def, v, n, g_ini);
}
static void LoadConfig() {
    Str("Graphics", "Aspect", cfg.aspect, sizeof cfg.aspect);
    Str("Graphics", "Resolution", cfg.resolution, sizeof cfg.resolution);
    Int("Graphics", "MaxFps", &cfg.maxFps);
    Int("Graphics", "NoVsyncWait", &cfg.noVsyncWait);
    Int("Graphics", "VideoMemoryCheck", &cfg.videoMemoryCheck);
    Int("Graphics", "RetestAccelerator", &cfg.retestAccelerator);
    Int("Disc", "NoDisc", &cfg.noDisc);
    Int("Disc", "FindDisc", &cfg.findDisc);
    Int("Disc", "RunWithoutSetup", &cfg.runWithoutSetup);
    Int("General", "Clock", &cfg.clock);
    Int("General", "PortalCheck", &cfg.portalCheck);
    Int("General", "Log", &cfg.log);
    Int("General", "Diagnostics", &cfg.diagnostics);
}

static void Start(HMODULE self) {
    wchar_t w[MAX_PATH], sw[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, w, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    BOOL lossy = FALSE;
    if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, g_dir, MAX_PATH, nullptr, &lossy)) return;
    if (lossy) {        // short names for folders outside of the ANSI code page
        n = GetShortPathNameW(w, sw, MAX_PATH);
        if (!n || n >= MAX_PATH || !WideCharToMultiByte(CP_ACP, 0, sw, -1, g_dir, MAX_PATH, nullptr, nullptr)) return;
    }
    char* s = strrchr(g_dir, '\\');
    if (!s || s - g_dir > MAX_PATH - 32) return;
    s[1] = 0;
    sprintf(g_ini, "%sHardTruck.SkoFix.ini", g_dir);
    LoadConfig();
    if (cfg.log) {
        char p[MAX_PATH];
        sprintf(p, "%sHardTruck.SkoFix.log", g_dir);
        g_log = fopen(p, "w");
    }
    Log("Hard Truck SkoFix " VERSION);

    auto b = (uint8_t*)GetModuleHandleA(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    DWORD entry = nt->OptionalHeader.AddressOfEntryPoint, stamp = nt->FileHeader.TimeDateStamp;
    if ((uintptr_t)b != 0x400000 || entry != GAME_ENTRY || stamp != GAME_STAMP) {
        Log("[game] unknown version (base %p, entry %08lx, stamp %08lx), nothing changed", b, entry, stamp);
        return;
    }
    RenderInit(TimingInit());
    GameFilesInit();
}

// ---- dinput -------------------------------------------------------------------------
static FARPROC Real(const char* name) {
    static HMODULE m;
    if (!m) {
        char p[MAX_PATH];
        UINT n = GetSystemDirectoryA(p, MAX_PATH - 16);
        if (!n || n > MAX_PATH - 16) return nullptr;
        strcat(p, "\\dinput.dll");
        m = LoadLibraryA(p);
        if (!m) Log("[dinput] the system's dinput.dll could not be loaded (%lu)", GetLastError());
    }
    return m ? GetProcAddress(m, name) : nullptr;
}
extern "C" HRESULT WINAPI Proxy_DirectInputCreateA(HINSTANCE h, DWORD v, void** d, void* u) {
    auto f = (HRESULT(WINAPI*)(HINSTANCE, DWORD, void**, void*))Real("DirectInputCreateA");
    return f ? f(h, v, d, u) : E_FAIL;
}
extern "C" HRESULT WINAPI Proxy_DirectInputCreateW(HINSTANCE h, DWORD v, void** d, void* u) {
    auto f = (HRESULT(WINAPI*)(HINSTANCE, DWORD, void**, void*))Real("DirectInputCreateW");
    return f ? f(h, v, d, u) : E_FAIL;
}
extern "C" HRESULT WINAPI Proxy_DirectInputCreateEx(HINSTANCE h, DWORD v, const GUID& i, void** d, void* u) {
    auto f = (HRESULT(WINAPI*)(HINSTANCE, DWORD, const GUID&, void**, void*))Real("DirectInputCreateEx");
    return f ? f(h, v, i, d, u) : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        Start(h);
    }
    return TRUE;
}
