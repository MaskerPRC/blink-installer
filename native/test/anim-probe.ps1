# Measures which animation techniques actually reach the screen on miniblink's
# layered window.
#
# For each lane, the window is captured at several times and the horizontal
# position of the white bar is located. A lane "works" when that position
# changes between captures — which is the only meaningful test, because a
# technique can advance its computed style perfectly and never be presented.
#
# Two capture methods are used deliberately:
#   BitBlt       - what a normal screen grab sees
#   PrintWindow  - asks the window to render itself, with PW_RENDERFULLCONTENT
# They can disagree on layered windows, and knowing which is which prevents
# drawing conclusions from the wrong one.

param(
    [ValidateSet('win', 'full')]
    [string]$Mode = 'win',
    [int[]]$AtMs = @(300, 800, 1300, 1800)
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;

public class Cap {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    // PW_RENDERFULLCONTENT (0x2) is what makes this work for layered and
    // composited windows; without it the call returns an empty bitmap.
    public static Bitmap Print(IntPtr hwnd, int w, int h) {
        Bitmap bmp = new Bitmap(w, h);
        using (Graphics g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            PrintWindow(hwnd, hdc, 0x2);
            g.ReleaseHdc(hdc);
        }
        return bmp;
    }

    public static Bitmap Screen(int x, int y, int w, int h) {
        Bitmap bmp = new Bitmap(w, h);
        using (Graphics g = Graphics.FromImage(bmp)) {
            g.CopyFromScreen(x, y, 0, 0, new Size(w, h));
        }
        return bmp;
    }
}
"@ -ReferencedAssemblies System.Drawing

[void][Cap]::SetProcessDPIAware()

$lanes = @(
    'css @keyframes transform',
    'css @keyframes left',
    'css transition transform',
    'css transition left',
    'js rAF transform',
    'js rAF left',
    'canvas 2d',
    'svg SMIL'
)

# Finds the centre x of the brightest run in a lane's band. The bar is pure
# white on either transparency or a 5% wash, so a high threshold is safe.
function Get-BarX($bmp, $laneIndex) {
    $y = $laneIndex * 64 + 20 + 28   # lane top + bar centre
    if ($y -ge $bmp.Height) { return -1 }
    $first = -1; $last = -1
    for ($x = 300; $x -lt [Math]::Min(1300, $bmp.Width); $x += 3) {
        $c = $bmp.GetPixel($x, $y)
        if ($c.R -gt 200 -and $c.G -gt 200 -and $c.B -gt 200) {
            if ($first -lt 0) { $first = $x }
            $last = $x
        }
    }
    if ($first -lt 0) { return -1 }
    return [int](($first + $last) / 2)
}

$exe = Join-Path $PSScriptRoot "anim-probe-$Mode.exe"
if (-not (Test-Path $exe)) { throw "not built: $exe" }

Get-Process 'anim-probe-win', 'anim-probe-full' -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
Start-Process -FilePath $exe | Out-Null

# Wait for the window, then start the clock — capturing before it exists is
# how an earlier run "proved" that nothing rendered.
$proc = $null
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 100
    $proc = Get-Process -Name "anim-probe-$Mode" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) { break }
}
if (-not $proc) { throw 'probe process never appeared' }

$hwnd = $proc.MainWindowHandle
$rect = New-Object Cap+RECT
[void][Cap]::GetWindowRect($hwnd, [ref]$rect)
$w = $rect.R - $rect.L; $h = $rect.B - $rect.T
Write-Host ("window {0}x{1} at {2},{3}  visible={4}" -f $w, $h, $rect.L, $rect.T, [Cap]::IsWindowVisible($hwnd))

$samples = @{}
foreach ($method in @('PrintWindow', 'BitBlt')) { $samples[$method] = @{} }

$clock = [Diagnostics.Stopwatch]::StartNew()
foreach ($at in $AtMs) {
    while ($clock.ElapsedMilliseconds -lt $at) { Start-Sleep -Milliseconds 5 }
    foreach ($method in @('PrintWindow', 'BitBlt')) {
        $bmp = if ($method -eq 'PrintWindow') { [Cap]::Print($hwnd, $w, $h) }
               else { [Cap]::Screen($rect.L, $rect.T, $w, $h) }
        for ($i = 0; $i -lt $lanes.Count; $i++) {
            if (-not $samples[$method].ContainsKey($i)) { $samples[$method][$i] = @() }
            $samples[$method][$i] += (Get-BarX $bmp $i)
        }
        $bmp.Save((Join-Path $PSScriptRoot "probe-$Mode-$method-$at.png"))
        $bmp.Dispose()
    }
}

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("mode={0}  samples at {1} ms" -f $Mode, ($AtMs -join ', '))
Write-Host ("{0,-26} {1,-34} {2}" -f 'technique', 'PrintWindow x-positions', 'verdict')
Write-Host ('-' * 84)
for ($i = 0; $i -lt $lanes.Count; $i++) {
    $pw = $samples['PrintWindow'][$i]
    $bb = $samples['BitBlt'][$i]
    $seen = @($pw | Where-Object { $_ -ge 0 })
    $moved = ($seen | Select-Object -Unique).Count -gt 1
    $verdict = if ($seen.Count -eq 0) { 'not rendered' }
               elseif ($moved) { 'ANIMATES' }
               else { 'rendered but frozen' }
    $bbNote = if (($bb | Where-Object { $_ -ge 0 }).Count -eq 0) { '  [BitBlt saw nothing]' } else { '' }
    Write-Host ("{0,-26} {1,-34} {2}{3}" -f $lanes[$i], ($pw -join ' '), $verdict, $bbNote)
}
