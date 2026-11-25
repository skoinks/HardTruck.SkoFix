// Graphics: accelerator checks, portal stack, widescreen and the dgVoodoo2 settings that go with it.
#include "common.h"

static float g_wide = 1.0f;         // aspect ratio relative to 4:3

// 452F9A: fdiv [eax+58h] / mov esi,3F800000h. Horizontal scale of a camera's projection.
__declspec(naked) static void LensHook() {
    __asm {
        fdiv dword ptr [eax+0x58]
        cmp dword ptr ds:[D3D_DEVICE], 0
        je done
        fdiv dword ptr [g_wide]
    done:
        mov esi, 0x3F800000
        ret
    }
}
// 452854: mov eax,[ebp-38h] / fld [eax+58h] / fmulp st(2),st. Sky backdrop.
__declspec(naked) static void SkyHook() {
    __asm {
        mov eax, dword ptr [ebp-0x38]
        fld dword ptr [eax+0x58]
        cmp dword ptr ds:[D3D_DEVICE], 0
        je done
        fmul dword ptr [g_wide]
    done:
        fmulp st(2), st
        ret
    }
}

// 43A82C: mov edx,[ebp-1Ch] / mov edx,[edx+1F20h]. The stack has 20 entries, the other path stops at 18.
static const uintptr_t PORTAL_OK = 0x43A835, PORTAL_FULL = 0x43ABB2;
__declspec(naked) static void PortalHook() {
    __asm {
        mov edx, dword ptr [ebp-0x1C]
        mov edx, dword ptr [edx+0x1F20]
        cmp edx, 0x12
        jge full
        jmp dword ptr [PORTAL_OK]
    full:
        jmp dword ptr [PORTAL_FULL]
    }
}

// ---- dgVoodoo2 -----------------------------------------------------------------
static bool Parse(const char* s, int* w, int* h) {
    return (sscanf(s, " h:%d, v:%d", w, h) == 2 || sscanf(s, "%d x %d", w, h) == 2) &&
           *w >= 320 && *h >= 200 && *w <= 16384 && *h <= 16384;
}
static void AutoResolution(int* w, int* h) {
    DEVMODEA m = {};
    m.dmSize = sizeof m;
    int sw = 1920, sh = 1080;
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &m) && m.dmPelsWidth && m.dmPelsHeight) {
        sw = m.dmPelsWidth;
        sh = m.dmPelsHeight;
    }
    double aspect = (double)sw / sh;
    int ax, ay;
    if (sscanf(cfg.aspect, "%d:%d", &ax, &ay) == 2 && ax > 0 && ay > 0) aspect = (double)ax / ay;
    if (aspect < 4.0 / 3) aspect = 4.0 / 3;
    if (aspect > 32.0 / 9) aspect = 32.0 / 9;
    // whole multiples of 800x600 keep the 2D parts sharp
    double wide = aspect * 3 / 4;
    for (int b = (sh + 599) / 600; b * 600 <= 2400 && b * 600 <= sh * 1.7; b++) {
        int a = (int)(b * wide + 0.5);
        if (fabs((double)a / b / wide - 1) < 0.005) { *w = a * 800; *h = b * 600; return; }
    }
    *h = sh & ~1;
    *w = (int)(*h * aspect + 0.5) & ~1;
}
// Finds the value of Resolution in [DirectX].
static char* ResolutionValue(char* text, size_t* len) {
    char* s = strstr(text, "\n[DirectX]");
    if (!s) return nullptr;
    char* end = strstr(s + 2, "\n[");
    for (char* p = s; (p = strstr(p, "\nResolution")) != nullptr && (!end || p < end); p++) {
        char* eq = p + 11;
        eq += strspn(eq, " \t");
        if (*eq != '=') continue;
        char* v = eq + 1 + strspn(eq + 1, " \t");
        *len = strcspn(v, "\r\n");
        return v;
    }
    return nullptr;
}
static bool InGameFolder(char* path, const char* name) {
    _snprintf(path, MAX_PATH - 1, "%s%s", g_dir, name);
    path[MAX_PATH - 1] = 0;
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}
// Makes dgVoodoo.conf match the settings. Returns the resolution it ends up with.
static bool SyncWrapper(int* w, int* h) {
    char path[MAX_PATH];
    if (!InGameFolder(path, "DDraw.dll") || !InGameFolder(path, "dgVoodoo.conf")) {
        Log("[wrapper] dgVoodoo2 not found, widescreen off");
        return false;
    }
    FILE* f = fopen(path, "rb");
    if (!f) { Log("[wrapper] dgVoodoo.conf could not be read, widescreen off"); return false; }
    static char text[128 * 1024];
    size_t n = fread(text, 1, sizeof text - 1, f);
    bool whole = feof(f) && !ferror(f);
    fclose(f);
    text[n] = 0;
    size_t len = 0;
    char* v = whole ? ResolutionValue(text, &len) : nullptr;
    if (!v) { Log("[wrapper] no Resolution in dgVoodoo.conf, widescreen off"); return false; }
    char cur[64] = {};
    strncpy(cur, v, len < sizeof cur - 1 ? len : sizeof cur - 1);

    if (!_stricmp(cfg.resolution, "Off") || !strcmp(cfg.resolution, "0")) {
        if (Parse(cur, w, h)) return true;
        Log("[wrapper] resolution %s has no fixed shape, widescreen off", cur);
        return false;
    }
    if (!Parse(cfg.resolution, w, h)) AutoResolution(w, h);
    char want[64];
    size_t wantLen = (size_t)sprintf(want, "h:%d, v:%d", *w, *h);
    if (!strcmp(want, cur)) return true;
    size_t head = v - text, tail = n - head - len;
    char temp[MAX_PATH];
    InGameFolder(temp, "dgVoodoo.conf.new");
    f = fopen(temp, "wb");
    bool ok = f != nullptr;
    if (f) {
        ok = fwrite(text, 1, head, f) == head && fwrite(want, 1, wantLen, f) == wantLen && fwrite(v + len, 1, tail, f) == tail;
        ok = fclose(f) == 0 && ok;
        ok = ok && MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING);
        if (!ok) DeleteFileA(temp);
    }
    Log("[wrapper] resolution %s -> %s %s", cur, want, ok ? "ok" : "could not be saved");
    return ok || Parse(cur, w, h);
}

void RenderInit(bool limiter) {
    // free video memory compared as a signed number: jge -> jae
    if (cfg.videoMemoryCheck) Patch("video memory check", 0x412A9F, "7d 18", "73 18");
    // Flip(DDFLIP_WAIT) -> Flip(DDFLIP_WAIT | DDFLIP_NOVSYNC)
    if (cfg.noVsyncWait && limiter) Patch("no vsync wait", 0x410C7C, "6a 01", "6a 09");
    if (cfg.portalCheck)
        Redirect("portal check", 0x43A82C, "8b 55 e4 8b 92 20 1f 00 00", 9, 0xE9, (void*)PortalHook);

    int w = 0, h = 0;
    if (!SyncWrapper(&w, &h)) return;
    g_wide = ((float)w / h) * 0.75f;
    Log("[wrapper] %dx%d, %.3f times as wide as 4:3", w, h, g_wide);
    if (g_wide < 0.99f || g_wide >= 3.0f) Log("[wrapper] shape not supported, the picture is stretched");
    if (g_wide > 1.01f && g_wide < 3.0f &&
        Redirect("widescreen lens", 0x452F9A, "d8 70 58 be 00 00 80 3f", 8, 0xE8, (void*)LensHook))
        Redirect("widescreen sky", 0x452854, "8b 45 c8 d9 40 58 de ca", 8, 0xE8, (void*)SkyHook);
}
