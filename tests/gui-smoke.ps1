param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Project,
    [switch]$OnlyFunctions,
    [switch]$WindowOnly
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class NativeSmoke {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool SetWindowText(IntPtr hwnd, string text);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")] public static extern IntPtr SendMessageText(IntPtr hwnd, uint message, IntPtr wParam, StringBuilder lParam);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr targetDc, uint flags);
    [DllImport("user32.dll")] public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

    public struct RECT { public int Left, Top, Right, Bottom; }
    public struct POINT { public int X, Y; }

    public static string Title(IntPtr hwnd) {
        var text = new StringBuilder(512);
        GetWindowText(hwnd, text, text.Capacity);
        return text.ToString();
    }

    public static string ClassName(IntPtr hwnd) {
        var text = new StringBuilder(128);
        GetClassName(hwnd, text, text.Capacity);
        return text.ToString();
    }

    public static string ControlText(IntPtr hwnd) {
        int length = SendMessage(hwnd, 0x000E, IntPtr.Zero, IntPtr.Zero).ToInt32();
        var text = new StringBuilder(Math.Max(1, length + 1));
        SendMessageText(hwnd, 0x000D, (IntPtr)text.Capacity, text);
        return text.ToString();
    }

    public static IntPtr FindTopWindow(uint processId, string titlePart) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, _) => {
            uint pid;
            GetWindowThreadProcessId(hwnd, out pid);
            if (pid == processId && (String.IsNullOrEmpty(titlePart) || Title(hwnd).Contains(titlePart))) {
                found = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindChildById(IntPtr root, int id) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(root, (hwnd, _) => {
            if (GetDlgCtrlID(hwnd) == id && IsWindowVisible(hwnd) && ClassName(hwnd).Equals("Edit", StringComparison.OrdinalIgnoreCase)) {
                found = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindAnyChildById(IntPtr root, int id) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(root, (hwnd, _) => {
            if (GetDlgCtrlID(hwnd) == id && IsWindowVisible(hwnd)) {
                found = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindChildByIdAnyVisibility(IntPtr root, int id) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(root, (hwnd, _) => {
            if (GetDlgCtrlID(hwnd) == id) {
                found = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static string DescribeChildrenById(IntPtr root, int id) {
        var result = new StringBuilder();
        EnumChildWindows(root, (hwnd, _) => {
            if (GetDlgCtrlID(hwnd) == id) {
                result.Append(hwnd.ToInt64()).Append(" | ").Append(ClassName(hwnd)).Append(" | visible=")
                      .Append(IsWindowVisible(hwnd)).Append(" | text=").Append(Title(hwnd)).AppendLine();
            }
            return true;
        }, IntPtr.Zero);
        return result.ToString();
    }

    public static IntPtr FindOtherTopWindow(uint processId, IntPtr excluded) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, _) => {
            uint pid;
            GetWindowThreadProcessId(hwnd, out pid);
            if (pid == processId && hwnd != excluded && IsWindowVisible(hwnd)) {
                found = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindVisibleChildByClass(IntPtr root, string className) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(root, (hwnd, _) => {
            if (IsWindowVisible(hwnd) && ClassName(hwnd).Equals(className, StringComparison.OrdinalIgnoreCase)) {
                found = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindVisibleChildByText(IntPtr root, string text) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(root, (hwnd, _) => {
            if (IsWindowVisible(hwnd) && Title(hwnd).Equals(text, StringComparison.Ordinal)) {
                found = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static int ParagraphStartIndent(IntPtr editor) {
        const int structureSize = 188;
        IntPtr format = Marshal.AllocHGlobal(structureSize);
        try {
            for (int offset = 0; offset < structureSize; offset += 4) Marshal.WriteInt32(format, offset, 0);
            Marshal.WriteInt32(format, 0, structureSize);
            SendMessage(editor, 0x043D, IntPtr.Zero, format); // EM_GETPARAFORMAT
            return Marshal.ReadInt32(format, 12);
        } finally {
            Marshal.FreeHGlobal(format);
        }
    }

    public static int[] EditorZoom(IntPtr editor) {
        IntPtr numerator = Marshal.AllocHGlobal(4);
        IntPtr denominator = Marshal.AllocHGlobal(4);
        try {
            Marshal.WriteInt32(numerator, 0, 0);
            Marshal.WriteInt32(denominator, 0, 0);
            SendMessage(editor, 0x04E0, numerator, denominator); // EM_GETZOOM
            return new[] { Marshal.ReadInt32(numerator), Marshal.ReadInt32(denominator) };
        } finally {
            Marshal.FreeHGlobal(numerator);
            Marshal.FreeHGlobal(denominator);
        }
    }
}
'@

function Wait-Window([uint32]$ProcessId, [string]$TitlePart, [int]$TimeoutMs = 10000) {
    $until = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $window = [NativeSmoke]::FindTopWindow($ProcessId, $TitlePart)
        if ($window -ne [IntPtr]::Zero) { return $window }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $until)
    throw "Window not found: $TitlePart"
}

function Wait-OtherWindow([uint32]$ProcessId, [IntPtr]$Excluded, [int]$TimeoutMs = 10000) {
    $until = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $window = [NativeSmoke]::FindOtherTopWindow($ProcessId, $Excluded)
        if ($window -ne [IntPtr]::Zero) { return $window }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $until)
    throw "Secondary window not found"
}

function Capture-Window([IntPtr]$Window, [string]$Path) {
    $rect = New-Object NativeSmoke+RECT
    if (-not [NativeSmoke]::GetWindowRect($Window, [ref]$rect)) { throw "GetWindowRect failed" }
    $width = [Math]::Max(1, $rect.Right - $rect.Left)
    $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $targetDc = $graphics.GetHdc()
        try {
            $printed = [NativeSmoke]::PrintWindow($Window, $targetDc, 2)
        } finally {
            $graphics.ReleaseHdc($targetDc)
        }
        if (-not $printed) {
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Capture-ScreenWindow([IntPtr]$Window, [string]$Path) {
    if ($WindowOnly) { Capture-Window $Window $Path; return }
    $rect = New-Object NativeSmoke+RECT
    if (-not [NativeSmoke]::GetWindowRect($Window, [ref]$rect)) { throw "GetWindowRect failed" }
    $width = [Math]::Max(1, $rect.Right - $rect.Left)
    $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Open-Page([IntPtr]$Main, [int]$CommandId, [string]$Screenshot) {
    [void][NativeSmoke]::SendMessage($Main, 0x0111, [IntPtr]$CommandId, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 350
    Capture-Window $Main $Screenshot
}

function Send-CtrlKey([IntPtr]$Target, [byte]$Key) {
    # Send the sequence to Mezozoy's own message queue. System-wide keybd_event
    # is flaky on Windows when the test runner loses foreground activation.
    [void][NativeSmoke]::PostMessage($Target, 0x0100, [IntPtr]0x11, [IntPtr]::Zero)
    [void][NativeSmoke]::PostMessage($Target, 0x0100, [IntPtr]$Key, [IntPtr]::Zero)
    [void][NativeSmoke]::PostMessage($Target, 0x0101, [IntPtr]$Key, [IntPtr]::Zero)
    [void][NativeSmoke]::PostMessage($Target, 0x0101, [IntPtr]0x11, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 220
}

function Select-FirstListItemOnce([IntPtr]$List) {
    $itemHeight = [NativeSmoke]::SendMessage($List, 0x01A1, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $y = [Math]::Max(1, [int]($itemHeight / 2))
    $coordinates = (($y -band 0xFFFF) -shl 16) -bor 12
    [void][NativeSmoke]::SendMessage($List, 0x0201, [IntPtr]1, [IntPtr]$coordinates)
    [void][NativeSmoke]::SendMessage($List, 0x0202, [IntPtr]::Zero, [IntPtr]$coordinates)
}

if ($OnlyFunctions) { return }
$root = Split-Path -Parent $PSScriptRoot
$copy = Join-Path $PSScriptRoot 'gui-smoke-project.mzoy'
$copyAutosave = Join-Path $PSScriptRoot 'gui-smoke-project.autosave.mzoy'
$settingsPath = if ($env:MEZOZOY_PROFILE_DIR) { Join-Path $env:MEZOZOY_PROFILE_DIR 'settings.json' } else { Join-Path $env:APPDATA 'Mezozoy\settings.json' }
$settingsBackup = if (Test-Path -LiteralPath $settingsPath) { [System.IO.File]::ReadAllBytes($settingsPath) } else { $null }
$settingsFolder = Split-Path -Parent $settingsPath
if (-not (Test-Path -LiteralPath $settingsFolder)) { [void](New-Item -ItemType Directory -Path $settingsFolder -Force) }
if (Test-Path -LiteralPath $settingsPath) {
    $testSettings = Get-Content -Encoding UTF8 -Raw -LiteralPath $settingsPath | ConvertFrom-Json
} else {
    $testSettings = [PSCustomObject]@{}
}
$testSettings | Add-Member -NotePropertyName ScriptSmartEnter -NotePropertyValue $true -Force
$testSettings | Add-Member -NotePropertyName ScriptEnableAutocomplete -NotePropertyValue $true -Force
[System.IO.File]::WriteAllText($settingsPath, ($testSettings | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
if (Test-Path -LiteralPath $copyAutosave) { Remove-Item -LiteralPath $copyAutosave -Force }
Copy-Item -LiteralPath $Project -Destination $copy -Force
$process = Start-Process -FilePath $Exe -ArgumentList ('"' + $copy + '"') -WindowStyle Hidden -PassThru

try {
    $main = Wait-Window ([uint32]$process.Id) 'Mezozoy' 15000
    [void][NativeSmoke]::ShowWindow($main, 3)
    [void][NativeSmoke]::SetForegroundWindow($main)
    Start-Sleep -Milliseconds 600

    Open-Page $main 6101 (Join-Path $root 'smoke-home.png')
    $homePage = [NativeSmoke]::FindVisibleChildByClass($main, 'Mezozoy.NativePage')
    if ($homePage -eq [IntPtr]::Zero) { throw 'Home page window was not found' }
    $hoverPoint = New-Object NativeSmoke+POINT
    $hoverPoint.X = 700
    $hoverPoint.Y = 165
    [void][NativeSmoke]::ClientToScreen($homePage, [ref]$hoverPoint)
    if (-not $WindowOnly) { [void][NativeSmoke]::SetCursorPos($hoverPoint.X, $hoverPoint.Y) }
    Start-Sleep -Milliseconds 250
    Capture-ScreenWindow $main (Join-Path $root 'smoke-home-card-hover.png')
    $posterPoint = New-Object NativeSmoke+POINT
    $posterPoint.X = 520
    $posterPoint.Y = 208
    [void][NativeSmoke]::ClientToScreen($homePage, [ref]$posterPoint)
    if (-not $WindowOnly) { [void][NativeSmoke]::SetCursorPos($posterPoint.X, $posterPoint.Y) }
    Start-Sleep -Milliseconds 250
    Capture-ScreenWindow $main (Join-Path $root 'smoke-home-poster-hover.png')
    Open-Page $main 6105 (Join-Path $root 'smoke-script.png')

    $sceneList = [NativeSmoke]::FindAnyChildById($main, 1101)
    if ($sceneList -eq [IntPtr]::Zero) { throw 'Scene list was not found' }
    $initialSceneCount = [NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    [void][NativeSmoke]::SetFocus($sceneList)
    Send-CtrlKey $main 0x43
    Send-CtrlKey $main 0x56
    $pastedSceneCount = [NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($pastedSceneCount -ne $initialSceneCount + 1) { throw 'Global Ctrl+C / Ctrl+V did not duplicate the selected scene' }
    $undoButton = [NativeSmoke]::FindAnyChildById($main, 6197)
    if ($undoButton -eq [IntPtr]::Zero) { throw 'Project Undo button was not found' }
    [void][NativeSmoke]::SendMessage($undoButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    if ([NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne $initialSceneCount) { throw 'Global Ctrl+Z did not undo scene paste' }
    Send-CtrlKey $main 0x59
    if ([NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne $initialSceneCount + 1) { throw 'Global Ctrl+Y did not redo scene paste' }
    [void][NativeSmoke]::SendMessage($undoButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)

    Write-Output ([NativeSmoke]::DescribeChildrenById($main, 1105))
    $titleEdit = [NativeSmoke]::FindChildById($main, 1105)
    if ($titleEdit -eq [IntPtr]::Zero) { throw 'Scene title edit was not found' }
    $currentButton = [NativeSmoke]::FindAnyChildById($main, 1118)
    if ($currentButton -eq [IntPtr]::Zero) { throw 'Scene focus button was not found' }
    [void][NativeSmoke]::SendMessage($currentButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $scriptEditor = [NativeSmoke]::FindAnyChildById($main, 1104)
    if ($scriptEditor -eq [IntPtr]::Zero) { throw 'Script editor was not found' }
    $focusedSceneText = [NativeSmoke]::ControlText($scriptEditor)
    if ($focusedSceneText.Contains('ИНТ. члены бомжей - ДЕНЬ')) {
        throw "Focus Scene still contains another scene: $focusedSceneText"
    }

    $chronometer = [NativeSmoke]::FindAnyChildById($main, 1131)
    if ($chronometer -eq [IntPtr]::Zero) { throw 'Screenplay chronometer was not found' }
    $chronometerText = [NativeSmoke]::ControlText($chronometer)
    if ($chronometerText -notmatch '^[^:]+: \d+:\d{2} \| \d+:\d{2}$') {
        throw "Screenplay chronometer has an unexpected format: $chronometerText"
    }

    # A scene-heading prefix is offered from the first letter, then the same
    # popup immediately switches to project locations.
    $autocompleteOriginalTitle = [NativeSmoke]::ControlText($titleEdit)
    $scriptPageForAutocomplete = [NativeSmoke]::GetParent($scriptEditor)
    if ([NativeSmoke]::SendMessage($scriptPageForAutocomplete, 0x804D, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne 1) {
        throw 'Could not focus the screenplay editor for autocomplete testing'
    }
    $autocompleteEditorText = [NativeSmoke]::ControlText($scriptEditor)
    $autocompleteTitlePosition = $autocompleteEditorText.IndexOf($autocompleteOriginalTitle)
    if ($autocompleteTitlePosition -lt 0) {
        throw 'Could not locate the selected scene title in the continuous screenplay text'
    }
    [void][NativeSmoke]::SendMessage(
        $scriptEditor,
        0x00B1,
        [IntPtr]$autocompleteTitlePosition,
        [IntPtr]($autocompleteTitlePosition + $autocompleteOriginalTitle.Length)
    )
    $headingInitial = [Text.StringBuilder]::new([string][char]0x0418)
    [void][NativeSmoke]::SendMessageText($scriptEditor, 0x00C2, [IntPtr]1, $headingInitial)
    Start-Sleep -Milliseconds 350
    $autocompleteState = [NativeSmoke]::SendMessage($scriptPageForAutocomplete, 0x804E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $autocomplete = [NativeSmoke]::FindAnyChildById($main, 1126)
    if ($autocomplete -eq [IntPtr]::Zero) {
        $afterInitialText = [NativeSmoke]::ControlText($scriptEditor)
        $firstCode = if ($afterInitialText.Length -gt 0) { [int][char]$afterInitialText[0] } else { -1 }
        $titleAfterInitial = [NativeSmoke]::ControlText($titleEdit)
        $titleFirstCode = if ($titleAfterInitial.Length -gt 0) { [int][char]$titleAfterInitial[0] } else { -1 }
        throw "Scene heading prefix autocomplete did not open after the first letter (packed state=$autocompleteState, text length=$($afterInitialText.Length), first code=$firstCode, title length=$($titleAfterInitial.Length), title first code=$titleFirstCode)"
    }
    $prefixCount = [NativeSmoke]::SendMessage($autocomplete, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $prefixValues = @()
    for ($index = 0; $index -lt $prefixCount; $index++) {
        $item = [Text.StringBuilder]::new(256)
        [void][NativeSmoke]::SendMessageText($autocomplete, 0x0189, [IntPtr]$index, $item)
        $prefixValues += $item.ToString()
    }
    $intPrefix = -join @([char]0x0418, [char]0x041D, [char]0x0422, '.')
    $intNatPrefix = -join @([char]0x0418, [char]0x041D, [char]0x0422, '.', '/',
                            [char]0x041D, [char]0x0410, [char]0x0422, '.')
    if (-not ($prefixValues | Where-Object { $_.StartsWith($intPrefix) }) -or
        -not ($prefixValues | Where-Object { $_.StartsWith($intNatPrefix) })) {
        throw "Scene heading prefixes are incomplete: $($prefixValues -join ', ')"
    }
    Capture-Window $main (Join-Path $root 'smoke-autocomplete-heading.png')

    Select-FirstListItemOnce $autocomplete
    Start-Sleep -Milliseconds 350
    $locationAutocomplete = [NativeSmoke]::FindAnyChildById($main, 1126)
    if ($locationAutocomplete -eq [IntPtr]::Zero) {
        $forcedLocationState = [NativeSmoke]::SendMessage($scriptPageForAutocomplete, 0x804E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
        Capture-Window $main (Join-Path $root 'smoke-autocomplete-failure.png')
        $debugLine = [NativeSmoke]::SendMessage($scriptPageForAutocomplete, 0x804F, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
        $debugSelection = [NativeSmoke]::SendMessage($scriptEditor, 0x00B0, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
        throw "Location autocomplete did not follow prefix (state=$forcedLocationState, line=$debugLine, selection=$debugSelection, text=$([NativeSmoke]::ControlText($scriptEditor)))"
    }
    $locationCount = [NativeSmoke]::SendMessage($locationAutocomplete, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $locationValues = @()
    for ($index = 0; $index -lt $locationCount; $index++) {
        $item = [Text.StringBuilder]::new(256)
        [void][NativeSmoke]::SendMessageText($locationAutocomplete, 0x0189, [IntPtr]$index, $item)
        $locationValues += $item.ToString()
    }
    $expectedLocation = -join @([char]0x0411, [char]0x0423, [char]0x041D, [char]0x041A,
                                [char]0x0415, [char]0x0420)
    if (-not ($locationValues -contains $expectedLocation)) {
        throw "Project locations were not offered after the scene prefix: $($locationValues -join ', ')"
    }
    if ($locationCount -ne 1) {
        throw "Single location suggestion contains filler rows: count=$locationCount"
    }
    $locationRect = New-Object NativeSmoke+RECT
    if (-not [NativeSmoke]::GetWindowRect($locationAutocomplete, [ref]$locationRect)) {
        throw 'Could not measure the location autocomplete popup'
    }
    $locationItemHeight = [NativeSmoke]::SendMessage($locationAutocomplete, 0x01A1, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $locationPopupHeight = $locationRect.Bottom - $locationRect.Top
    if ($locationPopupHeight -gt ($locationItemHeight + 4)) {
        throw "Single suggestion popup reserved a blank row: popup=$locationPopupHeight item=$locationItemHeight"
    }
    [void][NativeSmoke]::SetForegroundWindow($main)
    Start-Sleep -Milliseconds 180
    Capture-ScreenWindow $main (Join-Path $root 'smoke-autocomplete-location.png')

    Select-FirstListItemOnce $locationAutocomplete
    Start-Sleep -Milliseconds 300
    if ([NativeSmoke]::FindAnyChildById($main, 1126) -ne [IntPtr]::Zero) {
        throw 'Location autocomplete did not close after one mouse click'
    }
    $acceptedHeading = [NativeSmoke]::ControlText($scriptEditor)
    if (-not $acceptedHeading.Contains("$intPrefix $expectedLocation")) {
        throw 'One mouse click did not insert the selected scene location'
    }

    # Restore the fixture title so the existing synchronization test remains
    # independent from the autocomplete assertion.
    $restoreEditorText = [NativeSmoke]::ControlText($scriptEditor)
    $restoreHeadingEnd = $restoreEditorText.IndexOf("`r", $autocompleteTitlePosition)
    if ($restoreHeadingEnd -lt 0) {
        $restoreHeadingEnd = $restoreEditorText.IndexOf("`n", $autocompleteTitlePosition)
    }
    if ($restoreHeadingEnd -lt 0) {
        $restoreHeadingEnd = $restoreEditorText.Length
    }
    [void][NativeSmoke]::SendMessage(
        $scriptEditor,
        0x00B1,
        [IntPtr]$autocompleteTitlePosition,
        [IntPtr]$restoreHeadingEnd
    )
    $restoreHeading = [Text.StringBuilder]::new($autocompleteOriginalTitle)
    [void][NativeSmoke]::SendMessageText($scriptEditor, 0x00C2, [IntPtr]1, $restoreHeading)
    Start-Sleep -Milliseconds 300

    # Heading suggestions belong only to Scene Heading. Typing the same first
    # letter in an Action paragraph must not offer INT/EXT prefixes.
    $characterTestStart = [NativeSmoke]::SendMessage($scriptEditor, 0x000E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x00B1, [IntPtr]$characterTestStart, [IntPtr]$characterTestStart)
    $blankParagraph = [Text.StringBuilder]::new("`r`n")
    [void][NativeSmoke]::SendMessageText($scriptEditor, 0x00C2, [IntPtr]1, $blankParagraph)
    $characterLineStart = [NativeSmoke]::SendMessage($scriptEditor, 0x00BB, [IntPtr](-1), [IntPtr]::Zero).ToInt32()
    $autocompleteActionButton = [NativeSmoke]::FindAnyChildById($main, 1109)
    if ($autocompleteActionButton -eq [IntPtr]::Zero) { throw 'Action format button was not found' }
    [void][NativeSmoke]::SendMessage($autocompleteActionButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    $actionInitial = [Text.StringBuilder]::new([string][char]0x0418)
    [void][NativeSmoke]::SendMessageText($scriptEditor, 0x00C2, [IntPtr]1, $actionInitial)
    Start-Sleep -Milliseconds 350
    $actionAutocompleteState = [NativeSmoke]::SendMessage($scriptPageForAutocomplete, 0x804E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $actionSuggestionCount = $actionAutocompleteState -band 0xFFFF
    $actionSuggestionVisible = ($actionAutocompleteState -shr 16) -band 0xFFFF
    if ($actionSuggestionCount -ne 0 -or $actionSuggestionVisible -ne 0) {
        throw "Scene heading autocomplete leaked into an Action paragraph (state=$actionAutocompleteState)"
    }

    # Character names use the same first-letter behaviour in the focused and
    # continuous screenplay modes. The paragraph is typed only after its
    # semantic format is selected, matching the real writing workflow.
    $actionLineStart = [NativeSmoke]::SendMessage($scriptEditor, 0x00BB, [IntPtr](-1), [IntPtr]::Zero).ToInt32()
    $actionLineLength = [NativeSmoke]::SendMessage($scriptEditor, 0x00C1, [IntPtr]$actionLineStart, [IntPtr]::Zero).ToInt32()
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x00B1, [IntPtr]$actionLineStart, [IntPtr]($actionLineStart + $actionLineLength))
    $emptyAction = [Text.StringBuilder]::new('')
    [void][NativeSmoke]::SendMessageText($scriptEditor, 0x00C2, [IntPtr]1, $emptyAction)
    $autocompleteCharacterButton = [NativeSmoke]::FindAnyChildById($main, 1110)
    [void][NativeSmoke]::SendMessage($autocompleteCharacterButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    $characterInitial = [Text.StringBuilder]::new([string][char]0x041C)
    [void][NativeSmoke]::SendMessageText($scriptEditor, 0x00C2, [IntPtr]1, $characterInitial)
    Start-Sleep -Milliseconds 350
    $characterAutocompleteState = [NativeSmoke]::SendMessage($scriptPageForAutocomplete, 0x804E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $characterLineState = [NativeSmoke]::SendMessage($scriptPageForAutocomplete, 0x804F, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $characterAutocomplete = [NativeSmoke]::FindAnyChildById($main, 1126)
    if ($characterAutocomplete -eq [IntPtr]::Zero) {
        $characterEditorText = [NativeSmoke]::ControlText($scriptEditor)
        $characterTailStart = [Math]::Max(0, $characterEditorText.Length - 80)
        $characterTail = $characterEditorText.Substring($characterTailStart).Replace("`r", '<CR>').Replace("`n", '<LF>')
        throw "Character autocomplete did not open after the first letter (popup=$characterAutocompleteState, line=$characterLineState, tail=$characterTail)"
    }
    $characterSuggestion = [Text.StringBuilder]::new(256)
    [void][NativeSmoke]::SendMessageText($characterAutocomplete, 0x0189, [IntPtr]0, $characterSuggestion)
    $expectedCharacter = -join @([char]0x041C, [char]0x0418, [char]0x0428, [char]0x0410)
    if ($characterSuggestion.ToString() -ne $expectedCharacter) {
        throw "Unexpected first character suggestion: $($characterSuggestion.ToString())"
    }
    $characterCount = [NativeSmoke]::SendMessage($characterAutocomplete, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($characterCount -ne 1) {
        throw "Single character suggestion contains filler rows: count=$characterCount"
    }
    Capture-ScreenWindow $main (Join-Path $root 'smoke-autocomplete-character.png')
    Select-FirstListItemOnce $characterAutocomplete
    Start-Sleep -Milliseconds 300
    if ([NativeSmoke]::FindAnyChildById($main, 1126) -ne [IntPtr]::Zero) {
        throw 'Character autocomplete did not close after one mouse click'
    }
    $characterAcceptedText = [NativeSmoke]::ControlText($scriptEditor)
    if (-not $characterAcceptedText.Contains($expectedCharacter)) {
        throw 'One mouse click did not insert the selected character name'
    }
    $characterTestEnd = [NativeSmoke]::SendMessage($scriptEditor, 0x000E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x00B1, [IntPtr]$characterTestStart, [IntPtr]$characterTestEnd)
    $emptyReplacement = [Text.StringBuilder]::new('')
    [void][NativeSmoke]::SendMessageText($scriptEditor, 0x00C2, [IntPtr]1, $emptyReplacement)
    Start-Sleep -Milliseconds 300

    # Rename the scene exactly where a writer does it: in the screenplay heading.
    # This exercises RichEdit -> scene model -> properties/list synchronization instead
    # of relying on a synthetic cross-process EN_CHANGE notification.
    $newTitle = 'CXX-SMOKE-SCENE-0.9'
    $oldTitle = [NativeSmoke]::ControlText($titleEdit)
    $oldTitlePosition = $focusedSceneText.IndexOf($oldTitle)
    if ([string]::IsNullOrWhiteSpace($oldTitle) -or $oldTitlePosition -lt 0) {
        throw "Current scene heading was not found in the editor: $oldTitle"
    }
    [void][NativeSmoke]::SendMessage(
        $scriptEditor,
        0x00B1,
        [IntPtr]$oldTitlePosition,
        [IntPtr]($oldTitlePosition + $oldTitle.Length))
    $newTitleBuffer = [Text.StringBuilder]::new($newTitle)
    [void][NativeSmoke]::SendMessageText($scriptEditor, 0x00C2, [IntPtr]1, $newTitleBuffer)
    Start-Sleep -Milliseconds 700

    $renamedSceneText = [NativeSmoke]::ControlText($scriptEditor)
    if (-not $renamedSceneText.Contains($newTitle)) {
        throw "Scene heading rename was not retained in the editor: $renamedSceneText"
    }
    $titleAfterEditorRename = [NativeSmoke]::ControlText($titleEdit)
    if ($titleAfterEditorRename -ne $newTitle) {
        throw "Scene heading rename did not reach properties: $titleAfterEditorRename"
    }
    $renamedSceneCaption = [Text.StringBuilder]::new(512)
    [void][NativeSmoke]::SendMessageText($sceneList, 0x0189, [IntPtr]0, $renamedSceneCaption)
    if (-not $renamedSceneCaption.ToString().Contains($newTitle)) {
        throw "Scene heading rename did not reach navigator: $($renamedSceneCaption.ToString())"
    }
    Capture-Window $main (Join-Path $root 'smoke-script-after-title-change.png')

    $currentSceneMarker = "`r`nCURRENT-SCENE-TYPING-OK"
    $currentText = [NativeSmoke]::ControlText($scriptEditor)
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x00B1, [IntPtr]$currentText.Length, [IntPtr]$currentText.Length)
    foreach ($character in $currentSceneMarker.ToCharArray()) {
        [void][NativeSmoke]::SendMessage($scriptEditor, 0x0102, [IntPtr][int]$character, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 650
    if (-not ([NativeSmoke]::ControlText($scriptEditor)).Contains('CURRENT-SCENE-TYPING-OK')) {
        throw 'Typing in Current Scene editor failed'
    }

    # Smart Enter follows the screenplay chain: Character -> Dialogue -> Action.
    $smartCharacter = "`r`nSMARTTESTPERSON"
    $currentText = [NativeSmoke]::ControlText($scriptEditor)
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x00B1, [IntPtr]$currentText.Length, [IntPtr]$currentText.Length)
    foreach ($character in $smartCharacter.ToCharArray()) {
        [void][NativeSmoke]::SendMessage($scriptEditor, 0x0102, [IntPtr][int]$character, [IntPtr]::Zero)
    }
    $characterButton = [NativeSmoke]::FindAnyChildById($main, 1110)
    if ($characterButton -eq [IntPtr]::Zero) { throw 'Character format button was not found' }
    $formatLabel = [NativeSmoke]::FindAnyChildById($main, 1129)
    if ($formatLabel -eq [IntPtr]::Zero) { throw 'Current screenplay format indicator was not found' }
    [void][NativeSmoke]::SendMessage($characterButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    $characterFormatText = [NativeSmoke]::ControlText($formatLabel)
    if ([string]::IsNullOrWhiteSpace($characterFormatText)) { throw 'Character format was not applied before Smart Enter' }
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero)
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x0102, [IntPtr]0x0D, [IntPtr]::Zero)
    $dialogueFormatText = [NativeSmoke]::ControlText($formatLabel)
    if ($dialogueFormatText -eq $characterFormatText) {
        throw 'Smart Enter did not switch Character to Dialogue'
    }
    foreach ($character in 'SMART DIALOGUE'.ToCharArray()) {
        [void][NativeSmoke]::SendMessage($scriptEditor, 0x0102, [IntPtr][int]$character, [IntPtr]::Zero)
    }
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero)
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x0102, [IntPtr]0x0D, [IntPtr]::Zero)
    $actionFormatText = [NativeSmoke]::ControlText($formatLabel)
    if ($actionFormatText -eq $dialogueFormatText) {
        throw 'Smart Enter did not switch Dialogue to Action'
    }
    $visibleAction = 'VISIBLE ACTION COLOR'
    foreach ($character in $visibleAction.ToCharArray()) {
        [void][NativeSmoke]::SendMessage($scriptEditor, 0x0102, [IntPtr][int]$character, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 250
    $visibleActionText = [NativeSmoke]::ControlText($scriptEditor)
    $visibleActionPosition = $visibleActionText.LastIndexOf($visibleAction)
    if ($visibleActionPosition -lt 0) { throw 'Action colour test text was not inserted' }
    [void][NativeSmoke]::SendMessage(
        $scriptEditor,
        0x00B1,
        [IntPtr]$visibleActionPosition,
        [IntPtr]($visibleActionPosition + $visibleAction.Length))
    $scriptPageForColor = [NativeSmoke]::GetParent($scriptEditor)
    $visibleActionColor = [NativeSmoke]::SendMessage($scriptPageForColor, 0x804A, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if (($visibleActionColor -band 255) -lt 220 -or (($visibleActionColor -shr 8) -band 255) -lt 220 -or (($visibleActionColor -shr 16) -band 255) -lt 220) {
        throw "Typed action inherited a dark RichEdit colour: 0x$($visibleActionColor.ToString('X8'))"
    }
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x00B1, [IntPtr]$visibleActionText.Length, [IntPtr]$visibleActionText.Length)

    # Both explicit creation paths must create a real scene object.
    $sceneCountBeforeCreation = [NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $headingButton = [NativeSmoke]::FindAnyChildById($main, 1107)
    if ($headingButton -eq [IntPtr]::Zero) { throw 'Scene Heading button was not found' }
    [void][NativeSmoke]::SendMessage($headingButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 350
    if ([NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne $sceneCountBeforeCreation + 1) {
        throw 'Scene Heading button did not create a scene'
    }
    [void][NativeSmoke]::SendMessage($main, 0x0111, [IntPtr]6197, [IntPtr]::Zero)
    $afterHeadingUndoCount = [NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($afterHeadingUndoCount -ne $sceneCountBeforeCreation) {
        throw "Undo after Scene Heading creation failed (before=$sceneCountBeforeCreation, after=$afterHeadingUndoCount)"
    }
    $addSceneButton = [NativeSmoke]::FindAnyChildById($main, 1102)
    if ($addSceneButton -eq [IntPtr]::Zero) { throw 'Add Scene button was not found' }
    [void][NativeSmoke]::SendMessage($addSceneButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 350
    if ([NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne $sceneCountBeforeCreation + 1) {
        throw '+ Scene button did not create a scene'
    }
    [void][NativeSmoke]::SendMessage($main, 0x0111, [IntPtr]6197, [IntPtr]::Zero)
    if ([NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne $sceneCountBeforeCreation) {
        throw 'Undo after + Scene creation failed'
    }

    # Full Script is one editable continuous viewport with finite physical
    # pages. Its viewport can be taller than one page because it represents a
    # scrollable stack; paper geometry comes from the shared layout engine.
    $fullButton = [NativeSmoke]::FindAnyChildById($main, 1119)
    if ($fullButton -eq [IntPtr]::Zero) { throw 'Full Script button was not found' }
    # Windows can minimize a foreground test window after synthetic keyboard
    # input. Restore it before measuring the physical screenplay viewport.
    [void][NativeSmoke]::ShowWindow($main, 3)
    [void][NativeSmoke]::SetForegroundWindow($main)
    [void][NativeSmoke]::SendMessage($fullButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    $sceneCountAfterFullSwitch = [NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($sceneCountAfterFullSwitch -ne $sceneCountBeforeCreation) {
        throw "Switching to Full Script duplicated scenes (before=$sceneCountBeforeCreation, after=$sceneCountAfterFullSwitch)"
    }
    $scriptPage = [NativeSmoke]::GetParent($scriptEditor)
    $letterButton = [NativeSmoke]::FindAnyChildById($main, 1117)
    $a4Button = [NativeSmoke]::FindAnyChildById($main, 1130)
    if ($letterButton -eq [IntPtr]::Zero -or $a4Button -eq [IntPtr]::Zero) {
        throw 'Hollywood / US Letter and A4 presets were not found'
    }
    [void][NativeSmoke]::SendMessage($a4Button, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    if ([NativeSmoke]::SendMessage($scriptPage, 0x804B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne 1) {
        throw 'A4 preset did not reach the shared screenplay layout'
    }
    [void][NativeSmoke]::SendMessage($letterButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    if ([NativeSmoke]::SendMessage($scriptPage, 0x804B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne 0) {
        throw 'Hollywood / US Letter preset did not reach the shared screenplay layout'
    }
    $layoutPacked = [NativeSmoke]::SendMessage($scriptPage, 0x804C, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if (($layoutPacked -band 0xFFFF) -ne 54 -or (($layoutPacked -shr 16) -band 0xFFFF) -ne 12) {
        throw "Hollywood physical layout is not Courier 12 / 54 lines: $layoutPacked"
    }
    $zoomPacked = [NativeSmoke]::SendMessage($scriptPage, 0x8047, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $editorZoom = @([int]($zoomPacked -band 0xFFFF), [int](($zoomPacked -shr 16) -band 0xFFFF))
    if ($editorZoom[0] -lt 35 -or $editorZoom[0] -gt 200 -or $editorZoom[1] -ne 100) {
        throw "Script editor display zoom is invalid: zoom=$($editorZoom[0])/$($editorZoom[1])"
    }
    foreach ($removedFormatId in @(1108, 1114, 1115, 1116)) {
        if ([NativeSmoke]::FindAnyChildById($main, $removedFormatId) -ne [IntPtr]::Zero) {
            throw "Removed screenplay format is still visible: id=$removedFormatId"
        }
    }
    $fullText = [NativeSmoke]::ControlText($scriptEditor)
    $firstPosition = $fullText.IndexOf($newTitle)
    $secondSceneCaption = [Text.StringBuilder]::new(512)
    [void][NativeSmoke]::SendMessageText($sceneList, 0x0189, [IntPtr]1, $secondSceneCaption)
    $secondSceneTitle = $secondSceneCaption.ToString().Trim([char]0) -replace '^\d+\.\s*', ''
    $runtimeSeparator = $secondSceneTitle.LastIndexOf('    ')
    if ($runtimeSeparator -gt 0) { $secondSceneTitle = $secondSceneTitle.Substring(0, $runtimeSeparator) }
    $secondPosition = if ([string]::IsNullOrWhiteSpace($secondSceneTitle)) { -1 } else { $fullText.IndexOf($secondSceneTitle, [Math]::Max(0, $firstPosition + $newTitle.Length)) }
    if ($firstPosition -lt 0 -or $secondPosition -le $firstPosition) {
        $diagnostic = Join-Path $root 'smoke-full-script-diagnostic.txt'
        [System.IO.File]::WriteAllText($diagnostic, "first=$firstPosition`r`nsecond=$secondPosition`r`nsecondTitle=$secondSceneTitle`r`n---TEXT---`r`n$fullText", [Text.Encoding]::UTF8)
        throw "Full Script is not a continuous ordered document. Diagnostic: $diagnostic"
    }
    $syncedTitle = 'FULL-SCRIPT-SYNCED-TITLE'
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x00B1, [IntPtr]$firstPosition, [IntPtr]($firstPosition + $newTitle.Length))
    $replacement = [Text.StringBuilder]::new($syncedTitle)
    [void][NativeSmoke]::SendMessageText($scriptEditor, 0x00C2, [IntPtr]1, $replacement)
    Start-Sleep -Milliseconds 650
    if (-not ([NativeSmoke]::ControlText($titleEdit)).Contains($syncedTitle)) {
        throw 'Title edited in Full Script did not synchronize with scene properties'
    }
    $firstSceneCaption = [Text.StringBuilder]::new(512)
    [void][NativeSmoke]::SendMessageText($sceneList, 0x0189, [IntPtr]0, $firstSceneCaption)
    if (-not $firstSceneCaption.ToString().Contains($syncedTitle)) {
        throw 'Title edited in Full Script did not synchronize with scene navigator'
    }
    $sceneCountAfterFirstTitle = [NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($sceneCountAfterFirstTitle -ne $sceneCountBeforeCreation) {
        throw "First Full Script title edit duplicated scenes (before=$sceneCountBeforeCreation, after=$sceneCountAfterFirstTitle)"
    }

    # Move straight into the second scene inside the A4 viewport. The user
    # must not have to select that scene in the navigator before editing it.
    $secondSyncedTitle = 'FULL-SCRIPT-SECOND-SYNCED'
    if ([NativeSmoke]::SendMessage($scriptPage, 0x8049, [IntPtr]1, [IntPtr]::Zero).ToInt32() -ne 1) {
        throw 'Editor could not select the second scene heading by its protected boundary'
    }
    $secondReplacement = [Text.StringBuilder]::new($secondSyncedTitle)
    [void][NativeSmoke]::SendMessageText($scriptEditor, 0x00C2, [IntPtr]1, $secondReplacement)
    Start-Sleep -Milliseconds 700

    $selectedAfterViewportEdit = [NativeSmoke]::SendMessage($sceneList, 0x0188, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($selectedAfterViewportEdit -ne 1) {
        throw "Caret scene did not become active in navigator: $selectedAfterViewportEdit"
    }
    if ([NativeSmoke]::ControlText($titleEdit) -ne $secondSyncedTitle) {
        throw "Second viewport heading did not reach scene properties: $([NativeSmoke]::ControlText($titleEdit))"
    }
    $secondSceneCaptionAfter = [Text.StringBuilder]::new(512)
    [void][NativeSmoke]::SendMessageText($sceneList, 0x0189, [IntPtr]1, $secondSceneCaptionAfter)
    if (-not $secondSceneCaptionAfter.ToString().Contains($secondSyncedTitle)) {
        throw 'Second viewport heading did not reach scene navigator'
    }
    [void][NativeSmoke]::SendMessage($scriptPage, 0x8049, [IntPtr]1, [IntPtr]::Zero)
    $visibleTitleEffects = [NativeSmoke]::SendMessage($scriptPage, 0x8048, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    if ($visibleTitleEffects -ne 0) {
        throw "Visible scene heading inherited hidden/protected formatting: $visibleTitleEffects"
    }
    $sceneCountAfterSecondTitle = [NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($sceneCountAfterSecondTitle -ne $sceneCountBeforeCreation) {
        throw "Second Full Script title edit duplicated scenes (before=$sceneCountBeforeCreation, after=$sceneCountAfterSecondTitle)"
    }
    Capture-Window $main (Join-Path $root 'smoke-script-second-title-sync.png')

    $fullScriptMarker = "`r`nFULL-SCRIPT-TYPING-OK"
    $updatedFullTextLength = ([NativeSmoke]::ControlText($scriptEditor)).Length
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x00B1, [IntPtr]$updatedFullTextLength, [IntPtr]$updatedFullTextLength)
    foreach ($character in $fullScriptMarker.ToCharArray()) {
        [void][NativeSmoke]::SendMessage($scriptEditor, 0x0102, [IntPtr][int]$character, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 650
    if (-not ([NativeSmoke]::ControlText($scriptEditor)).Contains('FULL-SCRIPT-TYPING-OK')) {
        throw 'Typing in Full Script editor failed'
    }
    $sceneCountAfterFullTyping = [NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($sceneCountAfterFullTyping -ne $sceneCountBeforeCreation) {
        throw "Full Script typing duplicated scenes (before=$sceneCountBeforeCreation, after=$sceneCountAfterFullTyping)"
    }
    # Formatting commands must also work in the continuous viewport and be
    # serialized, not merely alter the current RichEdit paint state.
    $formattedText = [NativeSmoke]::ControlText($scriptEditor)
    $formatMarkerPosition = $formattedText.LastIndexOf('FULL-SCRIPT-TYPING-OK')
    if ($formatMarkerPosition -lt 0) { throw 'Full Script format marker was not found' }
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x00B1, [IntPtr]$formatMarkerPosition, [IntPtr]($formatMarkerPosition + 1))
    $parentheticalButton = [NativeSmoke]::FindAnyChildById($main, 1111)
    if ($parentheticalButton -eq [IntPtr]::Zero) { throw 'Parenthetical format button was not found' }
    [void][NativeSmoke]::SendMessage($parentheticalButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    $sceneCountAfterFullFormat = [NativeSmoke]::SendMessage($sceneList, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($sceneCountAfterFullFormat -ne $sceneCountBeforeCreation) {
        throw "Full Script formatting duplicated scenes (before=$sceneCountBeforeCreation, after=$sceneCountAfterFullFormat)"
    }
    # Leave the editor in a neutral visual state for the release screenshot.
    # The formatting assertion above already exercised the selected paragraph.
    $releaseTextLength = ([NativeSmoke]::ControlText($scriptEditor)).Length
    [void][NativeSmoke]::SendMessage($scriptEditor, 0x00B1, [IntPtr]$releaseTextLength, [IntPtr]$releaseTextLength)
    [void][NativeSmoke]::SetFocus($sceneList)
    Start-Sleep -Milliseconds 150
    Capture-Window $main (Join-Path $root 'smoke-script-full.png')
    [void][NativeSmoke]::SendMessage($currentButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 350

    [void][NativeSmoke]::SendMessage($main, 0x0111, [IntPtr]6190, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 900

    Open-Page $main 6103 (Join-Path $root 'smoke-development.png')
    $developmentTree = [NativeSmoke]::FindAnyChildById($main, 2101)
    if ($developmentTree -eq [IntPtr]::Zero) { throw 'Development tree was not found' }
    $projectTitle = [NativeSmoke]::SendMessage($developmentTree, 0x110A, [IntPtr]::Zero, [IntPtr]::Zero)
    if ($projectTitle -eq [IntPtr]::Zero) { throw 'Project title root was not found in Development' }
    [void][NativeSmoke]::SendMessage($developmentTree, 0x110B, [IntPtr]9, $projectTitle)
    Start-Sleep -Milliseconds 250
    if ([NativeSmoke]::FindAnyChildById($main, 8601) -eq [IntPtr]::Zero) { throw 'Poster chooser was not found on the title page' }
    Capture-Window $main (Join-Path $root 'smoke-development-poster.png')
    Open-Page $main 6104 (Join-Path $root 'smoke-cards.png')
    Open-Page $main 6110 (Join-Path $root 'smoke-settings.png')
    $settingsCategories = [NativeSmoke]::FindAnyChildById($main, 5101)
    if ($settingsCategories -eq [IntPtr]::Zero) { throw 'Settings categories were not found' }
    [void][NativeSmoke]::SendMessage($settingsCategories, 0x0186, [IntPtr]4, [IntPtr]::Zero)
    $settingsPage = [NativeSmoke]::GetParent($settingsCategories)
    [void][NativeSmoke]::SendMessage($settingsPage, 0x0111, [IntPtr](5101 -bor (1 -shl 16)), $settingsCategories)
    Start-Sleep -Milliseconds 250
    $historySteps = [NativeSmoke]::FindAnyChildById($main, 5128)
    if ($historySteps -eq [IntPtr]::Zero) { throw 'Project history steps setting was not found' }
    Capture-Window $main (Join-Path $root 'smoke-settings-history.png')
    Open-Page $main 6105 (Join-Path $root 'smoke-script-after-navigation.png')

    [void][NativeSmoke]::SetForegroundWindow($main)
    # Deliver F12 to a child editor. MainWindow's global message loop must
    # intercept it even when keyboard focus belongs to a text field.
    [void][NativeSmoke]::PostMessage($titleEdit, 0x0100, [IntPtr]0x7B, [IntPtr]::Zero)
    $preview = Wait-Window ([uint32]$process.Id) 'F12' 10000
    Start-Sleep -Milliseconds 500
    Capture-Window $preview (Join-Path $root 'smoke-preview.png')
    [void][NativeSmoke]::PostMessage($preview, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400

    # F12 commits pending editor changes before creating the preview. Persist
    # that final state so WM_CLOSE can be tested without an interactive prompt.
    [void][NativeSmoke]::SendMessage($main, 0x0111, [IntPtr]6190, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    [void][NativeSmoke]::PostMessage($main, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(6000)) { $process.Kill() }

    $xml = Get-Content -Encoding UTF8 -Raw -LiteralPath $copy
    if (-not $xml.Contains($syncedTitle)) { throw 'Ctrl+S/title synchronization did not reach the project file' }
    if (-not $xml.Contains($secondSyncedTitle)) { throw 'Second viewport title synchronization did not reach the project file' }
    if (-not $xml.Contains('CURRENT-SCENE-TYPING-OK')) { throw 'Current Scene typing did not reach the project file' }
    if (-not $xml.Contains('FULL-SCRIPT-TYPING-OK')) { throw 'Full Script typing did not reach the project file' }
    if ($xml -notmatch '<ScreenplayFormats>[^<]*P[^<]*</ScreenplayFormats>') {
        throw 'Full Script paragraph formatting did not reach the project file'
    }
    [PSCustomObject]@{
        ExitCode = $process.ExitCode
        SavedTitle = $syncedTitle
        ProjectCopy = $copy
        Screenshots = 18
    } | Format-List
} finally {
    if (-not $process.HasExited) { $process.Kill() }
    if ($null -ne $settingsBackup) {
        [System.IO.File]::WriteAllBytes($settingsPath, $settingsBackup)
    } elseif (Test-Path -LiteralPath $settingsPath) {
        Remove-Item -LiteralPath $settingsPath -Force
    }
}
