# Counts presented frames exactly.
#
# The probe page paints a swatch whose colour encodes its requestAnimationFrame
# counter, so every presented frame carries a unique value. Counting distinct
# colours in rapid captures counts frames without the quantisation error that
# position-based measurement suffers from 鈥?two consecutive frames of a slowly
# moving bar can round to the same pixel and look like one frame.
#
# Reports both the sampling rate and the frame rate, because if they are equal
# the measurement is sampling-limited and the true rate is higher.

param(
    [ValidateSet('win', 'full')][string]$Mode = 'win',
    [int]$DurationMs = 2000
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class Frm {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    // Captures the window and returns the swatch colour packed into an int.
    // The swatch is 120x120 at the top-right corner.
    public static int Swatch(IntPtr hwnd, Bitmap bmp) {
        using (Graphics g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            PrintWindow(hwnd, hdc, 0x2);
            g.ReleaseHdc(hdc);
        }
        Color c = bmp.GetPixel(bmp.Width - 60, 60);
        return (c.R << 16) | (c.G << 8) | c.B;
    }
}
"@ -ReferencedAssemblies System.Drawing

[void][Frm]::SetProcessDPIAware()

$name = "fps-min-$Mode"
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
Start-Sleep -Milliseconds 700

$hwnd = $proc.MainWindowHandle
$rect = New-Object Frm+RECT
[void][Frm]::GetWindowRect($hwnd, [ref]$rect)
$w = $rect.R - $rect.L; $h = $rect.B - $rect.T
$bmp = New-Object System.Drawing.Bitmap($w, $h)

$seen = New-Object System.Collections.Generic.HashSet[int]
$samples = 0
$clock = [Diagnostics.Stopwatch]::StartNew()
while ($clock.ElapsedMilliseconds -lt $DurationMs) {
    [void]$seen.Add([Frm]::Swatch($hwnd, $bmp))
    $samples++
}
$clock.Stop()
$bmp.Dispose()
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue

$seconds = $clock.ElapsedMilliseconds / 1000.0
$sampleRate = $samples / $seconds
$frameRate = $seen.Count / $seconds

Write-Host ("mode={0}  window {1}x{2}  drawMinInterval={3}" -f `
    $Mode, $w, $h, $(if ($env:BLINKKIT_DRAW_INTERVAL) { $env:BLINKKIT_DRAW_INTERVAL } else { 'default' }))
Write-Host ("  sampled  {0,5:N0} /s" -f $sampleRate)
Write-Host ("  presented {0,4:N0} /s   ({1} unique frames in {2:N2}s)" -f $frameRate, $seen.Count, $seconds)
if ($frameRate -ge $sampleRate * 0.9) {
    Write-Host "  -> sampling-limited; the real rate is at least this and probably higher"
}

