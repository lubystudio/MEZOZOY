param([Parameter(Mandatory=$true)][string]$Exe)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$sample=Join-Path $root 'examples\Horizon.mzoy'
. (Join-Path $PSScriptRoot 'gui-smoke.ps1') -Exe $Exe -Project $sample -OnlyFunctions -WindowOnly
$oldProfile=$env:MEZOZOY_PROFILE_DIR
$env:MEZOZOY_PROFILE_DIR=Join-Path $root 'test-results\ui96-profile'
[void](New-Item -ItemType Directory -Force -Path $env:MEZOZOY_PROFILE_DIR)
$copy=Join-Path $root 'test-results\ui96-project.mzoy'
Copy-Item -LiteralPath $sample -Destination $copy -Force
$out=Join-Path $root 'test-results\ui96'
[void](New-Item -ItemType Directory -Force -Path $out)
function Send($w,[uint32]$m,[long]$a=0,[long]$b=0) { [NativeSmoke]::SendMessage($w,$m,[IntPtr]$a,[IntPtr]$b).ToInt64() }
function Child([int]$id) {
    $w=[NativeSmoke]::FindAnyChildById($script:main,$id)
    if($w -eq [IntPtr]::Zero) { throw "Missing visible control $id" }; $w
}
function Click([int]$id) { [void](Send (Child $id) 0xF5) }
function Shot($name) { Capture-Window $script:main (Join-Path $out "$name.png") }
$process=$null
try {
    $process=Start-Process -FilePath $Exe -ArgumentList ('"'+$copy+'"') -WindowStyle Hidden -PassThru
    $script:main=Wait-Window ([uint32]$process.Id) 'Mezozoy' 15000
    [void][NativeSmoke]::ShowWindow($main,3)
    Open-Page $main 6105 (Join-Path $out '01-screenplay.png')
    $editor=Child 1104; $page=[NativeSmoke]::GetParent($editor)
    Click 1119
    $before=[NativeSmoke]::ControlText($editor)
    $pages=Send $page 0x8054
    $times=@()
    for($i=0;$i -lt 30;$i++) {
        $watch=[Diagnostics.Stopwatch]::StartNew()
        Click 1118; Click 1119
        $watch.Stop(); $times+=$watch.Elapsed.TotalMilliseconds
        if([NativeSmoke]::ControlText($editor) -cne $before) { throw "Mode switch $i changed text" }
        if((Send $page 0x8054) -ne $pages) { throw 'Mode switching changed pagination' }
    }
    "PASS: 60 mode switches preserve exact text and $pages pages; pair median=$([Math]::Round(($times|Sort-Object)[15],1))ms, max=$([Math]::Round(($times|Measure-Object -Maximum).Maximum,1))ms."
    Shot '01-screenplay'
    Click 1118; Shot '02-scene-mode'; Click 1119
    Open-Page $main 6103 (Join-Path $out '03-development.png')
    $tree=Child 2101
    $project=Send $tree 0x110A 0 0
    $people=Send $tree 0x110A 1 $project
    [void](Send $tree 0x1102 2 $people)
    $person=Send $tree 0x110A 4 $people
    if(!$person) { throw 'Character tree row missing' }
    [void](Send $tree 0x110B 9 $person)
    Shot '04-character-row'
    Click 2109
    $categories=Child 8202
    $rowHeight=Send $categories 0x01A1 0
    if($rowHeight -lt 36) { throw "Dossier rows still cramped: $rowHeight" }
    Shot '05-character'
    [void](Send $categories 0x0186 4)
    [void](Send ([NativeSmoke]::GetParent($categories)) 0x0111 (8202 -bor (1 -shl 16)) ($categories.ToInt64()))
    for($i=0;$i -lt 12;$i++) { [void](Send ([NativeSmoke]::GetParent($categories)) 0x020A (-120 -shl 16)) }
    Shot '06-character-scrolled'
    Click 8201
    $tree=Child 2101
    $project=Send $tree 0x110A 0 0
    $people=Send $tree 0x110A 1 $project
    $worlds=Send $tree 0x110A 1 $people
    $locations=Send $tree 0x110A 1 $worlds
    [void](Send $tree 0x1102 2 $locations)
    $location=Send $tree 0x110A 4 $locations
    [void](Send $tree 0x110B 9 $location)
    Shot '07-location-row'
    Click 2110
    Shot '08-location'
    Click 8401
    $tree=Child 2101
    [void](Send ([NativeSmoke]::GetParent($tree)) 0x0111 7306)
    $project=Send $tree 0x110A 0 0
    $people=Send $tree 0x110A 1 $project
    [void](Send $tree 0x1102 2 $people)
    $folder=Send $tree 0x110A 4 $people
    [void](Send $tree 0x110B 9 $folder)
    Shot '11-folder'
    Open-Page $main 6104 (Join-Path $out '09-cards.png')
    $board=[NativeSmoke]::FindVisibleChildByClass($main,'Mezozoy.BoardCanvas')
    if($board -eq [IntPtr]::Zero) { throw 'Board canvas missing' }
    $point=Send $board 0x805A 0
    $locked=Send $board 0x805B 0
    [void](Send $board 0x0200 0 $point)
    [void](Send $board 0x0201 1 $point)
    [void](Send $board 0x0202 0 $point)
    if((Send $board 0x805B 0) -eq $locked) { throw 'One click failed to toggle card lock' }
    Shot '09-cards'
    if(!(Send $board 0x805D) -or (Send $board 0x805C) -ne 3) { throw 'Native board snapshot failed' }
    Copy-Item -LiteralPath (Join-Path $env:MEZOZOY_PROFILE_DIR 'board-snapshot.png') -Destination (Join-Path $out '09-board.png') -Force
    [void](Send $board 0x0201 1 $point)
    [void](Send $board 0x0202 0 $point)
    if((Send $board 0x805B 0) -ne $locked) { throw 'Card did not unlock' }
    'PASS: The lock toggles both ways on one mouse click.'
    Open-Page $main 6110 (Join-Path $out '10-settings.png')
    Click 5300
    Start-Sleep -Milliseconds 100
    Click 5306
    Start-Sleep -Milliseconds 100
    Shot '12-green-accent'
    $bitmap=[Drawing.Bitmap]::FromFile((Join-Path $out '12-green-accent.png'))
    try {
        $green=0
        for($x=16;$x -lt 210;$x+=2) { for($y=315;$y -lt 353;$y+=2) {
            $color=$bitmap.GetPixel($x,$y)
            if($color.G -gt $color.R*1.25 -and $color.G -gt $color.B*1.1) { $green++ }
        } }
        if($green -lt 100) { throw "Accent did not reach navigation: $green green pixels" }
    } finally { $bitmap.Dispose() }
    Click 5300
    'PASS: Changing the accent recolors the navigation selection.'
    'PASS: Development, character dossier, scrolled fields, locations, cards and settings captured from the built EXE.'
} finally {
    if($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    $env:MEZOZOY_PROFILE_DIR=$oldProfile
}
