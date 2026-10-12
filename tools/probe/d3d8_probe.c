// Diagnostic d3d8.dll proxy: logs device setup, frame rate, Present cost and the game's timer fields.
// Build: zig cc -target x86-windows-gnu -O2 -shared -o d3d8.dll d3d8_probe.c d3d8.def -lwinmm
// Put it next to tsu_han.exe; it writes probe.log. Remove it for normal play.
// PROBE_RGN=none|rect|layered (environment variable) changes what happens to the shaped window's region.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <mmsystem.h>
#include <d3d8.h>

static HMODULE real_d3d8;
static FILE *logf;
static IDirect3D8 *(WINAPI *real_create)(UINT);

static HRESULT (STDMETHODCALLTYPE *real_create_device)(IDirect3D8 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice8 **);
static HRESULT (STDMETHODCALLTYPE *real_get_caps)(IDirect3D8 *, UINT, D3DDEVTYPE, D3DCAPS8 *);
static HRESULT (STDMETHODCALLTYPE *real_present)(IDirect3DDevice8 *, const RECT *, const RECT *, HWND, const RGNDATA *);
static HRESULT (STDMETHODCALLTYPE *real_reset)(IDirect3DDevice8 *, D3DPRESENT_PARAMETERS *);
static HRESULT (STDMETHODCALLTYPE *real_create_vs)(IDirect3DDevice8 *, const DWORD *, const DWORD *, DWORD *, DWORD);

static volatile LONG sleep_calls, sleep_ms;
static VOID (WINAPI *real_sleep)(DWORD);
static VOID WINAPI hook_sleep(DWORD ms)
{
    InterlockedIncrement(&sleep_calls);
    InterlockedExchangeAdd(&sleep_ms, ms);
    real_sleep(ms);
}

static void hook_iat(const char *dll, const char *func, void *hook, void **orig)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; imp->Name; imp++) {
        if (lstrcmpiA((char *)(base + imp->Name), dll)) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA *addrs = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++) {
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, func)) continue;
            DWORD old;
            VirtualProtect(&addrs->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            *orig = (void *)addrs->u1.Function;
            addrs->u1.Function = (DWORD_PTR)hook;
            VirtualProtect(&addrs->u1.Function, sizeof(void *), old, &old);
        }
    }
}

static void logp(const char *fmt, ...)
{
    if (!logf) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fflush(logf);
}

static void patch(void **vtbl, int idx, void *hook, void **orig)
{
    DWORD old;
    if (vtbl[idx] == hook) return;
    VirtualProtect(&vtbl[idx], sizeof(void *), PAGE_EXECUTE_READWRITE, &old);
    *orig = vtbl[idx];
    vtbl[idx] = hook;
    VirtualProtect(&vtbl[idx], sizeof(void *), old, &old);
}

static void log_pp(const char *tag, D3DPRESENT_PARAMETERS *pp)
{
    logp("%s: %ux%u fmt=%d count=%u ms=%d swap=%d hwnd=%p windowed=%d autodepth=%d depthfmt=%d flags=%#x refresh=%u interval=%#x\n",
         tag, pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->BackBufferCount,
         pp->MultiSampleType, pp->SwapEffect, pp->hDeviceWindow, pp->Windowed, pp->EnableAutoDepthStencil,
         pp->AutoDepthStencilFormat, pp->Flags, pp->FullScreen_RefreshRateInHz, pp->FullScreen_PresentationInterval);
}


static HRESULT STDMETHODCALLTYPE hook_present(IDirect3DDevice8 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty)
{
    static LARGE_INTEGER freq, start;
    static unsigned frames;
    static double present_ms;
    LARGE_INTEGER now, t0;
    QueryPerformanceCounter(&t0);
    HRESULT hr = real_present(dev, src, dst, wnd, dirty);

    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start); }
    frames++;
    QueryPerformanceCounter(&now);
    if (freq.QuadPart) present_ms += (double)(now.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
    double secs = (double)(now.QuadPart - start.QuadPart) / freq.QuadPart;
    if (secs >= 2.0) {
        logp("fps %.1f present avg %.2fms (timeGetTime=%lu) sleeps=%ld total=%ldms\n", frames / secs, present_ms / frames, timeGetTime(),
             InterlockedExchange(&sleep_calls, 0), InterlockedExchange(&sleep_ms, 0));
        // Find the game's graphics object (device pointer at +8) on the stack and dump its timing fields.
        static BYTE *gfx;
        if (!gfx) {
            DWORD *sp = (DWORD *)&now;
            for (int i = 0; i < 1024 && !gfx; i++) {
                BYTE *p = (BYTE *)sp[i];
                if (p > (BYTE *)0x10000 && !IsBadReadPtr(p, 0x140) && *(IDirect3DDevice8 **)(p + 8) == dev)
                    gfx = p;
            }
        }
        if (gfx) {
            unsigned short cw;
            __asm__ volatile("fnstcw %0" : "=m"(cw));
            logp("  gfx=%p windowed=%d mode=%lu lastX=%f fpsmeas=%f frametime=%f accum=%f timer.start=%f fpucw=%#x\n", gfx,
                 gfx[0x18], *(DWORD *)(gfx + 0xdc), *(float *)(gfx + 0xec), *(float *)(gfx + 0x128),
                 *(float *)(gfx + 0x12c), *(float *)(gfx + 0x130), *(float *)(gfx + 0xe8), cw);
        }
        frames = 0;
        present_ms = 0;
        start = now;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_reset(IDirect3DDevice8 *dev, D3DPRESENT_PARAMETERS *pp)
{
    log_pp("Reset", pp);
    HRESULT hr = real_reset(dev, pp);
    logp("Reset -> %#lx\n", hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_create_vs(IDirect3DDevice8 *dev, const DWORD *decl, const DWORD *func, DWORD *handle, DWORD usage)
{
    static int n;
    HRESULT hr = real_create_vs(dev, decl, func, handle, usage);
    logp("CreateVertexShader #%d func=%p usage=%#lx -> %#lx handle=%#lx\n", n, func, usage, hr, handle ? *handle : 0);
    if (func) {
        char name[64];
        const DWORD *p = func;
        while (*p != 0x0000FFFF) p++;
        sprintf(name, "probe_vs%02d.vso", n);
        FILE *f = fopen(name, "wb");
        if (f) { fwrite(func, 4, (p - func) + 1, f); fclose(f); }
    }
    n++;
    return hr;
}

static HRESULT (STDMETHODCALLTYPE *real_check_format)(IDirect3D8 *, UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT);
static HRESULT STDMETHODCALLTYPE hook_check_format(IDirect3D8 *d3d, UINT a, D3DDEVTYPE t, D3DFORMAT adapter_fmt, DWORD usage, D3DRESOURCETYPE rtype, D3DFORMAT fmt)
{
    HRESULT hr = real_check_format(d3d, a, t, adapter_fmt, usage, rtype, fmt);
    logp("CheckDeviceFormat adapterfmt=%d usage=%#lx rtype=%d fmt=%d -> %#lx (caller %p)\n", adapter_fmt, usage, rtype, fmt, hr, __builtin_return_address(0));
    return hr;
}

static HRESULT (STDMETHODCALLTYPE *real_create_tex)(IDirect3DDevice8 *, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture8 **);
static HRESULT STDMETHODCALLTYPE hook_create_tex(IDirect3DDevice8 *dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8 **out)
{
    HRESULT hr = real_create_tex(dev, w, h, levels, usage, fmt, pool, out);
    logp("CreateTexture %ux%u levels=%u usage=%#lx fmt=%d pool=%d -> %#lx (caller %p)\n", w, h, levels, usage, fmt, pool, hr, __builtin_return_address(0));
    return hr;
}

static HRESULT (STDMETHODCALLTYPE *real_set_palette)(IDirect3DDevice8 *, UINT, const PALETTEENTRY *);
static HRESULT STDMETHODCALLTYPE hook_set_palette(IDirect3DDevice8 *dev, UINT n, const PALETTEENTRY *e)
{
    static int count;
    HRESULT hr = real_set_palette(dev, n, e);
    if (count++ < 20) logp("SetPaletteEntries %u -> %#lx (caller %p)\n", n, hr, __builtin_return_address(0));
    return hr;
}

static BOOL (WINAPI *real_gvx)(OSVERSIONINFOA *);
static BOOL WINAPI hook_gvx(OSVERSIONINFOA *vi)
{
    BOOL r = real_gvx(vi);
    logp("exe GetVersionExA -> %lu.%lu build %lu platform %lu (caller %p)\n", vi->dwMajorVersion, vi->dwMinorVersion, vi->dwBuildNumber, vi->dwPlatformId, __builtin_return_address(0));
    return r;
}

// PROBE_RGN=none|rect|layered changes what happens to the game's window region (shaped window mode).
static int (WINAPI *real_swr)(HWND, HRGN, BOOL);
static int WINAPI hook_swr(HWND hwnd, HRGN rgn, BOOL redraw)
{
    static int calls;
    char mode[16] = "";
    GetEnvironmentVariableA("PROBE_RGN", mode, sizeof mode);
    DWORD size = rgn ? GetRegionData(rgn, 0, NULL) : 0;
    RECT box = { 0 };
    if (rgn) GetRgnBox(rgn, &box);
    calls++;
    if (calls <= 20)
        logp("SetWindowRgn #%d hwnd=%p rects=%lu box=%ld,%ld-%ld,%ld mode=%s (caller %p)\n", calls, hwnd,
             size ? (size - sizeof(RGNDATAHEADER)) / sizeof(RECT) : 0, box.left, box.top, box.right, box.bottom, mode,
             __builtin_return_address(0));
    if (rgn && !strcmp(mode, "none")) { DeleteObject(rgn); return 1; }
    if (rgn && !strcmp(mode, "rect")) { DeleteObject(rgn); rgn = CreateRectRgn(box.left, box.top, box.right, box.bottom - 1); }
    if (rgn && !strcmp(mode, "layered")) {
        DeleteObject(rgn);
        SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, RGB(255, 0, 255), 0, LWA_COLORKEY);
        return 1;
    }
    return real_swr(hwnd, rgn, redraw);
}

static HRESULT STDMETHODCALLTYPE hook_get_caps(IDirect3D8 *d3d, UINT adapter, D3DDEVTYPE type, D3DCAPS8 *caps)
{
    HRESULT hr = real_get_caps(d3d, adapter, type, caps);
    if (SUCCEEDED(hr))
        logp("GetDeviceCaps: VS=%#lx maxVSConst=%lu PS=%#lx maxStreams=%lu devcaps=%#lx maxBlendMatrices=%lu maxBlendIndex=%lu\n",
             caps->VertexShaderVersion, caps->MaxVertexShaderConst, caps->PixelShaderVersion, caps->MaxStreams,
             caps->DevCaps, caps->MaxVertexBlendMatrices, caps->MaxVertexBlendMatrixIndex);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_create_device(IDirect3D8 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                                    D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **out)
{
    logp("CreateDevice adapter=%u type=%d focus=%p flags=%#lx\n", adapter, type, focus, flags);
    log_pp("CreateDevice", pp);
    HRESULT hr = real_create_device(d3d, adapter, type, focus, flags, pp, out);
    logp("CreateDevice -> %#lx\n", hr);
    if (SUCCEEDED(hr)) {
        void **vtbl = *(void ***)*out;
        patch(vtbl, 14, hook_reset, (void **)&real_reset);
        patch(vtbl, 15, hook_present, (void **)&real_present);
        patch(vtbl, 75, hook_create_vs, (void **)&real_create_vs);
        patch(vtbl, 20, hook_create_tex, (void **)&real_create_tex);
        patch(vtbl, 66, hook_set_palette, (void **)&real_set_palette);
    }
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        logf = fopen("probe.log", "w");
        hook_iat("USER32.dll", "SetWindowRgn", hook_swr, (void **)&real_swr);
    }
    return TRUE;
}

IDirect3D8 *WINAPI Direct3DCreate8(UINT sdk)
{
    if (!real_d3d8) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat(path, "\\d3d8.dll");
        real_d3d8 = LoadLibraryA(path);
        real_create = (void *)GetProcAddress(real_d3d8, "Direct3DCreate8");
        hook_iat("KERNEL32.dll", "Sleep", hook_sleep, (void **)&real_sleep);
        hook_iat("KERNEL32.dll", "GetVersionExA", hook_gvx, (void **)&real_gvx);
        OSVERSIONINFOA vi = { sizeof vi };
        GetVersionExA(&vi);
        logp("probe loaded, real d3d8 from %s, sdk=%u, os reports %lu.%lu build %lu\n", path, sdk,
             vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    }
    IDirect3D8 *d3d = real_create(sdk);
    if (d3d) {
        void **vtbl = *(void ***)d3d;
        patch(vtbl, 13, hook_get_caps, (void **)&real_get_caps);
        patch(vtbl, 15, hook_create_device, (void **)&real_create_device);
        patch(vtbl, 10, hook_check_format, (void **)&real_check_format);
    }
    return d3d;
}
