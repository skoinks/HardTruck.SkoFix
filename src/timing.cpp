// Clock and frame pacing.
#include "common.h"
#include <mmsystem.h>

static double g_msPerTick;
static LONGLONG g_base;
static bool g_clock;
static double g_minFrame;
static volatile LONG g_frames;

static double NowMs() {
    if (!g_clock) return timeGetTime();
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return (double)(q.QuadPart - g_base) * g_msPerTick;
}

// Replaces the game's clock (4231C0): milliseconds in st(0), all registers preserved.
static double __cdecl ClockImpl() { return NowMs(); }
__declspec(naked) static void ClockHook() {
    __asm {
        pushad
        call ClockImpl
        popad
        ret
    }
}

// First clock read of a view's frame update, ebx = view.
//   +270 double  time of the previous frame, seconds (rounded to float by the game)
//   +B74 int     fixed step in milliseconds, or 0
static double __cdecl FrameImpl(uint8_t* view) {
    double prev = At<double>((uintptr_t)view + 0x270);
    if (g_minFrame > 0 && At<int>((uintptr_t)view + 0xB74) <= 0) {
        for (;;) {
            double d = NowMs() / 1000.0 - prev;
            if (!(d >= -0.1 && d < g_minFrame)) break;      // also ends on NaN and after a clock jump
            if (g_minFrame - d > 0.003) Sleep(1);
            else YieldProcessor();
        }
    }
    InterlockedIncrement(&g_frames);
    return NowMs();
}
__declspec(naked) static void FrameHook() {
    __asm {
        pushad
        push ebx
        call FrameImpl
        add esp, 4
        popad
        ret
    }
}

static DWORD WINAPI Diagnostics(LPVOID) {
    for (;;) {
        Sleep(10000);
        DEVMODEA m = {};
        m.dmSize = sizeof m;
        EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &m);
        Log("[diag] %.1f frames/s, display %lux%lux%lu %lu Hz, accelerator %s", InterlockedExchange(&g_frames, 0) / 10.0,
            m.dmPelsWidth, m.dmPelsHeight, m.dmBitsPerPel, m.dmDisplayFrequency, At<void*>(D3D_DEVICE) ? "on" : "off");
    }
}

bool TimingInit() {
    LARGE_INTEGER f, q;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q);
    g_msPerTick = 1000.0 / (double)f.QuadPart;
    g_base = q.QuadPart - 1000 * f.QuadPart;    // starts at 1000 s: the game's timers start at 0 and expect an older clock
    timeBeginPeriod(1);

    if (cfg.clock)
        g_clock = Redirect("clock", 0x4231C0, "55 89 e5 50 51", 5, 0xE9, (void*)ClockHook);
    if (cfg.maxFps > 0) {
        int fps = cfg.maxFps < 10 ? 10 : cfg.maxFps > 200 ? 200 : cfg.maxFps;
        g_minFrame = 1.0 / fps;
    }
    bool hooked = false;
    if (cfg.maxFps > 0 || cfg.diagnostics)
        hooked = Redirect("frame limiter", 0x429261, "e8 5a 9f ff ff", 5, 0xE8, (void*)FrameHook);
    if (cfg.diagnostics) {
        HANDLE t = CreateThread(nullptr, 0, Diagnostics, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return hooked && cfg.maxFps > 0;
}
