# Find a visible window whose title contains TEXT, bring it to the front and capture the real screen area.
# Usage: python grab_title.py TEXT out.png   (the patched game's title is 春日歩の通販生活; "通販" works)
import ctypes, sys
from ctypes import wintypes
from PIL import ImageGrab
u = ctypes.windll.user32
u.SetProcessDPIAware()
found = []
@ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def cb(h, l):
    n = u.GetWindowTextLengthW(h)
    if n and u.IsWindowVisible(h):
        b = ctypes.create_unicode_buffer(n + 1); u.GetWindowTextW(h, b, n + 1)
        if sys.argv[1] in b.value: found.append((h, b.value))
    return True
u.EnumWindows(cb, 0)
for h, t in found:
    r = wintypes.RECT(); u.GetWindowRect(h, ctypes.byref(r))
    print(repr(t).encode('ascii', 'backslashreplace').decode(), r.left, r.top, r.right, r.bottom)
if found:
    h = found[0][0]; u.SetForegroundWindow(h)
    r = wintypes.RECT(); u.GetWindowRect(h, ctypes.byref(r))
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(sys.argv[2])
