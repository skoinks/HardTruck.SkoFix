// Answers to the game's questions about truck.ini: disc drive, installation, stored accelerator tests.
#include "common.h"

typedef DWORD(WINAPI* GetProfile)(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);
static GetProfile g_real;

// ---- disc ------------------------------------------------------------------------
static bool HasDisc(const char* root) {
    char p[MAX_PATH];
    if (!*root || strlen(root) > MAX_PATH - 8) return false;
    sprintf(p, "%s\\disk1", root);
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);       // no "insert a disc" message for empty drives
    bool ok = GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
    SetErrorMode(mode);
    return ok;
}
static bool FindDisc(char* drive) {
    DWORD m = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(m >> i & 1)) continue;
        char root[4] = { (char)('A' + i), ':', '\\', 0 };
        if (GetDriveTypeA(root) != DRIVE_CDROM) continue;
        root[2] = 0;
        if (HasDisc(root)) { strcpy(drive, root); return true; }
    }
    return false;
}
static bool Disc(char* drive) {
    static char last[4];
    if (*last && HasDisc(last)) { strcpy(drive, last); return true; }
    if (!FindDisc(drive)) return false;
    strcpy(last, drive);
    Log("[disc] %s", drive);
    return true;
}

// ---- truck.ini -------------------------------------------------------------------
static DWORD Answer(const char* s, char* buf, DWORD size) {
    strncpy(buf, s, size - 1);
    buf[size - 1] = 0;
    return (DWORD)strlen(buf);
}
static bool NotInstalled(const char* file) {
    static int state = -1;
    if (state < 0) {
        char home[64];
        g_real("INSTALL", "home", "", home, sizeof home, file);
        state = !_stricmp(home, "__noinst");
        if (state) {
            Log("[game] not installed, running from its own folder");
            DWORD attr = GetFileAttributesA(file);      // read-only when copied from a disc
            if (attr != INVALID_FILE_ATTRIBUTES && attr & FILE_ATTRIBUTE_READONLY) SetFileAttributesA(file, attr & ~FILE_ATTRIBUTE_READONLY);
        }
    }
    return state != 0;
}
static DWORD WINAPI GetProfileHook(LPCSTR sec, LPCSTR key, LPCSTR def, LPSTR buf, DWORD size, LPCSTR file) {
    DWORD n = g_real(sec, key, def, buf, size, file);
    if (!sec || !key || !buf || !size) return n;
    if (!_stricmp(sec, "INSTALL")) {
        if (!_stricmp(key, "SOURCE")) {
            char drive[4];
            if (cfg.findDisc && !HasDisc(buf) && Disc(drive)) return Answer(drive, buf, size);
        } else if (cfg.runWithoutSetup && NotInstalled(file)) {
            if (!_stricmp(key, "home") || !_stricmp(key, "base") || !_stricmp(key, "movie")) return Answer(".", buf, size);
            if (!_stricmp(key, "setup")) return Answer("on", buf, size);
            if (!_stricmp(key, "mode")) return Answer("maximum", buf, size);
        }
    } else if (cfg.retestAccelerator && cfg.videoMemoryCheck && !_stricmp(sec, "EFFECTS") && strlen(key) == 4 && !_strnicmp(key, "vpa", 3) && !strcmp(buf, "2")) {
        // 2 = failed, 1 = no such display mode. The game tests again if there is no stored result.
        return Answer(def ? def : "", buf, size);
    }
    return n;
}

void GameFilesInit() {
    // looks for disk1 on the disc: return 1
    if (cfg.noDisc) Patch("no disc", 0x4E2AB8, "55 89 e5 53 51 56", "b8 01 00 00 00 c3");
    if (!cfg.findDisc && !cfg.runWithoutSetup && !cfg.retestAccelerator) return;
    g_real = GetPrivateProfileStringA;
    auto old = (GetProfile)HookIAT("KERNEL32.dll", "GetPrivateProfileStringA", (void*)GetProfileHook);
    if (old) g_real = old;
    Log("[hook] truck.ini %s", old ? "ok" : "failed");
}
