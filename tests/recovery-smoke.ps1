param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Project
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class RecoverySmokeNative {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr targetDc, uint flags);
    public struct RECT { public int Left, Top, Right, Bottom; }

    public static string Title(IntPtr hwnd) {
        var text = new StringBuilder(512); GetWindowText(hwnd, text, text.Capacity); return text.ToString();
    }
    public static IntPtr FindTopWindow(uint processId, string titlePart) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, _) => {
            uint pid; GetWindowThreadProcessId(hwnd, out pid);
            if (pid == processId && IsWindowVisible(hwnd) && Title(hwnd).Contains(titlePart)) { found = hwnd; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr FindChildById(IntPtr root, int id) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(root, (hwnd, _) => {
            if (GetDlgCtrlID(hwnd) == id && IsWindowVisible(hwnd)) { found = hwnd; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

function Wait-Window([uint32]$ProcessId, [string]$TitlePart, [int]$TimeoutMs = 12000) {
    $until = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $window = [RecoverySmokeNative]::FindTopWindow($ProcessId, $TitlePart)
        if ($window -ne [IntPtr]::Zero) { return $window }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $until)
    throw "Window not found: $TitlePart"
}

function Capture-Window([IntPtr]$Window, [string]$Path) {
    $rect = New-Object RecoverySmokeNative+RECT
    if (-not [RecoverySmokeNative]::GetWindowRect($Window, [ref]$rect)) { throw 'GetWindowRect failed' }
    $bitmap = New-Object System.Drawing.Bitmap([Math]::Max(1, $rect.Right - $rect.Left), [Math]::Max(1, $rect.Bottom - $rect.Top))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $dc = $graphics.GetHdc()
        try { [void][RecoverySmokeNative]::PrintWindow($Window, $dc, 2) } finally { $graphics.ReleaseHdc($dc) }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}

$root = Split-Path -Parent $PSScriptRoot
$damaged = Join-Path $PSScriptRoot 'recovery-smoke.mzoy'
$backup = $damaged + '.bak'
$autosave = Join-Path $PSScriptRoot 'recovery-smoke.autosave.mzoy'
$sourceBytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Project))
foreach ($path in @($damaged, $backup, $autosave)) { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force } }
[System.IO.File]::WriteAllBytes($backup, $sourceBytes)
[System.IO.File]::WriteAllText($damaged, '<MezozoyProject><broken>', [System.Text.Encoding]::UTF8)

$process = Start-Process -FilePath $Exe -ArgumentList ('"' + $damaged + '"') -PassThru
try {
    $dialog = Wait-Window -ProcessId ([uint32]$process.Id) -TitlePart 'Mezozoy'
    Capture-Window $dialog (Join-Path $root 'smoke-recovery.png')
    $primary = [RecoverySmokeNative]::FindChildById($dialog, 1)
    if ($primary -eq [IntPtr]::Zero) { throw 'Recovery action was not found' }
    [void][RecoverySmokeNative]::SendMessage($primary, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)

    $main = Wait-Window -ProcessId ([uint32]$process.Id) -TitlePart 'Mezozoy'
    Start-Sleep -Milliseconds 500
    [void][RecoverySmokeNative]::SendMessage($main, 0x0111, [IntPtr]6190, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 900
    [void][RecoverySmokeNative]::PostMessage($main, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(6000)) { $process.Kill(); throw 'Recovered application did not close' }

    $xml = Get-Content -LiteralPath $damaged -Raw -Encoding UTF8
    if (-not $xml.Contains('<MezozoyProject')) { throw 'Recovered project was not saved back to the original path' }
    [PSCustomObject]@{ ExitCode = $process.ExitCode; RecoveredProject = $damaged; Screenshot = (Join-Path $root 'smoke-recovery.png') } | Format-List
} finally {
    if (-not $process.HasExited) { $process.Kill() }
}
