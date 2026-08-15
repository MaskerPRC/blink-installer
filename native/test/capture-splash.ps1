# Captures the installer's splash at precise offsets using PrintWindow.
#
# BitBlt screen grabs of a layered window are timing- and z-order-sensitive;
# PrintWindow with PW_RENDERFULLCONTENT asks the window to render itself and is
# what should be used to judge whether an animation is visible.

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [int[]]$AtMs = @(250, 550, 850, 1150, 1450, 1750, 2050, 2400),
    [string]$OutDir = $PSScriptRoot,
    [int]$Scale = 1000,
    # NSIS uninstallers copy themselves to %TEMP% and relaunch, so the process
    # to watch is `Un`, not the name of the file that was started.
    [string]$ProcessName
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class Snap {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public static Bitmap Print(IntPtr hwnd, int w, int h) {
        Bitmap bmp = new Bitmap(w, h);
        using (Graphics g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            PrintWindow(hwnd, hdc, 0x2);
            g.ReleaseHdc(hdc);
        }
        return bmp;
    }
}
"@ -ReferencedAssemblies System.Drawing

[void][Snap]::SetProcessDPIAware()

$name = if ($ProcessName) { $ProcessName } else { [IO.Path]::GetFileNameWithoutExtension($Exe) }
Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

Start-Process -FilePath $Exe | Out-Null

# Start the clock only once the window exists.
$proc = $null
for ($i = 0; $i -lt 80; $i++) {
    Start-Sleep -Milliseconds 50
    $proc = Get-Process -Name $name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) { break }
}
if (-not $proc) { throw 'window never appeared' }

$clock = [Diagnostics.Stopwatch]::StartNew()
$hwnd = $proc.MainWindowHandle
$rect = New-Object Snap+RECT
[void][Snap]::GetWindowRect($hwnd, [ref]$rect)
Write-Host ("window {0}x{1} at {2},{3}" -f ($rect.R - $rect.L), ($rect.B - $rect.T), $rect.L, $rect.T)

foreach ($at in $AtMs) {
    while ($clock.ElapsedMilliseconds -lt $at) { Start-Sleep -Milliseconds 4 }
    # Re-read: the window resizes when the splash hands off.
    [void][Snap]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.R - $rect.L; $h = $rect.B - $rect.T
    if ($w -le 0 -or $h -le 0) { continue }

    $bmp = [Snap]::Print($hwnd, $w, $h)
    $sw = [Math]::Min($Scale, $w)
    $sh = [int]($sw * $h / $w)
    $small = New-Object System.Drawing.Bitmap($sw, $sh)
    $g = [System.Drawing.Graphics]::FromImage($small)
    $g.InterpolationMode = 'HighQualityBicubic'
    # Dark checker behind, so semi-transparent pixels are visible in the file
    # instead of being flattened onto black.
    $g.Clear([System.Drawing.Color]::FromArgb(255, 24, 26, 30))
    $g.DrawImage($bmp, 0, 0, $sw, $sh)
    $small.Save((Join-Path $OutDir "splash-$at.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $small.Dispose(); $bmp.Dispose()
    Write-Host ("  t={0,5}ms  {1}x{2}" -f $at, $w, $h)
}

Start-Sleep -Milliseconds 500
Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force
