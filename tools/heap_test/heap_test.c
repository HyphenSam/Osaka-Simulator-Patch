// Build: zig cc -target x86-windows-gnu -O2 -shared -o d3d8.dll heap_test.c d3d8.def
// Test d3d8.dll proxy: changes the exe's heap behaviour to find which memory bug causes the blue hair.
// HEAPTEST=zero    -> every HeapAlloc/HeapReAlloc from the exe zero-fills
// HEAPTEST=nofree  -> HeapFree from the exe does nothing (memory is leaked)
// HEAPTEST=poison  -> zero-fill allocations but fill freed blocks with 0xDD (makes use-after-free worse)
// HEAPTEST=junk    -> fresh allocations are filled with 0xCD (makes uninitialised reads worse)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HMODULE real_d3d8;
static void *(WINAPI *real_create)(UINT);
static LPVOID (WINAPI *real_alloc)(HANDLE, DWORD, SIZE_T);
static LPVOID (WINAPI *real_realloc)(HANDLE, DWORD, LPVOID, SIZE_T);
static BOOL (WINAPI *real_free)(HANDLE, DWORD, LPVOID);
static char mode[16];

static LPVOID WINAPI hook_alloc(HANDLE h, DWORD flags, SIZE_T n)
{
    if (!strcmp(mode, "zero") || !strcmp(mode, "poison")) flags |= HEAP_ZERO_MEMORY;
    LPVOID p = real_alloc(h, flags, n);
    if (p && !strcmp(mode, "junk") && !(flags & HEAP_ZERO_MEMORY)) memset(p, 0xCD, n);
    return p;
}

static LPVOID WINAPI hook_realloc(HANDLE h, DWORD flags, LPVOID old, SIZE_T n)
{
    if (!strcmp(mode, "zero")) flags |= HEAP_ZERO_MEMORY;
    return real_realloc(h, flags, old, n);
}

static BOOL WINAPI hook_free(HANDLE h, DWORD flags, LPVOID p)
{
    if (!strcmp(mode, "nofree")) return TRUE;
    if (!strcmp(mode, "poison") && p) {
        SIZE_T n = HeapSize(h, 0, p);
        if (n != (SIZE_T)-1) memset(p, 0xDD, n);
    }
    return real_free(h, flags, p);
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

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        GetEnvironmentVariableA("HEAPTEST", mode, sizeof mode);
        hook_iat("KERNEL32.dll", "HeapAlloc", hook_alloc, (void **)&real_alloc);
        hook_iat("KERNEL32.dll", "HeapReAlloc", hook_realloc, (void **)&real_realloc);
        hook_iat("KERNEL32.dll", "HeapFree", hook_free, (void **)&real_free);
    }
    return TRUE;
}

void *WINAPI Direct3DCreate8(UINT sdk)
{
    if (!real_d3d8) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat(path, "\\d3d8.dll");
        real_d3d8 = LoadLibraryA(path);
        real_create = (void *)GetProcAddress(real_d3d8, "Direct3DCreate8");
    }
    return real_create(sdk);
}
