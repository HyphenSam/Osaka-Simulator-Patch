# Code added to tsu_han.exe, placed in the unused padding at the end of .text (0x4E25A0).
#
# 1. Unicode text. The game passes Shift-JIS (code page 932) strings to ANSI Win32
#    APIs, which Windows decodes with the user's system code page, so on
#    non-Japanese systems the window title and error dialogs are garbled. The
#    wrappers replace the game's calls to CreateWindowExA, SetWindowTextA and
#    MessageBoxA. Text containing non-ASCII bytes is converted from code page 932
#    and shown through the W APIs (DefWindowProcW for titles, MessageBoxW for dialogs).
#
# 2. Shaped window speed. In "Window & Clip" mode the game cuts its window into a
#    speech bubble with SetWindowRgn. Direct3D's windowed Present is very slow into
#    a window with a complex region (~27 ms at 1200x1200, roughly one rectangle per
#    row). On Windows 8 and later the region is replaced by a colour-keyed layered
#    window: the area outside the bubble is cleared to magenta just before each
#    Present and the desktop compositor makes it transparent. Older Windows and Wine
#    keep the region, because layered windows can't show Direct3D there (and Wine is
#    fast anyway).
#
# Build: python build_shim.py
# All addresses are absolute: tsu_han.exe has no relocations and always loads at 0x400000.

    .intel_syntax noprefix
    .text

    .set ORIGIN, 0x4E25A0
    .set IAT_GetModuleHandleA,    0x4E31F4
    .set IAT_GetProcAddress,      0x4E3200
    .set IAT_MultiByteToWideChar, 0x4E3234
    .set IAT_SetWindowTextA,      0x4E33B0
    .set IAT_CreateWindowExA,     0x4E3450
    .set IAT_MessageBoxA,         0x4E3534
    .set IAT_GetVersion,          0x4E32E8
    .set IAT_HeapAlloc,           0x4E3120
    .set IAT_HeapFree,            0x4E3124
    .set IAT_DeleteObject,        0x4E307C
    .set IAT_CreateRectRgn,       0x4E30DC
    .set IAT_CombineRgn,          0x4E30E0
    .set IAT_SetPropA,            0x4E3460
    .set IAT_GetPropA,            0x4E3468
    .set IAT_RemovePropA,         0x4E3470
    .set IAT_GetWindowLongA,      0x4E3494
    .set IAT_SetWindowLongA,      0x4E3498
    .set IAT_GetWindowRect,       0x4E34B0
    .set IAT_GetClientRect,       0x4E34B4
    .set IAT_SetWindowRgn,        0x4E3510
    .set CRT_HEAP,                0x52D128      # the C runtime's heap handle
    .set GWL_EXSTYLE,             -20
    .set WS_EX_LAYERED,           0x80000
    .set MASK_COLOUR,             0xFF00FF      # magenta: same value as COLORREF and D3DCOLOR

base:

# void *get_proc(const char *module, const char *name) - GetProcAddress(GetModuleHandleA(module), name)
get_proc:
    push    dword ptr [esp + 4]
    call    dword ptr [IAT_GetModuleHandleA]
    test    eax, eax
    jz      1f
    push    dword ptr [esp + 8]
    push    eax
    call    dword ptr [IAT_GetProcAddress]
1:  ret

# void *user32_proc(const char *name)
user32_proc:
    push    dword ptr [esp + 4]
    push    offset str_user32 - base + ORIGIN
    call    get_proc
    add     esp, 8
    ret

# BOOL to_wide(const char *src, wchar_t *dst, int cch)
# Returns nonzero if src is non-null, contains non-ASCII bytes and converted.
to_wide:
    mov     eax, [esp + 4]
    test    eax, eax
    jz      3f
    mov     ecx, eax
2:  mov     dl, [ecx]
    test    dl, dl
    jz      3f
    inc     ecx
    test    dl, 0x80
    jz      2b
    push    dword ptr [esp + 12]        # cchWideChar
    push    dword ptr [esp + 12]        # lpWideCharStr
    push    -1                          # cbMultiByte
    push    eax                         # lpMultiByteStr
    push    0                           # dwFlags
    push    932                         # CodePage
    call    dword ptr [IAT_MultiByteToWideChar]
    ret
3:  xor     eax, eax
    ret

# void fix_title(HWND hwnd, const char *text)
fix_title:
    push    ebp
    mov     ebp, esp
    sub     esp, 0x200
    mov     eax, esp
    push    0x100
    push    eax
    push    dword ptr [ebp + 12]
    call    to_wide
    add     esp, 12
    test    eax, eax
    jz      4f
    push    offset str_defwndw - base + ORIGIN
    call    user32_proc
    add     esp, 4
    test    eax, eax
    jz      4f
    # DefWindowProcW(hwnd, WM_SETTEXT, 0, wide). SetWindowTextW would go through the
    # game's ANSI window procedure and be converted back to the system code page.
    mov     ecx, esp
    push    ecx
    push    0
    push    0x0C
    push    dword ptr [ebp + 8]
    call    eax
4:  mov     esp, ebp
    pop     ebp
    ret

# HWND WINAPI CreateWindowExA wrapper (12 args)
create_window_ex:
    push    ebp
    mov     ebp, esp
    mov     ecx, 12
    lea     eax, [ebp + 8 + 44]
5:  push    dword ptr [eax]
    sub     eax, 4
    loop    5b
    call    dword ptr [IAT_CreateWindowExA]
    test    eax, eax
    jz      6f
    push    eax
    push    dword ptr [ebp + 16]        # lpWindowName
    push    eax
    call    fix_title
    add     esp, 8
    pop     eax
6:  pop     ebp
    ret     48

# BOOL WINAPI SetWindowTextA wrapper
set_window_text:
    push    ebp
    mov     ebp, esp
    push    dword ptr [ebp + 12]
    push    dword ptr [ebp + 8]
    call    dword ptr [IAT_SetWindowTextA]
    push    eax
    push    dword ptr [ebp + 12]
    push    dword ptr [ebp + 8]
    call    fix_title
    add     esp, 8
    pop     eax
    pop     ebp
    ret     8

# int WINAPI MessageBoxA wrapper: uses MessageBoxW when the text or caption needs converting
message_box:
    push    ebp
    mov     ebp, esp
    sub     esp, 0xC00                  # text buffer at ebp-0xC00 (0x400 wchar), caption at ebp-0x400 (0x200 wchar)
    push    edi
    xor     edi, edi                    # edi = any conversion done
    lea     eax, [ebp - 0xC00]
    push    0x400
    push    eax
    push    dword ptr [ebp + 12]
    call    to_wide
    add     esp, 12
    or      edi, eax
    lea     eax, [ebp - 0x400]
    push    0x200
    push    eax
    push    dword ptr [ebp + 16]
    call    to_wide
    add     esp, 12
    or      edi, eax
    test    edi, edi
    jz      8f
    # Convert whichever string was plain ASCII as well, so both are wide.
    lea     eax, [ebp - 0xC00]
    push    0x400
    push    eax
    push    -1
    push    dword ptr [ebp + 12]
    push    0
    push    932
    mov     ecx, [ebp + 12]
    test    ecx, ecx
    jz      9f
    call    dword ptr [IAT_MultiByteToWideChar]
    jmp     10f
9:  add     esp, 24
10: lea     eax, [ebp - 0x400]
    push    0x200
    push    eax
    push    -1
    push    dword ptr [ebp + 16]
    push    0
    push    932
    mov     ecx, [ebp + 16]
    test    ecx, ecx
    jz      11f
    call    dword ptr [IAT_MultiByteToWideChar]
    jmp     12f
11: add     esp, 24
12: push    offset str_msgboxw - base + ORIGIN
    call    user32_proc
    add     esp, 4
    test    eax, eax
    jz      8f
    push    dword ptr [ebp + 20]        # uType
    lea     ecx, [ebp - 0x400]
    xor     edx, edx
    cmp     dword ptr [ebp + 16], 0
    cmovne  edx, ecx
    push    edx                         # lpCaption (NULL stays NULL)
    lea     ecx, [ebp - 0xC00]
    xor     edx, edx
    cmp     dword ptr [ebp + 12], 0
    cmovne  edx, ecx
    push    edx                         # lpText
    push    dword ptr [ebp + 8]         # hWnd
    call    eax
    jmp     13f
8:  push    dword ptr [ebp + 20]
    push    dword ptr [ebp + 16]
    push    dword ptr [ebp + 12]
    push    dword ptr [ebp + 8]
    call    dword ptr [IAT_MessageBoxA]
13: pop     edi
    mov     esp, ebp
    pop     ebp
    ret     16

# BOOL keep_region(void) - nonzero on Windows older than 8 (version 6.2) or on Wine
keep_region:
    call    dword ptr [IAT_GetVersion]
    cmp     al, 6
    jb      14f
    ja      15f
    cmp     ah, 2
    jb      14f
15: push    offset str_wine_get_version - base + ORIGIN
    push    offset str_ntdll - base + ORIGIN
    call    get_proc
    add     esp, 8
    ret
14: mov     eax, 1
    ret

# int WINAPI SetWindowRgn wrapper (hwnd, rgn, redraw)
set_window_rgn:
    push    ebp
    mov     ebp, esp
    sub     esp, 16                     # RECT at ebp-16
    push    ebx
    push    esi
    push    edi
    call    keep_region
    test    eax, eax
    jnz     20f
    push    offset str_prop - base + ORIGIN
    push    dword ptr [ebp + 8]
    call    dword ptr [IAT_RemovePropA] # forget any previous mask
    test    eax, eax
    jz      16f
    push    eax
    push    0
    push    dword ptr [CRT_HEAP]
    call    dword ptr [IAT_HeapFree]
16: cmp     dword ptr [ebp + 12], 0
    jne     17f
    push    GWL_EXSTYLE                 # region removed: back to a normal window
    push    dword ptr [ebp + 8]
    call    dword ptr [IAT_GetWindowLongA]
    and     eax, ~WS_EX_LAYERED
    push    eax
    push    GWL_EXSTYLE
    push    dword ptr [ebp + 8]
    call    dword ptr [IAT_SetWindowLongA]
20: push    dword ptr [ebp + 16]
    push    dword ptr [ebp + 12]
    push    dword ptr [ebp + 8]
    call    dword ptr [IAT_SetWindowRgn]
    jmp     19f
17: lea     eax, [ebp - 16]             # outside = window rectangle minus the bubble
    push    eax
    push    dword ptr [ebp + 8]
    call    dword ptr [IAT_GetWindowRect]
    mov     eax, [ebp - 4]
    sub     eax, [ebp - 12]
    push    eax                         # bottom = height
    mov     eax, [ebp - 8]
    sub     eax, [ebp - 16]
    push    eax                         # right = width
    push    0
    push    0
    call    dword ptr [IAT_CreateRectRgn]
    mov     ebx, eax
    test    ebx, ebx
    jz      18f
    push    4                           # RGN_DIFF
    push    dword ptr [ebp + 12]
    push    ebx
    push    ebx
    call    dword ptr [IAT_CombineRgn]
    push    offset str_getregiondata - base + ORIGIN
    push    offset str_gdi32 - base + ORIGIN
    call    get_proc
    add     esp, 8
    mov     esi, eax
    test    esi, esi
    jz      18f
    push    0
    push    0
    push    ebx
    call    esi                         # size = GetRegionData(outside, 0, NULL)
    mov     edi, eax
    test    edi, edi
    jz      18f
    push    edi
    push    0
    push    dword ptr [CRT_HEAP]
    call    dword ptr [IAT_HeapAlloc]
    test    eax, eax
    jz      18f
    push    eax                         # buffer, consumed by SetPropA below
    push    eax
    push    edi
    push    ebx
    call    esi                         # GetRegionData(outside, size, buffer)
    push    offset str_prop - base + ORIGIN
    push    dword ptr [ebp + 8]
    call    dword ptr [IAT_SetPropA]    # SetPropA(hwnd, "OsakaFixMask", buffer)
    push    GWL_EXSTYLE
    push    dword ptr [ebp + 8]
    call    dword ptr [IAT_GetWindowLongA]
    or      eax, WS_EX_LAYERED
    push    eax
    push    GWL_EXSTYLE
    push    dword ptr [ebp + 8]
    call    dword ptr [IAT_SetWindowLongA]
    push    offset str_setlayered - base + ORIGIN
    call    user32_proc
    add     esp, 4
    test    eax, eax
    jz      18f
    push    1                           # LWA_COLORKEY
    push    0
    push    MASK_COLOUR
    push    dword ptr [ebp + 8]
    call    eax                         # SetLayeredWindowAttributes
18: test    ebx, ebx
    jz      21f
    push    ebx
    call    dword ptr [IAT_DeleteObject]
21: push    dword ptr [ebp + 12]        # SetWindowRgn would have taken ownership of the region
    call    dword ptr [IAT_DeleteObject]
    mov     eax, 1
19: pop     edi
    pop     esi
    pop     ebx
    mov     esp, ebp
    pop     ebp
    ret     12

# HRESULT WINAPI IDirect3DDevice8::Present wrapper (device, src, dst, hwnd, dirty)
# If the window has a mask, clears everything outside the bubble to the colour key first.
present:
    push    ebp
    mov     ebp, esp
    sub     esp, 80                     # ebp-16 creation parameters, ebp-40 old viewport,
    push    esi                         # ebp-64 new viewport, ebp-80 client RECT
    push    edi
    mov     esi, [ebp + 8]
    lea     eax, [ebp - 16]
    push    eax
    push    esi
    mov     ecx, [esi]
    call    dword ptr [ecx + 0x24]      # GetCreationParameters
    test    eax, eax
    jnz     22f
    push    offset str_prop - base + ORIGIN
    push    dword ptr [ebp - 8]         # hFocusWindow
    call    dword ptr [IAT_GetPropA]
    mov     edi, eax
    test    edi, edi
    jz      22f
    lea     eax, [ebp - 80]
    push    eax
    push    dword ptr [ebp - 8]
    call    dword ptr [IAT_GetClientRect]
    lea     eax, [ebp - 40]
    push    eax
    push    esi
    mov     ecx, [esi]
    call    dword ptr [ecx + 0xA4]      # GetViewport(&old)
    mov     dword ptr [ebp - 64], 0
    mov     dword ptr [ebp - 60], 0
    mov     eax, [ebp - 72]
    mov     [ebp - 56], eax             # Width = client right
    mov     eax, [ebp - 68]
    mov     [ebp - 52], eax             # Height = client bottom
    mov     dword ptr [ebp - 48], 0
    mov     dword ptr [ebp - 44], 0x3F800000
    lea     eax, [ebp - 64]
    push    eax
    push    esi
    mov     ecx, [esi]
    call    dword ptr [ecx + 0xA0]      # SetViewport(full window)
    push    0                           # Stencil
    push    0x3F800000                  # Z
    push    0xFF000000 | MASK_COLOUR    # Color
    push    1                           # D3DCLEAR_TARGET
    lea     eax, [edi + 32]
    push    eax                         # RGNDATA.Buffer
    push    dword ptr [edi + 8]         # RGNDATA.rdh.nCount
    push    esi
    mov     ecx, [esi]
    call    dword ptr [ecx + 0x90]      # Clear
    lea     eax, [ebp - 40]
    push    eax
    push    esi
    mov     ecx, [esi]
    call    dword ptr [ecx + 0xA0]      # SetViewport(&old)
22: push    dword ptr [ebp + 24]
    push    dword ptr [ebp + 20]
    push    dword ptr [ebp + 16]
    push    dword ptr [ebp + 12]
    push    esi
    mov     ecx, [esi]
    call    dword ptr [ecx + 0x3C]      # Present
    pop     edi
    pop     esi
    mov     esp, ebp
    pop     ebp
    ret     20

str_user32:  .asciz "USER32.dll"
str_defwndw: .asciz "DefWindowProcW"
str_msgboxw: .asciz "MessageBoxW"
str_gdi32:   .asciz "GDI32.dll"
str_ntdll:   .asciz "ntdll.dll"
str_wine_get_version: .asciz "wine_get_version"
str_getregiondata:    .asciz "GetRegionData"
str_setlayered:       .asciz "SetLayeredWindowAttributes"
str_prop:    .asciz "OsakaFixMask"

    # Entry offsets, read by patch_exe.py (keep in sync with ENTRY_* there)
    .balign 4
entries:
    .long create_window_ex - base
    .long set_window_text - base
    .long message_box - base
    .long set_window_rgn - base
    .long present - base
