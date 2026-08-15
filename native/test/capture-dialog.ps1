# Captures the in-page confirmation dialog.
#
# The installer arms a close guard, so WM_CLOSE does not destroy the window —
# it raises `window-closing` in the page, which answers with ui.messageBox.
# That is the only way to see the dialog without a human clicking, and it is
# also the exact path a user takes when they hit the X mid-install.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [int]$SettleMs = 6000,
    [int]$AfterCloseMs = 900,
    [string]$OutDir = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class Dlg {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr wp, IntPtr lp, uint flags, uint ms, out IntPtr res);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
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

[void][Dlg]::SetProcessDPIAware()
New-Item -ItemType Directory -Force $OutDir | Out-Null

$name = [IO.Path]::GetFileNameWithoutExtension($Exe)
Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

$proc = Start-Process $Exe -PassThru
try {
    # Wait for the splash to finish and the card to settle.
    Start-Sleep -Milliseconds $SettleMs
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { throw "no main window after ${SettleMs}ms" }

    $r = New-Object Dlg+RECT
    [void][Dlg]::GetWindowRect($hwnd, [ref]$r)
    $w = $r.R - $r.L; $h = $r.B - $r.T
    "window ${w}x${h}"

    [Dlg]::Print($hwnd, $w, $h).Save("$OutDir\dialog-before.png", [Drawing.Imaging.ImageFormat]::Png)

    # WM_CLOSE, which the close guard turns into a page event.
    [void][Dlg]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds $AfterCloseMs

    [Dlg]::Print($hwnd, $w, $h).Save("$OutDir\dialog-after.png", [Drawing.Imaging.ImageFormat]::Png)
    "captured"
}
finally {
    Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force
}
