param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Project
)

$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class CharacterSmokeNative {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessageString(IntPtr hwnd, uint message, IntPtr wParam, string text);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessageBuffer(IntPtr hwnd, uint message, IntPtr wParam, StringBuilder text);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int capacity);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
    [DllImport("user32.dll")] public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

    public static IntPtr Top(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, _) => {
            uint pid; GetWindowThreadProcessId(window, out pid);
            if (pid == processId && IsWindowVisible(window) && Text(window).Contains("Mezozoy")) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr Child(IntPtr root, int id, bool visibleOnly) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(root, (window, _) => {
            if (GetDlgCtrlID(window) == id && (!visibleOnly || IsWindowVisible(window))) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr PopupMenu(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, _) => {
            uint pid; GetWindowThreadProcessId(window, out pid);
            var name = new StringBuilder(64); GetClassName(window, name, name.Capacity);
            if (pid == processId && IsWindowVisible(window) && name.ToString() == "#32768") { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static string Text(IntPtr window) {
        var value = new StringBuilder(4096);
        SendMessageBuffer(window, 0x000D, (IntPtr)value.Capacity, value);
        return value.ToString();
    }

    public static void SetText(IntPtr window, string value) {
        SendMessageString(window, 0x000C, IntPtr.Zero, value);
    }
}
'@

function Wait-For([scriptblock]$Condition, [string]$ErrorText, [int]$TimeoutMs = 8000) {
    $until = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $value = & $Condition
        if ($value -is [IntPtr]) {
            if ($value -ne [IntPtr]::Zero) { return $value }
        } elseif ($value) { return $value }
        Start-Sleep -Milliseconds 80
    } while ([DateTime]::UtcNow -lt $until)
    throw $ErrorText
}

$copy = Join-Path $PSScriptRoot 'character-smoke-project.mzoy'
Copy-Item -LiteralPath $Project -Destination $copy -Force
$process = Start-Process -FilePath $Exe -ArgumentList ('"' + $copy + '"') -PassThru

try {
    $main = Wait-For { [CharacterSmokeNative]::Top([uint32]$process.Id) } 'Main window was not found' 15000
    [void][CharacterSmokeNative]::ShowWindow($main, 3)
    [void][CharacterSmokeNative]::SetForegroundWindow($main)
    Start-Sleep -Milliseconds 500
    Write-Output ('Main title: ' + [CharacterSmokeNative]::Text($main))
    Write-Output ('Development nav: ' + [CharacterSmokeNative]::Child($main, 6103, $false))
    [void][CharacterSmokeNative]::SendMessage($main, 0x0111, [IntPtr]6103, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 350

    $tree = Wait-For { [CharacterSmokeNative]::Child($main, 2101, $true) } 'Development tree was not found'
    $before = [CharacterSmokeNative]::SendMessage($tree, 0x1105, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $add = Wait-For { [CharacterSmokeNative]::Child($main, 2102, $true) } 'Development add button was not found'
    $addRect = New-Object CharacterSmokeNative+RECT
    [void][CharacterSmokeNative]::GetWindowRect($add, [ref]$addRect)
    $developmentPage = [CharacterSmokeNative]::GetParent($add)
    [void][CharacterSmokeNative]::PostMessage($developmentPage, 0x0111, [IntPtr]2102, $add)
    Start-Sleep -Milliseconds 250
    $menu = Wait-For { [CharacterSmokeNative]::PopupMenu([uint32]$process.Id) } 'Character add menu was not shown'
    $menuRect = New-Object CharacterSmokeNative+RECT
    [void][CharacterSmokeNative]::GetWindowRect($menu, [ref]$menuRect)
    Write-Output ("Menu rect: $($menuRect.Left),$($menuRect.Top),$($menuRect.Right),$($menuRect.Bottom)")
    [void][CharacterSmokeNative]::PostMessage($developmentPage, 0x001F, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    [void][CharacterSmokeNative]::SendMessage($developmentPage, 0x0111, [IntPtr]7301, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500

    $after = [CharacterSmokeNative]::SendMessage($tree, 0x1105, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($after -ne $before + 1) { throw "Character add failed: tree count $before -> $after" }
    $open = Wait-For { [CharacterSmokeNative]::Child($main, 2109, $true) } 'Character arrow button was not shown'
    [void][CharacterSmokeNative]::SendMessage($open, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400

    $categories = Wait-For { [CharacterSmokeNative]::Child($main, 8202, $true) } 'Character categories were not shown'
    $name = Wait-For { [CharacterSmokeNative]::Child($main, 8300, $true) } 'Character name field was not shown'
    $dreamcast = Wait-For { [CharacterSmokeNative]::Child($main, 8306, $true) } 'Character dreamcast field was not shown'
    $editor = [CharacterSmokeNative]::GetParent($name)
    [CharacterSmokeNative]::SetText($name, 'SMOKE_CHARACTER')
    [CharacterSmokeNative]::SetText($dreamcast, 'SMOKE_ACTOR')
    Start-Sleep -Milliseconds 250
    Write-Output ('Editor handles: ' + $name + ' | ' + $dreamcast + ' | parent=' + $editor)
    Write-Output ('Editor values: ' + [CharacterSmokeNative]::Text($name) + ' | ' + [CharacterSmokeNative]::Text($dreamcast))
    [void][CharacterSmokeNative]::SendMessage($main, 0x0111, [IntPtr]6190, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    $mainSaveXml = Get-Content -Raw -Encoding UTF8 -LiteralPath $copy
    Write-Output ('Saved from main category: name=' + $mainSaveXml.Contains('<Name>SMOKE_CHARACTER</Name>') + ', dreamcast=' + $mainSaveXml.Contains('<Value>SMOKE_ACTOR</Value>'))

    [void][CharacterSmokeNative]::SendMessage($categories, 0x0186, [IntPtr]6, [IntPtr]::Zero)
    [void][CharacterSmokeNative]::SendMessage($editor, 0x0111, [IntPtr](8202 -bor (1 -shl 16)), $categories)
    Start-Sleep -Milliseconds 250
    $biography = Wait-For { [CharacterSmokeNative]::Child($main, 8367, $true) } 'Biography field was not shown'
    [CharacterSmokeNative]::SetText($biography, 'SMOKE_BACKSTORY_SAVED')

    $back = Wait-For { [CharacterSmokeNative]::Child($main, 8201, $true) } 'Character editor back button was not shown'
    [void][CharacterSmokeNative]::SendMessage($back, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    [void][CharacterSmokeNative]::SendMessage($main, 0x0111, [IntPtr]6190, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    [void][CharacterSmokeNative]::PostMessage($main, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(6000)) { $process.Kill() }

    $xml = Get-Content -Raw -Encoding UTF8 -LiteralPath $copy
    if (-not $xml.Contains('<Name>SMOKE_CHARACTER</Name>')) { throw 'Character name did not reach project file' }
    if (-not $xml.Contains('<Value>SMOKE_ACTOR</Value>')) { throw 'Dreamcast did not reach project file' }
    if (-not $xml.Contains('<Backstory>SMOKE_BACKSTORY_SAVED</Backstory>')) { throw 'Biography did not reach project file' }

    [PSCustomObject]@{ ExitCode = $process.ExitCode; Added = $after - $before; Character = 'SMOKE_CHARACTER'; ProjectCopy = $copy } | Format-List
} finally {
    if (-not $process.HasExited) { $process.Kill() }
}
