param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Project
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class NativeWindowMoveSmoke {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, System.Text.StringBuilder className, int maxCount);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);

    public static IntPtr FindMainWindow(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, _) => {
            uint pid;
            GetWindowThreadProcessId(hwnd, out pid);
            var className = new System.Text.StringBuilder(128);
            GetClassName(hwnd, className, className.Capacity);
            if (pid == processId && IsWindowVisible(hwnd) && className.ToString() == "Mezozoy.MainWindow") {
                found = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

function Wait-MainWindow([uint32]$ProcessId, [int]$TimeoutMs = 10000) {
    $until = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $window = [NativeWindowMoveSmoke]::FindMainWindow($ProcessId)
        if ($window -ne [IntPtr]::Zero) { return $window }
        Start-Sleep -Milliseconds 80
    } while ([DateTime]::UtcNow -lt $until)
    throw 'Main window was not created in time'
}

function Assert-Responsive([IntPtr]$Window, [string]$Step) {
    $response = [IntPtr]::Zero
    $ok = [NativeWindowMoveSmoke]::SendMessageTimeout($Window, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 1200, [ref]$response)
    if ($ok -eq [IntPtr]::Zero) { throw "Window stopped responding: $Step" }
}

$screens = [System.Windows.Forms.Screen]::AllScreens
if ($screens.Count -lt 1) { throw 'Windows did not report any displays' }
$process = Start-Process -FilePath $Exe -ArgumentList ('"' + $Project + '"') -PassThru
$window = [IntPtr]::Zero
$measurements = New-Object System.Collections.Generic.List[double]

try {
    $window = Wait-MainWindow ([uint32]$process.Id)
    Start-Sleep -Milliseconds 450
    Assert-Responsive $window 'startup'

    # Cards, screenplay and settings cover the expensive canvas, RichEdit and
    # control-heavy layouts. The synthetic native size/move loop uses exactly
    # the messages Windows sends while the user holds the title bar.
    foreach ($pageCommand in @(6104, 6105, 6110)) {
        [void][NativeWindowMoveSmoke]::SendMessage($window, 0x0111, [IntPtr]$pageCommand, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 250
        [void][NativeWindowMoveSmoke]::SendMessage($window, 0x0231, [IntPtr]::Zero, [IntPtr]::Zero) # WM_ENTERSIZEMOVE
        $enteredState = [NativeWindowMoveSmoke]::SendMessage($window, 0x8000 + 91, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
        if (($enteredState -band 1) -eq 0) { throw "Move loop did not start on page $pageCommand" }

        for ($index = 0; $index -lt 16; $index++) {
            $screen = $screens[$index % $screens.Count].WorkingArea
            $width = [Math]::Min(1460, $screen.Width - 80) - (($index % 2) * 24)
            $height = [Math]::Min(900, $screen.Height - 70) - (($index % 3) * 18)
            $x = $screen.X + [Math]::Max(20, [int](($screen.Width - $width) / 2))
            $y = $screen.Y + [Math]::Max(20, [int](($screen.Height - $height) / 2))
            $watch = [Diagnostics.Stopwatch]::StartNew()
            if (-not [NativeWindowMoveSmoke]::SetWindowPos($window, [IntPtr]::Zero, $x, $y, $width, $height, 0x0014)) {
                throw "SetWindowPos failed on page $pageCommand"
            }
            $watch.Stop()
            $measurements.Add($watch.Elapsed.TotalMilliseconds)
            Assert-Responsive $window "page=$pageCommand move=$index"
        }

        $activeState = [NativeWindowMoveSmoke]::SendMessage($window, 0x8000 + 91, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
        if (($activeState -band 1) -eq 0 -or ($activeState -band 2) -eq 0) {
            throw "Heavy workspace layout was not deferred on page $pageCommand (state=$activeState)"
        }
        [void][NativeWindowMoveSmoke]::SendMessage($window, 0x0232, [IntPtr]::Zero, [IntPtr]::Zero) # WM_EXITSIZEMOVE
        $finalState = [NativeWindowMoveSmoke]::SendMessage($window, 0x8000 + 91, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
        if ($finalState -ne 0) { throw "Move state leaked after release on page $pageCommand (state=$finalState)" }
        $dpi = [NativeWindowMoveSmoke]::SendMessage($window, 0x8000 + 90, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
        if ($dpi -lt 96) { throw "Invalid monitor DPI reported after move: $dpi" }
        Assert-Responsive $window "page=$pageCommand finalized"
    }

    $average = ($measurements | Measure-Object -Average).Average
    $maximum = ($measurements | Measure-Object -Maximum).Maximum
    if ($average -gt 80) { throw "Window movement is still too slow: average $([Math]::Round($average, 1)) ms" }
    Write-Host "WINDOW_MOVE_SMOKE_OK displays=$($screens.Count) moves=$($measurements.Count) averageMs=$([Math]::Round($average, 2)) maxMs=$([Math]::Round($maximum, 2))"
}
finally {
    if ($window -ne [IntPtr]::Zero) {
        [void][NativeWindowMoveSmoke]::SendMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    }
    if (-not $process.HasExited) {
        if (-not $process.WaitForExit(3000)) { $process.Kill() }
    }
}
