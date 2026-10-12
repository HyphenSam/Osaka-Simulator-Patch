# Osaka Simulator fix

A patch for **春日歩の通販生活** (*Ayumu Kasuga's Mail Order Life*, better known as *Osaka Simulator*), the free 2004 Azumanga Daioh desktop game. It makes the game run properly on modern Windows, and on Linux and macOS through Wine. No virtual machine or compatibility mode is needed.

## What it fixes

| Problem | Cause | Fix |
|---|---|---|
| The game runs far too fast (often 5–20×). | The frame timer stores "milliseconds since the PC booted" in a 32-bit float. After about 4.6 hours of uptime the float loses precision, the 60 fps limiter misfires, and the game renders hundreds of frames per second. Windows' Fast Startup means uptime rarely resets, even across shutdowns. | The timer uses whole milliseconds instead. The 60 fps cap also applies in fullscreen, which originally relied on vsync and ran too fast on 120/144 Hz monitors. |
| Osaka's hair turns solid blue or disappears, and her skin looks grey. | The game reads memory it never initialised. Old Windows heaps usually handed out clean memory; modern ones don't. | The game's memory allocator zero-fills new allocations. |
| The window title is garbled (`t“ú•à‚Ì’Ê”Ì¶Šˆ`). | Japanese (Shift-JIS) text is passed to Windows' legacy text functions, which decode it with your system language. | The title and error messages are converted and shown in proper Japanese: 春日歩の通販生活. |

Your existing save (`tsu_han.dat`) and settings (`tsu_han.cfg`) keep working as they are.

## Installing (Windows)

1. Download `tsu_han.exe` from the [Releases](../../releases) page.
2. Open the folder where the game is installed. It contains `tsu_han.exe` and the folders `animation`, `model` and `sound`.
3. Rename the original `tsu_han.exe` to `tsu_han_original.exe`, so you can go back if needed.
4. Copy the downloaded `tsu_han.exe` into the folder.
5. If you previously turned on compatibility mode for the game, turn it off: right-click `tsu_han.exe` → **Properties** → **Compatibility** → untick **Run this program in compatibility mode**. The patched game doesn't need it.
6. Run `tsu_han.exe`.

## Installing (Linux)

Install Wine with 32-bit support (on Ubuntu/Debian: `sudo dpkg --add-architecture i386 && sudo apt install wine wine32`). Replace `tsu_han.exe` as above, then run it from the game folder:

```bash
cd "/path/to/the/game"
wine tsu_han.exe
```

This has been tested with Wine 9.0 on Ubuntu 24.04. Both the normal window and the speech-bubble-shaped window run at full speed.

## Installing (macOS)

This is untested. A Wine build that can run 32-bit Windows programs on 64-bit macOS should work the same way as on Linux, for example CrossOver or a recent Wine from Homebrew. Reports are welcome.

## Display modes

`tsu_han.cfg` is a text file in the game folder:

- **WindowMode 0** (speech-bubble window): on Windows this runs at about 35 fps at the 1200×1200 size, because Windows draws shaped windows slowly. The game speed is still correct, but animation is smoother in WindowMode 1 or with a smaller `WindowConfig`.
- **WindowMode 1** (normal window): runs at a steady 60 fps.

## Patching it yourself

`patch_exe.py` needs Python 3 and nothing else.

```bash
python patch_exe.py tsu_han.exe tsu_han_patched.exe
```

It checks every byte it replaces and refuses to touch anything other than the expected version of the game:

| | SHA-256 |
|---|---|
| Original `tsu_han.exe` | `2bdf5be89f1ce01fbf9b5dc034e3ab4ee9c3960f2f508cc968a1a657c1f304d0` |
| Patched result | `c1557fc61e22ce35765ce853a6ce39e5f7c342ed3287435c14ac84b0d2b1331b` |

Running it on an already patched file does nothing.

## How it works

All changes are made in place in `tsu_han.exe`. The exe always loads at `0x400000`, so the addresses below are fixed.

| Address | Change |
|---|---|
| `0x4752C5`, `0x475340`, `0x475370` | Timer class (constructor, reset, elapsed): stores `timeGetTime()` as an integer and returns `(float)(now − start)`. The subtraction is exact and survives the 49.7-day wrap. |
| `0x465E77` | The frame limiter's "windowed only" check is removed. |
| `0x4AD88C` | The VC6 runtime's `malloc` calls `HeapAlloc` with `HEAP_ZERO_MEMORY`. |
| `0x4E25A0` | [`unicode_shim.s`](unicode_shim.s) is placed in the unused padding at the end of the code section. The game's calls to `CreateWindowExA`, `SetWindowTextA` and `MessageBoxA` are redirected to it. Text containing Shift-JIS is converted and shown through `DefWindowProcW(WM_SETTEXT)` and `MessageBoxW`. |

How these were found:

- **Speed.** A logging `d3d8.dll` proxy ([`tools/probe`](tools/probe)) showed the game rendering at 300–550 fps. It read the game's internal frame times as −62 ms and +60 ms on a PC with 16 days of uptime, where the float can only hold multiples of 128 ms.
- **Blue hair.** It reproduced in 15–20% of screenshots. It never appeared once heap allocations were zero-filled, filled with junk, or never reused ([`tools/heap_test`](tools/heap_test)), nor with Windows' `FaultTolerantHeap` shim. A pass-through control still showed it. That combination points at reads of uninitialised memory rather than use-after-free.

## Building and testing the shim

You need GNU binutils that can assemble 32-bit x86 (for example MinGW-w64), and any 32-bit Windows C compiler (`i686-w64-mingw32-gcc`, or `zig cc -target x86-windows-gnu`).

```bash
python build_shim.py
```

This assembles `unicode_shim.s`, updates the bytes embedded in `patch_exe.py`, and writes `tests/unicode_shim.bin`.

```bash
zig cc -target x86-windows-gnu -O1 tests/shim_test.c -o shim_test.exe -Wl,--image-base=0x10000000
```

```bash
./shim_test.exe tests/unicode_shim.bin
```

The test runs the shim at its real address with fake Windows functions and checks argument passing, return values, stack balance and the Shift-JIS conversion.

## Licence

The patch code in this repository is released under the [MIT licence](LICENSE). The game itself belongs to its original authors (hirahira.net) and is not covered by this licence. This project is not affiliated with them.
