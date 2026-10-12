# Capture a window of the given process to PNG with PrintWindow (works while occluded).
# Shows window contents only: transparent/colour-keyed areas come out as drawn. Use grab_title.py to see the real screen.
param([string]$Out, [string]$Proc = "tsu_han")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[W]::SetProcessDPIAware() | Out-Null
$p = Get-Process $Proc -ErrorAction Stop | Select-Object -First 1
$h = $p.MainWindowHandle
$r = New-Object W+RECT; $c = New-Object W+RECT
[W]::GetWindowRect($h, [ref]$r) | Out-Null
[W]::GetClientRect($h, [ref]$c) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [W]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"win $($r.L),$($r.T) ${w}x${ht} client $($c.R)x$($c.B) ok=$ok"
