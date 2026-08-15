# Measures how many *presented* frames a layered window actually produces.
#
# The rAF callback rate is not the answer: the page can tick at 60 Hz while the
# layered surface is pushed to the screen far less often. This samples the
# window as fast as PrintWindow allows and counts how many distinct bar
# positions come back, which is a lower bound on the real presented rate — and
# an honest one, because a frame nobody can capture is a frame nobody sees.

param(
    [ValidateSet('win', 'full')][string]$Mode = 'win',
    [int]$DurationMs = 2000,
    [int]$Lane = 4          # 'js rAF transform'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class Fps {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    // Reuses one bitmap and reads a single scanline: the goal is to sample as
    // often as possible, so per-sample cost has to stay low.
    public static int BarX(IntPtr hwnd, Bitmap bmp, int y) {
        using (Graphics g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            PrintWindow(hwnd, hdc, 0x2);
            g.ReleaseHdc(hdc);
        }
        int first = -1, last = -1;
        int max = Math.Min(1300, bmp.Width);
        for (int x = 300; x < max; x += 4) {
            Color c = bmp.GetPixel(x, y);
            if (c.R > 200 && c.G > 200 && c.B > 200) { if (first < 0) first = x; last = x; }
        }
        return first < 0 ? -1 : (first + last) / 2;
    }
}
"@ -ReferencedAssemblies System.Drawing

[void][Fps]::SetProcessDPIAware()

$name = "anim-probe-$Mode"
Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
Start-Process -FilePath (Join-Path $PSScriptRoot "$name.exe") | Out-Null

$proc = $null
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 100
    $proc = Get-Process -Name $name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) { break }
}
if (-not $proc) { throw 'probe never appeared' }
Start-Sleep -Milliseconds 600   # let it settle

$hwnd = $proc.MainWindowHandle
$rect = New-Object Fps+RECT
[void][Fps]::GetWindowRect($hwnd, [ref]$rect)
$w = $rect.R - $rect.L; $h = $rect.B - $rect.T
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$y = $Lane * 64 + 20 + 28

$positions = New-Object System.Collections.Generic.List[int]
$clock = [Diagnostics.Stopwatch]::StartNew()
while ($clock.ElapsedMilliseconds -lt $DurationMs) {
    $positions.Add([Fps]::BarX($hwnd, $bmp, $y))
}
$clock.Stop()
$bmp.Dispose()
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue

$samples = $positions.Count
$changes = 0
for ($i = 1; $i -lt $samples; $i++) { if ($positions[$i] -ne $positions[$i - 1]) { $changes++ } }
$seconds = $clock.ElapsedMilliseconds / 1000.0

Write-Host ("mode={0}  window {1}x{2}" -f $Mode, $w, $h)
Write-Host ("  samples          {0} in {1:N2}s  ({2:N0} samples/s)" -f $samples, $seconds, ($samples / $seconds))
Write-Host ("  distinct changes {0}  ->  >= {1:N0} presented fps" -f $changes, ($changes / $seconds))
if (($samples / $seconds) -lt 70) {
    Write-Host "  note: sampling is the bottleneck here, so the real rate may be higher"
}
