# Run the game for a while, optionally taking screenshots, then kill it.
#   -GameDir   folder containing the game (default: three levels up, i.e. this repo inside the game folder)
#   -Exe       exe to run, relative to GameDir
#   -Env       extra environment variables, "NAME=value;NAME2=value" (e.g. "__COMPAT_LAYER=FaultTolerantHeap",
#              "HEAPTEST=zero", "PROBE_RGN=layered")
#   -Seconds   how long to run after a 7 s start-up wait
#   -ShotEvery seconds between screenshots (0 = none); shots go to -OutDir as 001.png, 002.png, ...
# The game autosaves tsu_han.dat while running: back it up first.
param(
    [string]$GameDir = (Resolve-Path "$PSScriptRoot\..\..\.."),
    [string]$Exe = "tsu_han.exe",
    [string]$Env = "",
    [int]$Seconds = 15,
    [int]$ShotEvery = 0,
    [string]$OutDir = "$env:TEMP\osaka-shots"
)
$psi = New-Object System.Diagnostics.ProcessStartInfo (Join-Path $GameDir $Exe)
$psi.WorkingDirectory = $GameDir
$psi.UseShellExecute = $false
foreach ($kv in ($Env -split ';')) { if ($kv) { $k, $v = $kv -split '=', 2; $psi.EnvironmentVariables[$k] = $v } }
$p = [System.Diagnostics.Process]::Start($psi)
Start-Sleep 7
$proc = [IO.Path]::GetFileNameWithoutExtension($Exe)
if ($ShotEvery -gt 0) { New-Item -ItemType Directory -Force $OutDir | Out-Null }
$n = 0; $end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) {
    if ($ShotEvery -gt 0) {
        $n++
        & "$PSScriptRoot\grab.ps1" -Proc $proc -Out ("$OutDir\{0:D3}.png" -f $n) | Out-Null
        Start-Sleep $ShotEvery
    } else {
        Start-Sleep 1
    }
}
Stop-Process -Id $p.Id -Force
Start-Sleep 1
