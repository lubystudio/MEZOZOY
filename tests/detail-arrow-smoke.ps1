param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Project,
    [Parameter(Mandatory = $true)][string]$ScreenshotDirectory
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class NativeDetailArrowSmoke {
    public delegate bool EnumWindowsProc(IntPtr window, IntPtr value);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr value);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc callback, IntPtr value);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    public static IntPtr FindTopWindow(uint expectedProcessId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, value) => {
            uint processId;
            GetWindowThreadProcessId(window, out processId);
            if (processId == expectedProcessId && IsWindowVisible(window)) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindVisibleChildById(IntPtr parent, int id) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (window, value) => {
            if (GetDlgCtrlID(window) == id && IsWindowVisible(window)) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr TreeRoot(IntPtr tree) { return SendMessage(tree, 0x110A, IntPtr.Zero, IntPtr.Zero); }
    public static IntPtr TreeNext(IntPtr tree, IntPtr item) { return SendMessage(tree, 0x110A, (IntPtr)1, item); }
    public static IntPtr TreeChild(IntPtr tree, IntPtr item) { return SendMessage(tree, 0x110A, (IntPtr)4, item); }
    public static IntPtr FirstLeaf(IntPtr tree, IntPtr root) {
        IntPtr current = TreeChild(tree, root);
        if (current == IntPtr.Zero) return IntPtr.Zero;
        while (true) {
            IntPtr child = TreeChild(tree, current);
            if (child == IntPtr.Zero) return current;
            current = child;
        }
    }
    public static void SelectTreeItem(IntPtr tree, IntPtr item) { SendMessage(tree, 0x110B, (IntPtr)9, item); }
}
'@

function Wait-Window([uint32]$ProcessId, [int]$TimeoutMs = 15000) {
    $until = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $window = [NativeDetailArrowSmoke]::FindTopWindow($ProcessId)
        if ($window -ne [IntPtr]::Zero) { return $window }
        Start-Sleep -Milliseconds 80
    } while ([DateTime]::UtcNow -lt $until)
    throw 'Mezozoy main window was not found'
}

function Assert-AccentPixels([string]$Path, [IntPtr]$Main, [IntPtr]$Button) {
    $mainRect = New-Object NativeDetailArrowSmoke+Rect
    $buttonRect = New-Object NativeDetailArrowSmoke+Rect
    if (-not [NativeDetailArrowSmoke]::GetWindowRect($Main, [ref]$mainRect) -or
        -not [NativeDetailArrowSmoke]::GetWindowRect($Button, [ref]$buttonRect)) {
        throw 'Could not read arrow geometry'
    }

    $bitmap = [System.Drawing.Bitmap]::new($Path)
    try {
        $left = [Math]::Max(0, $buttonRect.Left - $mainRect.Left)
        $top = [Math]::Max(0, $buttonRect.Top - $mainRect.Top)
        $right = [Math]::Min($bitmap.Width, $buttonRect.Right - $mainRect.Left)
        $bottom = [Math]::Min($bitmap.Height, $buttonRect.Bottom - $mainRect.Top)
        $accentPixels = 0
        for ($y = $top; $y -lt $bottom; ++$y) {
            for ($x = $left; $x -lt $right; ++$x) {
                $pixel = $bitmap.GetPixel($x, $y)
                if ($pixel.B -gt ($pixel.R + 35) -and $pixel.B -gt ($pixel.G + 55)) { ++$accentPixels }
            }
        }
        if ($accentPixels -lt 12) { throw "Arrow is not visibly accented without hover: $accentPixels pixels" }
        return $accentPixels
    } finally {
        $bitmap.Dispose()
    }
}

$root = Split-Path -Parent $PSScriptRoot
$capture = Join-Path $root 'build\MezozoyCaptureWindow.exe'
$fixture = Join-Path $env:TEMP 'Mezozoy-detail-arrow-smoke.mzoy'
Copy-Item -LiteralPath $Project -Destination $fixture -Force
[void](New-Item -ItemType Directory -Path $ScreenshotDirectory -Force)

$process = Start-Process -FilePath $Exe -ArgumentList ('"' + $fixture + '"') -PassThru
try {
    $main = Wait-Window ([uint32]$process.Id)
    [void][NativeDetailArrowSmoke]::ShowWindow($main, 3)
    [void][NativeDetailArrowSmoke]::SetForegroundWindow($main)
    [void][NativeDetailArrowSmoke]::SendMessage($main, 0x0111, [IntPtr]6103, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 450

    $tree = [NativeDetailArrowSmoke]::FindVisibleChildById($main, 2101)
    if ($tree -eq [IntPtr]::Zero) { throw 'Development tree was not found' }
    $projectRoot = [NativeDetailArrowSmoke]::TreeRoot($tree)
    $charactersRoot = [NativeDetailArrowSmoke]::TreeNext($tree, $projectRoot)
    $worldsRoot = [NativeDetailArrowSmoke]::TreeNext($tree, $charactersRoot)
    $locationsRoot = [NativeDetailArrowSmoke]::TreeNext($tree, $worldsRoot)
    $character = [NativeDetailArrowSmoke]::FirstLeaf($tree, $charactersRoot)
    $location = [NativeDetailArrowSmoke]::FirstLeaf($tree, $locationsRoot)

    $results = @()
    foreach ($entry in @(
        @{ Name = 'character-arrow'; Item = $character; ButtonId = 2109 },
        @{ Name = 'location-arrow'; Item = $location; ButtonId = 2110 }
    )) {
        if ($entry.Item -eq [IntPtr]::Zero) { throw "$($entry.Name) fixture item was not found" }
        [NativeDetailArrowSmoke]::SelectTreeItem($tree, $entry.Item)
        [void][NativeDetailArrowSmoke]::SetCursorPos(0, 0)
        Start-Sleep -Milliseconds 350
        $button = [NativeDetailArrowSmoke]::FindVisibleChildById($main, $entry.ButtonId)
        if ($button -eq [IntPtr]::Zero) { throw "$($entry.Name) button was not visible" }
        $path = Join-Path $ScreenshotDirectory ($entry.Name + '.png')
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
        & $capture 'Mezozoy' $path
        if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -lt 1000) {
            throw "$($entry.Name) screenshot failed"
        }
        $pixels = Assert-AccentPixels $path $main $button
        $results += [PSCustomObject]@{ State = $entry.Name; AccentPixels = $pixels; Screenshot = $path }
    }
    $results | Format-Table -AutoSize
} finally {
    if (-not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
}
