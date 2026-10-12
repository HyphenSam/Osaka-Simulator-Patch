# Osaka Simulator Patch

A patch for **春日歩の通販生活** (*Ayumu Kasuga's Mail Order Life*, better known as *Osaka Simulator*), the free 2004 Azumanga Daioh desktop game. It makes the game run properly on modern Windows, and on Linux and macOS through Wine. No virtual machine or compatibility mode is needed.

## What it fixes

| Problem | Cause | Fix |
|---|---|---|
| The game runs far too fast (often 5–20×). | The frame timer stores "milliseconds since the PC booted" in a 32-bit float. After about 4.6 hours of uptime the float loses precision, the 60 fps limiter misfires, and the game renders hundreds of frames per second. Windows' Fast Startup means uptime rarely resets, even across shutdowns. | The timer uses whole milliseconds instead. The 60 fps cap also applies in fullscreen, which originally relied on vsync and ran too fast on 120/144 Hz monitors. |
| Osaka's hair turns solid blue or disappears, and certain models are invisible. | The game reads memory it never initialised. Old Windows heaps usually handed out clean memory; modern ones don't. | The game's memory allocator zero-fills new allocations. |
| The window title is garbled (`t“ú•à‚Ì’Ê”Ì¶Šˆ`). | Japanese (Shift-JIS) text is passed to Windows' legacy text functions, which decode it with your system language. | The title and error messages are converted and shown in proper Japanese: 春日歩の通販生活. |
| The speech-bubble window (WindowMode 0) runs at about 35 fps. | The bubble is a window region of over 700 rectangles. Direct3D's `Present` into a window with such a region takes about 27 ms per frame on Windows, and was slow on Windows XP too. | On Windows 8 and later, the bubble is cut out by the desktop compositor instead: the window becomes a colour-keyed layered window, and the area outside the bubble is filled with the key colour before each frame is shown. It now runs at 60 fps. |

Your existing save (`tsu_han.dat`) and settings (`tsu_han.cfg`) keep working as they are.

## Installing (Windows)

1. Get the original game from the [Internet Archive](https://archive.org/details/osakasimulator) or [My Abandonware](https://www.myabandonware.com/game/ayumu-kasuga-s-mail-order-life-voj). The patch has been tested on the Internet Archive version.
2. Download `tsu_han.exe` from the [Releases](../../releases) page.
3. (Optional) Download the `tsu_han.dat` save file containing all items owned, and ending achieved.
4. Open the folder where the game is installed. It contains `tsu_han.exe` and the folders `animation`, `model` and `sound`.
5. Rename the original `tsu_han.exe` to `tsu_han_original.exe`, so you can go back if needed.
6. Copy the downloaded `tsu_han.exe` into the folder. Optionally also copy the downloaded `tsu_han.dat` save file.
7. If you previously turned on compatibility mode for the game, turn it off: right-click `tsu_han.exe` → **Properties** → **Compatibility** → untick **Run this program in compatibility mode**. The patched game doesn't need it.
8. Run `tsu_han.exe`.

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

- **WindowMode 0**: the speech-bubble window, sitting on your desktop.
- **WindowMode 1**: a normal rectangular window.
- **WindowMode 2**: fullscreen.

All three run at a steady 60 fps.

## Patching it yourself

`patch_exe.py` needs Python 3 and nothing else.

```bash
python patch_exe.py tsu_han.exe tsu_han_patched.exe
```

It checks every byte it replaces and refuses to touch anything other than the expected version of the game:

| | SHA-256 |
|---|---|
| Original `tsu_han.exe` | `2bdf5be89f1ce01fbf9b5dc034e3ab4ee9c3960f2f508cc968a1a657c1f304d0` |
| Patched result | `1de2a9ffc327b0a3dbbe3abbdcc77fdb711f72b119d7aa8c55ca132604426785` |

Running it on an already patched file does nothing.

## How it works

All changes are made in place in `tsu_han.exe`. The exe always loads at `0x400000`, so the addresses below are fixed.

| Address | Change |
|---|---|
| `0x4752C5`, `0x475340`, `0x475370` | Timer class (constructor, reset, elapsed): stores `timeGetTime()` as an integer and returns `(float)(now − start)`. The subtraction is exact and survives the 49.7-day wrap. |
| `0x465E77` | The frame limiter's "windowed only" check is removed. |
| `0x4AD88C` | The VC6 runtime's `malloc` calls `HeapAlloc` with `HEAP_ZERO_MEMORY`. |
| `0x4E25A0` | [`shim.s`](shim.s) is placed in the unused padding at the end of the code section. |
| `0x4C3FB1`, `0x4C71B0`, `0x4CA30D`, `0x4271C8`, `0x427228`, `0x4C5022`, `0x4CA5A2` | The game's calls to `CreateWindowExA`, `SetWindowTextA` and `MessageBoxA` go to the shim. Text containing Shift-JIS is converted and shown through `DefWindowProcW(WM_SETTEXT)` and `MessageBoxW`. |
| `0x427898` | The main window's `SetWindowRgn` goes to the shim. On Windows 8+ (and not under Wine), it stores the rectangles outside the bubble as a window property and makes the window layered with a magenta colour key. |
| `0x4663C1` | The main loop's `Present` goes to the shim. If the window has the property, it `Clear`s those rectangles to magenta first. |

How these were found:

- **Speed.** A logging `d3d8.dll` proxy ([`tools/probe`](tools/probe)) showed the game rendering at 300–550 fps. It read the game's internal frame times as −62 ms and +60 ms on a PC with 16 days of uptime, where the float can only hold multiples of 128 ms.
- **Shaped window.** With the probe swapping the game's region at runtime: the real bubble (742 rectangles) gave a 27 ms `Present`, a single-rectangle region 0.36 ms, and a colour-keyed layered window about 3 ms.
- **Blue hair.** It reproduced in 15–20% of screenshots. It never appeared once heap allocations were zero-filled, filled with junk, or never reused ([`tools/heap_test`](tools/heap_test)), nor with Windows' `FaultTolerantHeap` shim. A pass-through control still showed it. That combination points at reads of uninitialised memory rather than use-after-free.

[`tools/testing`](tools/testing) holds the scripts used for these runs: a launcher with screenshots, a blue-hair frame counter, a screen grabber, and a reader for Windows' compatibility database.

## Building and testing the shim

You need GNU binutils that can assemble 32-bit x86 (for example MinGW-w64), and any 32-bit Windows C compiler (`i686-w64-mingw32-gcc`, or `zig cc -target x86-windows-gnu`).

```bash
python build_shim.py
```

This assembles `shim.s`, updates the bytes embedded in `patch_exe.py`, and writes `tests/shim.bin`.

```bash
zig cc -target x86-windows-gnu -O1 tests/shim_test.c -o shim_test.exe -Wl,--image-base=0x10000000
```

```bash
./shim_test.exe tests/shim.bin
```

The test runs the shim at its real address with fake Windows functions and checks argument passing, return values, stack balance and the Shift-JIS conversion. The shaped-window code is tested by running the game.

## Licence

The patch code in this repository is released under the [MIT licence](LICENSE). The game itself belongs to its original authors (hirahira.net) and is not covered by this licence. This project is not affiliated with them.
