"""Patch tsu_han.exe (Ayumu Kasuga's Mail Order Life / Osaka Simulator) to run on modern systems.

Fixes:
- Speed: the game's timer class stores timeGetTime() (ms since boot) in a 32-bit float.
Past ~4.6 hours of uptime the float can no longer hold every millisecond, the
60 fps limiter misreads frame times, and the game runs far too fast. This patch
stores the start time as an integer and returns integer differences instead.
- Blue hair / grey skin: the game reads uninitialised heap memory; malloc now zero-fills.
- Garbled window title: Shift-JIS text passed to ANSI APIs is shown via the Unicode APIs
  (see shim.s).
- Slow speech-bubble window: on Windows 8+ the window region is replaced by a
  colour-keyed layered window (see shim.s).

Usage: python patch_exe.py tsu_han.exe [output.exe]
"""
import sys

IMAGE_BASE = 0x400000
TEXT_FILE_DELTA = 0x400000  # .text: VMA 0x401000 at file offset 0x1000

PATCHES = [
    # CTimer::CTimer - store the start time as a DWORD instead of a float
    (0x4752C5,
     "df 6c 24 04 8b c6 d9 5e 04",
     "8b 44 24 04 89 46 04 8b c6"),
    # CTimer::Reset - start = timeGetTime()
    (0x475340,
     "83 ec 08 56 8b f1 ff 15 60 35 4e 00 89 44 24 04",
     "56 8b f1 ff 15 60 35 4e 00 89 46 04 5e c3 90 90"),
    # CTimer::Elapsed - return (float)(timeGetTime() - start)
    (0x475370,
     "83 ec 08 56 8b f1 ff 15 60 35 4e 00 89 44 24 04 c7 44 24 08 00 00 00",
     "56 8b f1 ff 15 60 35 4e 00 2b 46 04 6a 00 50 df 2c 24 83 c4 08 5e c3"),
    # Frame limiter - also cap at 60 fps in fullscreen, which originally relied
    # on vsync and runs too fast on high refresh rate monitors
    (0x465E77,
     "8a 46 18 84 c0 74 17",
     "8a 46 18 84 c0 90 90"),
    # malloc (VC6 CRT _heap_alloc) - HeapAlloc(_crtheap, HEAP_ZERO_MEMORY, size).
    # The game reads uninitialised memory somewhere; on modern heaps that memory
    # holds stale data and Osaka's hair turns solid blue and her skin grey.
    (0x4AD88C,
     "6a 00 ff 35 28 d1 52 00",
     "6a 08 ff 35 28 d1 52 00"),
]


# Assembled shim.s, placed in the unused padding at the end of .text.
SHIM_VA = 0x4E25A0
SHIM = bytes.fromhex(
    "ff742404ff15f4314e0085c0740bff74240850ff1500324e00c3ff74240468bb294e00e8d8ffffff83c408c38b442404"
    "85c0742789c18a1184d2741f41f6c28074f4ff74240cff74240c6aff506a0068a4030000ff1534324e00c331c0c35589"
    "e581ec0002000089e0680001000050ff750ce8b5ffffff83c40c85c0741d68c6294e00e892ffffff83c40485c0740c89"
    "e1516a006a0cff7508ffd089ec5dc35589e5b90c0000008d4534ff3083e804e2f9ff1550344e0085c0740e50ff751050"
    "e899ffffff83c408585dc230005589e5ff750cff7508ff15b0334e0050ff750cff7508e876ffffff83c408585dc20800"
    "5589e581ec000c00005731ff8d8500f4ffff680004000050ff750ce81cffffff83c40c09c78d8500fcffff6800020000"
    "50ff7510e803ffffff83c40c09c785ff0f848f0000008d8500f4ffff6800040000506affff750c6a0068a40300008b4d"
    "0c85c97408ff1534324e00eb0383c4188d8500fcffff6800020000506affff75106a0068a40300008b4d1085c97408ff"
    "1534324e00eb0383c41868d5294e00e886feffff83c40485c0742aff75148d8d00fcffff31d2837d10000f45d1528d8d"
    "00f4ffff31d2837d0c000f45d152ff7508ffd0eb12ff7514ff7510ff750cff7508ff1534354e005f89ec5dc21000ff15"
    "e8324e003c06721a770580fc02721368f5294e0068eb294e00e802feffff83c408c3b801000000c35589e583ec105356"
    "57e8c8ffffff85c07543682f2a4e00ff7508ff1570344e0085c0740f506a00ff3528d15200ff1524314e00837d0c0075"
    "306aecff7508ff1594344e0025fffff7ff506aecff7508ff1598344e00ff7510ff750cff7508ff1510354e00e9dc0000"
    "008d45f050ff7508ff15b0344e008b45fc2b45f4508b45f82b45f0506a006a00ff15dc304e0089c385db0f8494000000"
    "6a04ff750c5353ff15e0304e0068062a4e0068e1294e00e844fdffff83c40889c685f6746f6a006a0053ffd689c785ff"
    "7462576a00ff3528d15200ff1520314e0085c0744f50505753ffd6682f2a4e00ff7508ff1560344e006aecff7508ff15"
    "94344e000d00000800506aecff7508ff1598344e0068142a4e00e8fbfcffff83c40485c0740e6a016a0068ff00ff00ff"
    "7508ffd085db740753ff157c304e00ff750cff157c304e00b8010000005f5e5b89ec5dc20c005589e583ec5056578b75"
    "088d45f050568b0eff512485c00f858e000000682f2a4e00ff75f8ff1568344e0089c785ff747a8d45b050ff75f8ff15"
    "b4344e008d45d850568b0eff91a4000000c745c000000000c745c4000000008b45b88945c88b45bc8945ccc745d00000"
    "0000c745d40000803f8d45c050568b0eff91a00000006a00680000803f68ff00ffff6a018d472050ff7708568b0eff91"
    "900000008d45d850568b0eff91a0000000ff7518ff7514ff7510ff750c568b0eff513c5f5e89ec5dc214005553455233"
    "322e646c6c0044656657696e646f7750726f6357004d657373616765426f78570047444933322e646c6c006e74646c6c"
    "2e646c6c0077696e655f6765745f76657273696f6e00476574526567696f6e44617461005365744c6179657265645769"
    "6e646f7741747472696275746573004f73616b614669784d61736b009f000000cd000000f00000000802000056030000"
)

# Shim entry points, in the order of the table at the end of shim.s
ENTRY_CREATE_WINDOW, ENTRY_SET_WINDOW_TEXT, ENTRY_MESSAGE_BOX, ENTRY_SET_WINDOW_RGN, ENTRY_PRESENT = range(5)

# `call [import]` instructions redirected into the shim: (address, original instruction, entry)
SHIM_CALLS = [
    (0x4C3FB1, "ff 15 50 34 4e 00", ENTRY_CREATE_WINDOW),    # CWnd::CreateEx -> CreateWindowExA
    (0x4C71B0, "ff 15 b0 33 4e 00", ENTRY_SET_WINDOW_TEXT),  # SetWindowTextA
    (0x4CA30D, "ff 15 b0 33 4e 00", ENTRY_SET_WINDOW_TEXT),  # SetWindowTextA
    (0x4271C8, "ff 15 34 35 4e 00", ENTRY_MESSAGE_BOX),      # MessageBoxA (missing data files)
    (0x427228, "ff 15 34 35 4e 00", ENTRY_MESSAGE_BOX),      # MessageBoxA
    (0x4C5022, "ff 15 34 35 4e 00", ENTRY_MESSAGE_BOX),      # MessageBoxA (MFC)
    (0x4CA5A2, "ff 15 34 35 4e 00", ENTRY_MESSAGE_BOX),      # MessageBoxA (MFC)
]


def le32(value):
    return (value & 0xFFFFFFFF).to_bytes(4, "little").hex()


def all_patches():
    patches = list(PATCHES)
    patches.append((SHIM_VA, "00" * len(SHIM), SHIM.hex()))
    count = ENTRY_PRESENT + 1
    table = SHIM[-4 * count:]
    entry = [SHIM_VA + int.from_bytes(table[4 * i:4 * i + 4], "little") for i in range(count)]
    for va, old, idx in SHIM_CALLS:
        patches.append((va, old, "e8" + le32(entry[idx] - (va + 5)) + "90"))
    # Window region setup: `mov ebp, [SetWindowRgn]` -> `mov ebp, set_window_rgn; nop`.
    # Both of the game's SetWindowRgn calls for the main window go through ebp.
    patches.append((0x427898, "8b 2d 10 35 4e 00", "bd" + le32(entry[ENTRY_SET_WINDOW_RGN]) + "90"))
    # Main loop Present: `mov edx,[eax]; push ebx; push eax; call [edx+0x3c]`
    # -> `push ebx; push eax; call present` (ebx is 0, eax the device).
    patches.append((0x4663C1, "8b 10 53 50 ff 52 3c", "53 50 e8" + le32(entry[ENTRY_PRESENT] - (0x4663C3 + 5))))
    return patches


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src
    data = bytearray(open(src, "rb").read())
    for va, old, new in all_patches():
        off = va - TEXT_FILE_DELTA
        old_b, new_b = bytes.fromhex(old), bytes.fromhex(new)
        cur = bytes(data[off:off + len(old_b)])
        if cur == new_b:
            continue
        if cur != old_b:
            sys.exit(f"{va:#x}: unexpected bytes, is this the original tsu_han.exe? Nothing was written.")
        data[off:off + len(new_b)] = new_b
    open(dst, "wb").write(data)
    print(f"Patched {dst}")


if __name__ == "__main__":
    main()
