# Counts how many dialogs one fs.pickDirectory request actually opens, and
# which one it is.
#
# Two bugs met here. The picker chained IFileOpenDialog to SHBrowseForFolder
# with `&&`, so cancelling the modern dialog opened the legacy one behind it —
# declining escalated instead of ending the request. And nothing initialized
# COM, so whether the modern dialog appeared at all depended on whether some
# other component had happened to initialize an apartment on the thread first.
#
# So the probe checks both: which dialog opens, and whether declining it opens
# another. Clicks by real mouse input at the button's position rather than
# keyboard navigation, which does not reliably reach a page's own focus order.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [int]$SettleMs = 6500,
    # Browse button centre, as a fraction of the window. Matches the template
    # and the chinaClaw page; pass your own for a different layout.
    [double]$BrowseX = 0.907,
    [double]$BrowseY = 0.710
)

$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public class P {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int c);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int c);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }

    public static void Click(int x, int y) {
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(120);
        mouse_event(0x0002, 0, 0, 0, IntPtr.Zero);   // LEFTDOWN
        System.Threading.Thread.Sleep(60);
        mouse_event(0x0004, 0, 0, 0, IntPtr.Zero);   // LEFTUP
    }

    // Any visible top-level window of the process that is not the installer's
    // own wkeWebWindow is a dialog it put up.
    public static List<string> Dialogs(uint pid) {
        var found = new List<string>();
        EnumWindows((h, l) => {
            uint wp; GetWindowThreadProcessId(h, out wp);
            if (wp != pid || !IsWindowVisible(h)) return true;
            var cls = new StringBuilder(256); GetClassName(h, cls, 256);
            if (cls.ToString() == "wkeWebWindow") return true;
            var txt = new StringBuilder(256); GetWindowText(h, txt, 256);
            RECT r; GetWindowRect(h, out r);
            int w = r.R - r.L, ht = r.B - r.T;
            // SHBrowseForFolder is the narrow tree box; IFileOpenDialog is a
            // full explorer view and much wider.
            string kind = (w < 600) ? "legacy SHBrowseForFolder" : "modern IFileOpenDialog";
            found.Add(string.Format("{0,-8} {1,4}x{2,-4} \"{3}\"  <- {4}",
                cls.ToString(), w, ht, txt.ToString(), kind));
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@

$name = [IO.Path]::GetFileNameWithoutExtension($Exe)
Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

$proc = Start-Process $Exe -PassThru
try {
    Start-Sleep -Milliseconds $SettleMs
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { throw "no window after ${SettleMs}ms" }
    [void][P]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 400

    $r = New-Object P+RECT
    [void][P]::GetWindowRect($hwnd, [ref]$r)
    $x = $r.L + [int](($r.R - $r.L) * $BrowseX)
    $y = $r.T + [int](($r.B - $r.T) * $BrowseY)
    "window $($r.R - $r.L)x$($r.B - $r.T), clicking browse at ($x,$y)"

    [P]::Click($x, $y)
    Start-Sleep -Milliseconds 2800

    $opened = @([P]::Dialogs($proc.Id))
    "opened: $($opened.Count)"
    $opened | ForEach-Object { "    $_" }
    if ($opened.Count -eq 0) { "  INCONCLUSIVE - no dialog; the click missed"; return }

    $wsh = New-Object -ComObject WScript.Shell
    $wsh.SendKeys('{ESC}')
    Start-Sleep -Milliseconds 2200

    $after = @([P]::Dialogs($proc.Id))
    "after Escape: $($after.Count)"
    $after | ForEach-Object { "    $_" }
    if ($after.Count -eq 0) { "  PASS - declining ended the request" }
    else { "  FAIL - a second dialog stands after cancelling" }
}
finally {
    Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force
}
