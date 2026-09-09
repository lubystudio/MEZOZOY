param([Parameter(Mandatory=$true)][string]$Exe)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $PSScriptRoot 'gui-smoke-input.mzoy'
. (Join-Path $PSScriptRoot 'gui-smoke.ps1') -Exe $Exe -Project $fixture -OnlyFunctions
$copy = Join-Path $PSScriptRoot 'screenplay-workflow.mzoy'
Copy-Item -LiteralPath $fixture -Destination $copy -Force
$oldProfile = $env:MEZOZOY_PROFILE_DIR
$env:MEZOZOY_PROFILE_DIR = Join-Path $root 'test-results\editor-profile'
[void](New-Item -ItemType Directory -Force -Path $env:MEZOZOY_PROFILE_DIR)
$process = $null
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message }; Write-Output "PASS: $Message" }
function Send-Text([string]$Text) {
    [void][NativeSmoke]::SendMessageText($script:editor, 0x00C2, [IntPtr]1, [Text.StringBuilder]::new($Text))
}
function Key([int]$Code, [bool]$Character = $false) {
    [void][NativeSmoke]::SendMessage($script:editor, 0x0100, [IntPtr]$Code, [IntPtr]::Zero)
    if ($Character) { [void][NativeSmoke]::SendMessage($script:editor, 0x0102, [IntPtr]$Code, [IntPtr]::Zero) }
}
function Format([int]$Id) {
    $button = [NativeSmoke]::FindAnyChildById($script:main, $Id)
    Assert ($button -ne [IntPtr]::Zero) "Format control $Id exists"
    [void][NativeSmoke]::SendMessage($button, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
}
function Query([int]$Offset, [int]$Value=0) {
    return [NativeSmoke]::SendMessage($script:page, [uint32](0x8000 + $Offset), [IntPtr]$Value, [IntPtr]::Zero).ToInt64()
}
try {
    $process = Start-Process -FilePath $Exe -ArgumentList ('"' + $copy + '"') -WindowStyle Hidden -PassThru
    $script:main = Wait-Window ([uint32]$process.Id) 'Mezozoy' 15000
    [void][NativeSmoke]::ShowWindow($main, 3)
    Open-Page $main 6105 (Join-Path $root 'test-results\workflow-start.png')
    $script:editor = [NativeSmoke]::FindAnyChildById($main, 1104)
    Assert ($editor -ne [IntPtr]::Zero) 'Continuous editor exists'
    $script:page = [NativeSmoke]::GetParent($editor)
    [void](Query 77)

    # Whole-text replacement and one-step undo must restore text AND scene metadata.
    $before = [NativeSmoke]::ControlText($editor)
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr]::Zero, [IntPtr](-1))
    Send-Text 'NEW HEADING'
    Start-Sleep -Milliseconds 600
    [void](Query 85)
    Assert ([NativeSmoke]::ControlText($editor) -eq $before) 'Undo restores the entire screenplay after replacement'
    [void](Query 85 1)
    Assert ([NativeSmoke]::ControlText($editor) -eq 'NEW HEADING') 'Redo restores replacement text'
    [void](Query 85)
    $scenes = [NativeSmoke]::FindAnyChildById($main, 1101)
    Assert ([NativeSmoke]::SendMessage($scenes, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -eq 3) 'Undo restores all three scene objects'

    # Keep selection and paragraph type while formatting, including a parenthetical.
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr](-1), [IntPtr](-1))
    Send-Text "`r`n(тихо)"
    Format 1111
    $text = [NativeSmoke]::ControlText($editor)
    $end = $text.Replace("`r`n", "`r").Length
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr]($end-1), [IntPtr]($end-1))
    $parentheticalType = (Query 79) -band 0xFFFF
    Key 13 $true
    $after = [NativeSmoke]::ControlText($editor)
    Assert (-not $after.EndsWith("`r`n)")) "Enter keeps the closing parenthesis on the parenthetical line (type=$parentheticalType, tail=$($after.Substring([Math]::Max(0,$after.Length-32))))"
    $dialogueType = (Query 79) -band 0xFFFF
    Assert ($dialogueType -ne $parentheticalType) 'Enter changes Parenthetical to Dialogue'
    [void](Query 85)
    Assert ([NativeSmoke]::ControlText($editor) -eq $text) 'One undo restores the parenthetical and its text'
    Assert (((Query 79) -band 0xFFFF) -eq $parentheticalType) 'Undo restores semantic formatting'

    # Escape dismisses suggestions until the query changes; ordinary action has none.
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr](-1), [IntPtr](-1))
    Send-Text "`r`nМ"
    Format 1110
    Assert (((Query 78) -shr 16) -eq 1) 'Character completion opens for the first letter'
    Key 27
    Assert (((Query 78) -shr 16) -eq 0) 'Escape does not immediately reopen the same completion'
    Send-Text 'И'
    Assert (((Query 78) -shr 16) -eq 1) 'Changing the prefix allows completion again'
    Key 9 $true
    Assert ([NativeSmoke]::ControlText($editor).EndsWith('МИША')) 'Tab accepts the name in one action'
    Key 13 $true
    Send-Text 'Я здесь.'
    Key 13 $true
    Send-Text 'И'
    Assert (((Query 78) -shr 16) -eq 0) 'INT and character suggestions stay out of Action'
    $beforeDeletion = [NativeSmoke]::ControlText($editor)
    Key 8 $true
    Assert ([NativeSmoke]::ControlText($editor) -eq $beforeDeletion.Substring(0, $beforeDeletion.Length-1)) 'Backspace deletes exactly one character'
    [void](Query 85)
    Assert ([NativeSmoke]::ControlText($editor) -eq $beforeDeletion) 'One undo restores Backspace'
    Send-Text "`r`nМ"
    Format 1110
    $beforeAccept = [NativeSmoke]::ControlText($editor)
    Key 13 $true
    Assert ([NativeSmoke]::ControlText($editor).EndsWith("МИША`r`n")) 'Enter accepts the character and creates exactly one dialogue line'
    [void](Query 85)
    Assert ([NativeSmoke]::ControlText($editor) -eq $beforeAccept) 'One undo restores the prefix before Enter acceptance'

    # A long paragraph must cross physical pages; zoom must not repaginate.
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr](-1), [IntPtr](-1))
    Send-Text ("`r`n" + ('Длинный абзац. ' * 14000))
    Format 1109
    $pages = Query 84
    Assert ($pages -gt 40) 'Long continuous text produces more than forty finite pages'
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Send-Text 'КОНЕЦ'
    $watch.Stop()
    Assert ($watch.ElapsedMilliseconds -lt 1500) "Typing in a long document completes within 1500 ms ($($watch.ElapsedMilliseconds) ms)"
    foreach ($id in @(1132,1132,1134,1133)) {
        $button = [NativeSmoke]::FindAnyChildById($main, $id)
        [void][NativeSmoke]::SendMessage($button, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
        Assert ((Query 84) -eq $pages) 'Display zoom preserves the physical page count'
    }
    [void][NativeSmoke]::SendMessage($editor, 0x00B1, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][NativeSmoke]::SendMessage($editor, 0x00B7, [IntPtr]::Zero, [IntPtr]::Zero)
    Capture-Window $main (Join-Path $root 'test-results\workflow-long-pages.png')

    # Saving/reopening must retain the text and explicit styles.
    [void][NativeSmoke]::SendMessage($main, 0x0111, [IntPtr]6190, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
    $saved = Get-Content -Raw -LiteralPath $copy
    Assert ($saved.Contains('КОНЕЦ')) 'The long-document edit reaches the saved project'
    Write-Output 'Screenplay workflow checks completed.'
} finally {
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    $env:MEZOZOY_PROFILE_DIR = $oldProfile
}
