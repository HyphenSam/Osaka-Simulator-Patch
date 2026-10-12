// Runs the assembled unicode_shim at its real address (0x4E25A0) with fake import
// table entries, and checks what reaches the Unicode APIs.
// Build: zig cc -target x86-windows-gnu -O1 shim_test.c -o shim_test.exe -Wl,--image-base=0x10000000
// Usage: shim_test.exe unicode_shim.bin  (made by build_shim.py)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define SHIM_VA 0x4E25A0
#define IAT(va) (*(void **)(va))

static HWND last_hwnd;
static UINT last_msg;
static wchar_t last_title[256], last_text[1024], last_caption[512];
static int defwnd_calls, msgboxw_calls, msgboxa_calls, setwta_calls, cwa_calls;

static LRESULT WINAPI fake_defwndprocw(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    defwnd_calls++;
    last_hwnd = h;
    last_msg = msg;
    wcsncpy(last_title, (const wchar_t *)l, 255);
    return TRUE;
}

static int WINAPI fake_msgboxw(HWND h, LPCWSTR text, LPCWSTR caption, UINT type)
{
    msgboxw_calls++;
    wcsncpy(last_text, text ? text : L"(null)", 1023);
    wcsncpy(last_caption, caption ? caption : L"(null)", 511);
    return 42 + type;
}

static int WINAPI fake_msgboxa(HWND h, LPCSTR text, LPCSTR caption, UINT type) { msgboxa_calls++; return 7; }
static BOOL WINAPI fake_setwta(HWND h, LPCSTR s) { setwta_calls++; return 5; }
static HWND WINAPI fake_cwa(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int hh,
                            HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    cwa_calls++;
    return (HWND)(ULONG_PTR)(0x1000 + x + y + w + hh + style);
}

static FARPROC WINAPI fake_getprocaddress(HMODULE m, LPCSTR name)
{
    if (!strcmp(name, "DefWindowProcW")) return (FARPROC)fake_defwndprocw;
    if (!strcmp(name, "MessageBoxW")) return (FARPROC)fake_msgboxw;
    return NULL;
}

static int failures;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "unicode_shim.bin", "rb");
    if (!f) { puts("cannot open shim"); return 2; }
    BYTE shim[4096];
    size_t n = fread(shim, 1, sizeof shim, f);
    fclose(f);

    BYTE *page = VirtualAlloc((void *)0x4E0000, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (page != (BYTE *)0x4E0000) {
        // Address space layout is randomised; something occasionally sits there. Retry in a new process.
        static int tries;
        char cmd[MAX_PATH * 2];
        if (++tries > 1 || argc > 2) { puts("cannot map 0x4E0000"); return 2; }
        for (int i = 0; i < 10; i++) {
            STARTUPINFOA si = { sizeof si };
            PROCESS_INFORMATION pi;
            DWORD code = 2;
            snprintf(cmd, sizeof cmd, "\"%s\" \"%s\" retry", argv[0], argc > 1 ? argv[1] : "unicode_shim.bin");
            if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) break;
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (code != 2) return code;
        }
        puts("cannot map 0x4E0000");
        return 2;
    }
    memcpy((void *)SHIM_VA, shim, n);
    LoadLibraryA("user32.dll");  // the shim looks USER32 up by name; the game always has it loaded
    IAT(0x4E31F4) = (void *)GetModuleHandleA;
    IAT(0x4E3200) = (void *)fake_getprocaddress;
    IAT(0x4E3234) = (void *)MultiByteToWideChar;
    IAT(0x4E33B0) = (void *)fake_setwta;
    IAT(0x4E3450) = (void *)fake_cwa;
    IAT(0x4E3534) = (void *)fake_msgboxa;

    DWORD *entries = (DWORD *)(shim + n - 12);
    HWND (WINAPI *cwx)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) =
        (void *)(SHIM_VA + entries[0]);
    BOOL (WINAPI *swt)(HWND, LPCSTR) = (void *)(SHIM_VA + entries[1]);
    int (WINAPI *mb)(HWND, LPCSTR, LPCSTR, UINT) = (void *)(SHIM_VA + entries[2]);

    // "春日歩の通販生活" in Shift-JIS, as stored in tsu_han.exe
    const char title[] = "\x8f\x74\x93\xfa\x95\xe0\x82\xcc\x92\xca\x94\xcc\x90\xb6\x8a\x88";
    const wchar_t want[] = L"春日歩の通販生活";

    // CreateWindowExA: all 12 args must pass through, then the title is set in Unicode
    HWND h = cwx(0, "cls", title, 0x10, 1, 2, 3, 4, NULL, NULL, NULL, NULL);
    check(h == (HWND)(ULONG_PTR)(0x1000 + 1 + 2 + 3 + 4 + 0x10), "CreateWindowExA args and return value pass through");
    check(cwa_calls == 1 && defwnd_calls == 1 && last_msg == WM_SETTEXT && last_hwnd == h, "CreateWindowExA sets title via DefWindowProcW(WM_SETTEXT)");
    check(!wcscmp(last_title, want), "CreateWindowExA title converted from Shift-JIS");

    // ASCII titles are left alone
    defwnd_calls = 0;
    cwx(0, "cls", "plain", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    check(defwnd_calls == 0, "ASCII title not touched");
    cwx(0, "cls", NULL, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    check(defwnd_calls == 0, "NULL title not touched");

    // SetWindowTextA
    defwnd_calls = 0;
    memset(last_title, 0, sizeof last_title);
    BOOL r = swt((HWND)0x1234, title);
    check(r == 5 && setwta_calls == 1, "SetWindowTextA called through with return value");
    check(defwnd_calls == 1 && last_hwnd == (HWND)0x1234 && !wcscmp(last_title, want), "SetWindowTextA title converted");

    // MessageBoxA with Japanese text and ASCII caption
    int ret = mb((HWND)0x99, title, "Error", MB_ICONERROR);
    check(ret == 42 + MB_ICONERROR && msgboxw_calls == 1 && msgboxa_calls == 0, "MessageBoxA with Japanese text uses MessageBoxW");
    check(!wcscmp(last_text, want) && !wcscmp(last_caption, L"Error"), "MessageBox text and caption converted");

    // MessageBoxA with NULL caption
    ret = mb(NULL, title, NULL, 0);
    check(ret == 42 && !wcscmp(last_caption, L"(null)"), "MessageBoxA NULL caption stays NULL");

    // Plain ASCII message box goes to MessageBoxA unchanged
    ret = mb(NULL, "hello", "cap", 1);
    check(ret == 7 && msgboxa_calls == 1 && msgboxw_calls == 2, "ASCII MessageBoxA passes through");

    // Stack must be balanced after all stdcall wrappers (checked implicitly by reaching here)
    printf("%d failure(s)\n", failures);
    return failures != 0;
}
