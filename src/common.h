#pragma once
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Config {
    char aspect[32] = "Auto";
    char resolution[32] = "Auto";
    int maxFps = 60;
    int noVsyncWait = 1;
    int videoMemoryCheck = 1;
    int retestAccelerator = 1;
    int noDisc = 1;
    int findDisc = 1;
    int runWithoutSetup = 1;
    int clock = 1;
    int portalCheck = 1;
    int log = 1;
    int diagnostics = 0;
};
extern Config cfg;
extern char g_dir[MAX_PATH];        // game folder, with a trailing backslash

void Log(const char* fmt, ...);

// ---- patching ---------------------------------------------------------------
bool BytesMatch(uintptr_t va, const char* hexPattern);          // "55 8b ?? ec"
bool WriteMem(uintptr_t va, const void* src, size_t n);
// Writes `bytes` if the current bytes match `expect`.
bool Patch(const char* name, uintptr_t va, const char* expect, const char* bytes);
// Replaces `n` bytes with a call (0xE8) or jump (0xE9) to `to`, padded with nops.
bool Redirect(const char* name, uintptr_t va, const char* expect, int n, uint8_t op, const void* to);
// Import hook on the game; returns the previous pointer.
void* HookIAT(const char* dll, const char* fn, void* to);

template <class T> inline T& At(uintptr_t a) { return *reinterpret_cast<T*>(a); }

// ---- game (HTruck.exe of 6 April 1999, fixed base) ------------------------------
constexpr DWORD GAME_STAMP = 0x3709FF07;
constexpr DWORD GAME_ENTRY = 0x10E268;
#define D3D_DEVICE 0x5701B8         // IDirect3DDevice3*, null in the software modes

// ---- features -----------------------------------------------------------------
bool TimingInit();                  // true if the frame limiter is installed
void RenderInit(bool limiter);
void GameFilesInit();
