# Unicode title/message shim for tsu_han.exe.
#
# The game passes Shift-JIS (code page 932) strings to ANSI Win32 APIs, which
# Windows decodes with the user's system code page, so on non-Japanese systems
# the window title and error dialogs are garbled. These wrappers sit in the
# unused padding at the end of .text (0x4E25A0) and replace the game's calls to
# CreateWindowExA, SetWindowTextA and MessageBoxA. Text containing non-ASCII
# bytes is converted from code page 932 and shown through the W APIs
# (DefWindowProcW for titles, MessageBoxW for dialogs).
#
# Build:  as --32 unicode_shim.s -o unicode_shim.o
#         objcopy -O binary -j .text unicode_shim.o unicode_shim.bin
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

base:

# void *user32_proc(const char *name) - GetProcAddress(GetModuleHandleA("USER32.dll"), name)
user32_proc:
    push    offset str_user32 - base + ORIGIN
    call    dword ptr [IAT_GetModuleHandleA]
    test    eax, eax
    jz      1f
    push    dword ptr [esp + 4]
    push    eax
    call    dword ptr [IAT_GetProcAddress]
1:  ret

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

str_user32:  .asciz "USER32.dll"
str_defwndw: .asciz "DefWindowProcW"
str_msgboxw: .asciz "MessageBoxW"

    # Entry offsets, read by patch_exe.py
    .balign 4
entries:
    .long create_window_ex - base
    .long set_window_text - base
    .long message_box - base
