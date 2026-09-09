param([Parameter(Mandatory=$true)][string]$Exe)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sample = Join-Path $root 'examples\Horizon.mzoy'
. (Join-Path $PSScriptRoot 'gui-smoke.ps1') -Exe $Exe -Project $sample -OnlyFunctions -WindowOnly
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ReleaseWindow {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int width, int height, uint flags);
}
'@
$oldProfile = $env:MEZOZOY_PROFILE_DIR
$env:MEZOZOY_PROFILE_DIR = Join-Path $root 'test-results\release-profile'
[void](New-Item -ItemType Directory -Force -Path $env:MEZOZOY_PROFILE_DIR)
$copy = Join-Path $root 'test-results\Horizon-capture.mzoy'
Copy-Item -LiteralPath $sample -Destination $copy -Force
$process = $null
function Command([int]$Id) {
    $button = [NativeSmoke]::FindAnyChildById($script:main, $Id)
    if ($button -eq [IntPtr]::Zero) { throw "Missing control $Id" }
    [void][NativeSmoke]::SendMessage($button, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
}
function Query([int]$Id, [int]$Value=0) {
    [NativeSmoke]::SendMessage($script:page, [uint32](0x8000+$Id), [IntPtr]$Value, [IntPtr]::Zero).ToInt64()
}
try {
    $process = Start-Process -FilePath $Exe -ArgumentList ('"'+$copy+'"') -WindowStyle Hidden -PassThru
    $script:main = Wait-Window ([uint32]$process.Id) 'Mezozoy' 15000
    [void][NativeSmoke]::ShowWindow($main, 3)
    Open-Page $main 6105 (Join-Path $root 'test-results\01-editor.png')
    $editor = [NativeSmoke]::FindAnyChildById($main, 1104)
    $script:page = [NativeSmoke]::GetParent($editor)
    [void](Query 77)
    $pages = Query 84
    if ($pages -lt 2) { throw 'Demo must span physical pages' }

    # Map text to the visible page, click it, then verify the insertion point.
    $text = [NativeSmoke]::ControlText($editor).Replace("`r`n", "`r")
    $position = $text.IndexOf('настольная')
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr]$position, [IntPtr]$position)
    [void][NativeSmoke]::SendMessage($editor, 0x00B7, [IntPtr]::Zero, [IntPtr]::Zero)
    $point = Query 86 $position
    $coords = ((($point -shr 16)+4) -shl 16) -bor ($point -band 65535)
    [void][NativeSmoke]::SendMessage($editor, 0x0201, [IntPtr]1, [IntPtr]$coords)
    [void][NativeSmoke]::SendMessage($editor, 0x0202, [IntPtr]::Zero, [IntPtr]$coords)
    if ((Query 87) -ne $position) { throw 'Page mouse position and text position differ' }
    Write-Output 'PASS: A mouse click on a wrapped visual line resolves to its exact text position.'
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][NativeSmoke]::SendMessage($editor, 0x00B7, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][NativeSmoke]::SendMessage($editor, 0x0100, [IntPtr]27, [IntPtr]::Zero)
    Capture-Window $main (Join-Path $root 'test-results\01-editor.png')

    $position = $text.IndexOf("`rКАЙ`r")+1
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr]$position, [IntPtr]($position+3))
    [void][NativeSmoke]::SendMessageText($editor, 0x00C2, [IntPtr]1, [Text.StringBuilder]::new('К'))
    [void](Query 78)
    if (((Query 78) -shr 16) -ne 1) { throw 'Character suggestions are not visible' }
    Capture-Window $main (Join-Path $root 'test-results\02-character-suggestion.png')
    [void](Query 85)
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][NativeSmoke]::SendMessage($editor, 0x00B7, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][NativeSmoke]::SendMessage($editor, 0x020A, [IntPtr](1440 -shl 16), [IntPtr]::Zero)
    [void][NativeSmoke]::SendMessage($editor, 0x020A, [IntPtr](-2160 -shl 16), [IntPtr]::Zero)
    Capture-Window $main (Join-Path $root 'test-results\03-page-break.png')

    [void][NativeSmoke]::ShowWindow($main, 1)
    [void][ReleaseWindow]::SetWindowPos($main, [IntPtr]::Zero, 0, 0, 1366, 900, 0x0004)
    Command 1133
    $chooser = [NativeSmoke]::FindAnyChildById($main, 1137)
    if ($chooser -eq [IntPtr]::Zero) { throw 'Compact format chooser is missing' }
    Command 1138
    if ([NativeSmoke]::FindAnyChildById($main, 1105) -eq [IntPtr]::Zero) { throw 'Inspector toggle did not reveal properties' }
    Command 1138
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][NativeSmoke]::SendMessage($editor, 0x00B7, [IntPtr]::Zero, [IntPtr]::Zero)
    if ((Query 84) -ne $pages) { throw 'Window width changes physical pagination' }
    [void][NativeSmoke]::SendMessage($editor, 0x0100, [IntPtr]27, [IntPtr]::Zero)
    Capture-Window $main (Join-Path $root 'test-results\04-editor-1366.png')
    Write-Output "PASS: 1366-pixel and maximized layouts have the same $pages screenplay pages."

    [void][NativeSmoke]::PostMessage($main, 0x0111, [IntPtr]6192, [IntPtr]::Zero)
    $preview = Wait-Window ([uint32]$process.Id) 'F12' 10000
    Capture-Window $preview (Join-Path $root 'test-results\05-f12-title.png')
    [void][NativeSmoke]::SendMessage($preview, 0x020A, [IntPtr](-960 -shl 16), [IntPtr]::Zero)
    Capture-Window $preview (Join-Path $root 'test-results\06-f12-script.png')
} finally {
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    $env:MEZOZOY_PROFILE_DIR = $oldProfile
}
