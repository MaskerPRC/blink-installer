# Compares presented frame rate across miniblink builds.
#
# The renderer is loaded by path at runtime, so swapping node.dll and
# rebuilding the probe is a clean A/B. Restores the original on the way out.
#
# Measurement is inlined rather than delegated to measure-frames-min.ps1: a
# child script's Write-Host output does not come back through the pipeline in
# Windows PowerShell, which silently turned every result into -1.

param(
    [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Candidates,
    [ValidateSet('win', 'full')][string]$Mode = 'win',
    [int]$DurationMs = 2000
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class Bench {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
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
[void][Bench]::SetProcessDPIAware()

$here = $PSScriptRoot
$runtimeDll = [IO.Path]::GetFullPath((Join-Path $here '..\..\packages\runtime\bin\node.dll'))
$makensis = [IO.Path]::GetFullPath((Join-Path $here '..\..\packages\runtime\nsis\Bin\makensis.exe'))
$backup = Join-Path $env:TEMP 'blinkkit-node-dll.backup'
$exeName = "fps-min-$Mode"

if (-not (Test-Path $backup)) { Copy-Item $runtimeDll $backup -Force }

function Measure-Build {
    Get-Process $exeName -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 400
    Start-Process -FilePath (Join-Path $here "$exeName.exe") | Out-Null

    $proc = $null
    for ($i = 0; $i -lt 50; $i++) {
        Start-Sleep -Milliseconds 100
        $proc = Get-Process -Name $exeName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) { break }
    }
    if (-not $proc -or $proc.MainWindowHandle -eq [IntPtr]::Zero) { return -1 }
    Start-Sleep -Milliseconds 700

    $rect = New-Object Bench+RECT
    [void][Bench]::GetWindowRect($proc.MainWindowHandle, [ref]$rect)
    $w = $rect.R - $rect.L; $h = $rect.B - $rect.T
    if ($w -le 0 -or $h -le 0) { Stop-Process -Id $proc.Id -Force; return -1 }

    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $seen = New-Object System.Collections.Generic.HashSet[int]
    $samples = 0
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.ElapsedMilliseconds -lt $DurationMs) {
        [void]$seen.Add([Bench]::Swatch($proc.MainWindowHandle, $bmp))
        $samples++
    }
    $clock.Stop()
    $bmp.Dispose()
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue

    $seconds = $clock.ElapsedMilliseconds / 1000.0
    return [pscustomobject]@{
        Fps = [int]($seen.Count / $seconds)
        SampleRate = [int]($samples / $seconds)
    }
}

$results = @()
try {
    foreach ($label in $Candidates.Keys) {
        $dll = $Candidates[$label]
        if (-not (Test-Path $dll)) { Write-Host "  skip $label (missing)"; continue }

        Copy-Item $dll $runtimeDll -Force
        Push-Location $here
        # Built as an explicit array: a single-element array assigned through
        # an if/else collapses to a string, and splatting a string passes each
        # character as its own argument.
        $nsisArgs = New-Object System.Collections.ArrayList
        if ($Mode -eq 'full') { [void]$nsisArgs.Add('/DFULLSCREEN') }
        [void]$nsisArgs.Add('fps-min.nsi')
        & $makensis $nsisArgs.ToArray() | Out-Null
        Pop-Location

        $m = $null
        try { $m = Measure-Build } catch { }
        $fps = if ($m -and $m.Fps) { $m.Fps } else { -1 }
        $sampled = if ($m -and $m.SampleRate) { $m.SampleRate } else { 0 }

        $results += [pscustomobject]@{
            Build = $label
            SizeMB = [Math]::Round((Get-Item $dll).Length / 1MB, 1)
            PresentedFps = $fps
            SampledPerSec = $sampled
        }
        Write-Host ("  {0,-22} {1,5} MB   {2,3} fps   (sampled {3}/s)" -f `
            $label, [Math]::Round((Get-Item $dll).Length / 1MB, 1), $fps, $sampled)
    }
}
finally {
    Copy-Item $backup $runtimeDll -Force
    Push-Location $here
    & $makensis 'fps-min.nsi' | Out-Null
    Pop-Location
    Write-Host "restored bundled node.dll"
}

Write-Host ""
$results | Sort-Object PresentedFps -Descending | Format-Table -AutoSize
