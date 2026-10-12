"""Patch tsu_han.exe (Ayumu Kasuga's Mail Order Life / Osaka Simulator) to run on modern systems.

Fixes:
- Speed: the game's timer class stores timeGetTime() (ms since boot) in a 32-bit float.
Past ~4.6 hours of uptime the float can no longer hold every millisecond, the
60 fps limiter misreads frame times, and the game runs far too fast. This patch
stores the start time as an integer and returns integer differences instead.
- Blue hair / grey skin: the game reads uninitialised heap memory; malloc now zero-fills.
- Garbled window title: Shift-JIS text passed to ANSI APIs is shown via the Unicode APIs
  (see unicode_shim.s).

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


# Assembled unicode_shim.s, placed in the unused padding at the end of .text.
SHIM_VA = 0x4E25A0
SHIM = bytes.fromhex(
    "686d274e00ff15f4314e0085c0740bff74240450ff1500324e00c38b44240485c0742789c18a1184d2741f41f6c28074"
    "f4ff74240cff74240c6aff506a0068a4030000ff1534324e00c331c0c35589e581ec0002000089e0680001000050ff75"
    "0ce8b5ffffff83c40c85c0741d6878274e00e889ffffff83c40485c0740c89e1516a006a0cff7508ffd089ec5dc35589"
    "e5b90c0000008d4534ff3083e804e2f9ff1550344e0085c0740e50ff751050e899ffffff83c408585dc230005589e5ff"
    "750cff7508ff15b0334e0050ff750cff7508e876ffffff83c408585dc208005589e581ec000c00005731ff8d8500f4ff"
    "ff680004000050ff750ce81cffffff83c40c09c78d8500fcffff680002000050ff7510e803ffffff83c40c09c785ff0f"
    "848f0000008d8500f4ffff6800040000506affff750c6a0068a40300008b4d0c85c97408ff1534324e00eb0383c4188d"
    "8500fcffff6800020000506affff75106a0068a40300008b4d1085c97408ff1534324e00eb0383c4186887274e00e87d"
    "feffff83c40485c0742aff75148d8d00fcffff31d2837d10000f45d1528d8d00f4ffff31d2837d0c000f45d152ff7508"
    "ffd0eb12ff7514ff7510ff750cff7508ff1534354e005f89ec5dc210005553455233322e646c6c0044656657696e646f"
    "7750726f6357004d657373616765426f785700908e000000bc000000df000000"
)

# Call sites redirected into the shim: (address, original instruction, entry index)
SHIM_CALLS = [
    (0x4C3FB1, "ff 15 50 34 4e 00", 0),  # CWnd::CreateEx -> CreateWindowExA
    (0x4C71B0, "ff 15 b0 33 4e 00", 1),  # SetWindowTextA
    (0x4CA30D, "ff 15 b0 33 4e 00", 1),  # SetWindowTextA
    (0x4271C8, "ff 15 34 35 4e 00", 2),  # MessageBoxA (missing data files)
    (0x427228, "ff 15 34 35 4e 00", 2),  # MessageBoxA
    (0x4C5022, "ff 15 34 35 4e 00", 2),  # MessageBoxA (MFC)
    (0x4CA5A2, "ff 15 34 35 4e 00", 2),  # MessageBoxA (MFC)
]


def all_patches():
    patches = list(PATCHES)
    patches.append((SHIM_VA, "00" * len(SHIM), SHIM.hex()))
    entries = [int.from_bytes(SHIM[-12 + 4 * i:-8 + 4 * i or None], "little") for i in range(3)]
    for va, old, idx in SHIM_CALLS:
        rel = (SHIM_VA + entries[idx] - (va + 5)) & 0xFFFFFFFF
        patches.append((va, old, "e8" + rel.to_bytes(4, "little").hex() + "90"))
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
