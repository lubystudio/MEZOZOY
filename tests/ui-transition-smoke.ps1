param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Project,
    [Parameter(Mandatory = $true)][string]$OutputVideo,
    [Parameter(Mandatory = $true)][string]$ScreenshotDirectory
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class NativeTransitionSmoke {
    public delegate bool EnumWindowsProc(IntPtr window, IntPtr value);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr value);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc callback, IntPtr value);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    public static IntPtr FindTopWindow(uint expectedProcessId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, value) => {
            uint processId;
            GetWindowThreadProcessId(window, out processId);
            if (processId == expectedProcessId && IsWindowVisible(window)) {
                found = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindVisibleChildById(IntPtr parent, int id) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (window, value) => {
            if (GetDlgCtrlID(window) == id && IsWindowVisible(window)) {
                found = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr TreeRoot(IntPtr tree) {
        return SendMessage(tree, 0x110A, IntPtr.Zero, IntPtr.Zero);
    }

    public static IntPtr TreeNext(IntPtr tree, IntPtr item) {
        return SendMessage(tree, 0x110A, (IntPtr)1, item);
    }

    public static IntPtr TreeChild(IntPtr tree, IntPtr item) {
        return SendMessage(tree, 0x110A, (IntPtr)4, item);
    }

    public static IntPtr FirstLeaf(IntPtr tree, IntPtr root) {
        IntPtr current = TreeChild(tree, root);
        if (current == IntPtr.Zero) return IntPtr.Zero;
        while (true) {
            IntPtr child = TreeChild(tree, current);
            if (child == IntPtr.Zero) return current;
            current = child;
        }
    }

    public static void SelectTreeItem(IntPtr tree, IntPtr item) {
        SendMessage(tree, 0x110B, (IntPtr)9, item);
    }

    public static void Click(IntPtr button) {
        SendMessage(button, 0x00F5, IntPtr.Zero, IntPtr.Zero);
    }

    public static void SelectListItem(IntPtr list, int id, int index) {
        SendMessage(list, 0x0186, (IntPtr)index, IntPtr.Zero);
        SendMessage(GetParent(list), 0x0111, (IntPtr)(id | (1 << 16)), list);
    }
}
'@

function Wait-Window([uint32]$ProcessId, [int]$TimeoutMs = 15000) {
    $until = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $window = [NativeTransitionSmoke]::FindTopWindow($ProcessId)
        if ($window -ne [IntPtr]::Zero) { return $window }
        Start-Sleep -Milliseconds 80
    } while ([DateTime]::UtcNow -lt $until)
    throw 'Mezozoy main window was not found'
}

function Get-DevelopmentItems([IntPtr]$Main) {
    $tree = [NativeTransitionSmoke]::FindVisibleChildById($Main, 2101)
    if ($tree -eq [IntPtr]::Zero) { throw 'Development tree was not found' }
    $project = [NativeTransitionSmoke]::TreeRoot($tree)
    $charactersRoot = [NativeTransitionSmoke]::TreeNext($tree, $project)
    $worldsRoot = [NativeTransitionSmoke]::TreeNext($tree, $charactersRoot)
    $locationsRoot = [NativeTransitionSmoke]::TreeNext($tree, $worldsRoot)
    $referencesRoot = [NativeTransitionSmoke]::TreeNext($tree, $locationsRoot)
    $character = [NativeTransitionSmoke]::FirstLeaf($tree, $charactersRoot)
    $location = [NativeTransitionSmoke]::FirstLeaf($tree, $locationsRoot)
    $reference = [NativeTransitionSmoke]::FirstLeaf($tree, $referencesRoot)
    if ($character -eq [IntPtr]::Zero -or $location -eq [IntPtr]::Zero -or $reference -eq [IntPtr]::Zero) {
        throw 'The transition fixture does not contain a character, location, and reference'
    }
    return @($tree, $character, $location, $reference)
}

function Capture-ScreenWindow([IntPtr]$Window, [string]$Path) {
    $rect = New-Object NativeTransitionSmoke+Rect
    if (-not [NativeTransitionSmoke]::GetWindowRect($Window, [ref]$rect)) { throw 'GetWindowRect failed' }
    $width = [Math]::Max(1, $rect.Right - $rect.Left)
    $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$root = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $env:TEMP 'Mezozoy-ui-transition.mzoy'
$ffmpeg = (Get-Command ffmpeg.exe -ErrorAction Stop).Source
[void](New-Item -ItemType Directory -Path $ScreenshotDirectory -Force)
[void](New-Item -ItemType Directory -Path (Split-Path -Parent $OutputVideo) -Force)
Copy-Item -LiteralPath $Project -Destination $fixture -Force

$process = Start-Process -FilePath $Exe -ArgumentList ('"' + $fixture + '"') -PassThru
$recorder = $null
$main = [IntPtr]::Zero
try {
    Write-Host 'stage: wait-window'
    $main = Wait-Window ([uint32]$process.Id)
    Write-Host 'stage: open-development'
    [void][NativeTransitionSmoke]::ShowWindow($main, 3)
    [void][NativeTransitionSmoke]::SetForegroundWindow($main)
    [void][NativeTransitionSmoke]::SendMessage($main, 0x0111, [IntPtr]6103, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 450

    Write-Host 'stage: inspect-tree'
    $items = Get-DevelopmentItems $main
    $tree = $items[0]
    $character = $items[1]
    $location = $items[2]
    $reference = $items[3]

    $arguments = @('-y', '-f', 'gdigrab', '-framerate', '30', '-draw_mouse', '0', '-i', 'desktop',
                   '-t', '8', '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '18', $OutputVideo)
    Write-Host 'stage: start-recording'
    $recorderStart = [System.Diagnostics.ProcessStartInfo]::new()
    $recorderStart.FileName = $ffmpeg
    $recorderStart.UseShellExecute = $false
    $recorderStart.CreateNoWindow = $true
    $recorderStart.RedirectStandardError = $true
    $recorderStart.Arguments = ($arguments -join ' ')
    $recorder = [System.Diagnostics.Process]::new()
    $recorder.StartInfo = $recorderStart
    [void]$recorder.Start()
    $recorderError = $recorder.StandardError.ReadToEndAsync()
    Start-Sleep -Milliseconds 450

    Write-Host 'stage: switch-summary-items'
    for ($cycle = 0; $cycle -lt 5; ++$cycle) {
        foreach ($item in @($reference, $character, $location, $reference)) {
            [NativeTransitionSmoke]::SelectTreeItem($tree, $item)
            Start-Sleep -Milliseconds 90
        }
    }

    [NativeTransitionSmoke]::SelectTreeItem($tree, $reference)
    Start-Sleep -Milliseconds 300
    Write-Host 'stage: capture-reference'
    $process.Refresh()
    Write-Host ("window-title: " + $process.MainWindowTitle)
    $referenceScreenshot = Join-Path $ScreenshotDirectory 'reference-editor.png'
    Remove-Item -LiteralPath $referenceScreenshot -Force -ErrorAction SilentlyContinue
    Capture-ScreenWindow $main $referenceScreenshot
    if (-not (Test-Path -LiteralPath $referenceScreenshot) -or (Get-Item -LiteralPath $referenceScreenshot).Length -lt 1000) {
        throw 'Reference screenshot failed'
    }

    [NativeTransitionSmoke]::SelectTreeItem($tree, $character)
    $openCharacter = [NativeTransitionSmoke]::FindVisibleChildById($main, 2109)
    if ($openCharacter -eq [IntPtr]::Zero) { throw 'Character editor button was not found' }
    [NativeTransitionSmoke]::Click($openCharacter)
    Start-Sleep -Milliseconds 180
    Write-Host 'stage: switch-character-categories'
    $characterCategories = [NativeTransitionSmoke]::FindVisibleChildById($main, 8202)
    if ($characterCategories -eq [IntPtr]::Zero) { throw 'Character categories were not found' }
    for ($cycle = 0; $cycle -lt 3; ++$cycle) {
        foreach ($category in @(0, 1, 2, 3, 4, 5, 0)) {
            [NativeTransitionSmoke]::SelectListItem($characterCategories, 8202, $category)
            Start-Sleep -Milliseconds 85
        }
    }
    [NativeTransitionSmoke]::SelectListItem($characterCategories, 8202, 4)
    Start-Sleep -Milliseconds 300
    Write-Host 'stage: capture-character'
    $characterScreenshot = Join-Path $ScreenshotDirectory 'character-dossier.png'
    Remove-Item -LiteralPath $characterScreenshot -Force -ErrorAction SilentlyContinue
    Capture-ScreenWindow $main $characterScreenshot
    if (-not (Test-Path -LiteralPath $characterScreenshot) -or (Get-Item -LiteralPath $characterScreenshot).Length -lt 1000) {
        throw 'Character screenshot failed'
    }

    $backCharacter = [NativeTransitionSmoke]::FindVisibleChildById($main, 8201)
    [NativeTransitionSmoke]::Click($backCharacter)
    Start-Sleep -Milliseconds 180
    $items = Get-DevelopmentItems $main
    $tree = $items[0]
    [NativeTransitionSmoke]::SelectTreeItem($tree, $items[2])
    $openLocation = [NativeTransitionSmoke]::FindVisibleChildById($main, 2110)
    if ($openLocation -eq [IntPtr]::Zero) { throw 'Location editor button was not found' }
    [NativeTransitionSmoke]::Click($openLocation)
    Start-Sleep -Milliseconds 180
    Write-Host 'stage: switch-location-categories'
    $locationCategories = [NativeTransitionSmoke]::FindVisibleChildById($main, 8402)
    if ($locationCategories -eq [IntPtr]::Zero) { throw 'Location categories were not found' }
    for ($cycle = 0; $cycle -lt 3; ++$cycle) {
        foreach ($category in @(0, 1, 2, 3, 4, 5, 6, 0)) {
            [NativeTransitionSmoke]::SelectListItem($locationCategories, 8402, $category)
            Start-Sleep -Milliseconds 75
        }
    }
    [NativeTransitionSmoke]::SelectListItem($locationCategories, 8402, 3)
    Start-Sleep -Milliseconds 300
    Write-Host 'stage: capture-location'
    $locationScreenshot = Join-Path $ScreenshotDirectory 'location-dossier.png'
    Remove-Item -LiteralPath $locationScreenshot -Force -ErrorAction SilentlyContinue
    Capture-ScreenWindow $main $locationScreenshot
    if (-not (Test-Path -LiteralPath $locationScreenshot) -or (Get-Item -LiteralPath $locationScreenshot).Length -lt 1000) {
        throw 'Location screenshot failed'
    }

    Write-Host 'stage: wait-recording'
    if ($recorder -and -not $recorder.HasExited) { $recorder.WaitForExit() }
    if (-not $recorder -or $recorder.ExitCode -ne 0) {
        $diagnostic = if ($recorderError) { $recorderError.GetAwaiter().GetResult() } else { '' }
        throw "Desktop recording failed`r`n$diagnostic"
    }
    [PSCustomObject]@{
        Video = $OutputVideo
        Screenshots = $ScreenshotDirectory
        ReferenceSwitches = 20
        CharacterCategorySwitches = 21
        LocationCategorySwitches = 24
    } | Format-List
} finally {
    if ($recorder -and -not $recorder.HasExited) { $recorder.Kill() }
    if (-not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
}
