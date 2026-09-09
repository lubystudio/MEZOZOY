param([Parameter(Mandatory=$true)][string]$Exe,[switch]$CornersOnly)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'themes-smoke.ps1') -Exe $Exe -OnlyFunctions
$root=Split-Path -Parent $PSScriptRoot
$out=Join-Path $root 'test-results\polish981'
$profile=Join-Path $out 'profile'
[void](New-Item -ItemType Directory -Force -Path $out,$profile)
$copy=Join-Path $out 'sample.mzoy'
Copy-Item -LiteralPath (Join-Path $root 'examples\Horizon.mzoy') -Destination $copy -Force
$emptyCopy=$copy
$photoCopy=Join-Path $out 'photos.mzoy'
$bitmap=New-Object Drawing.Bitmap 16,16
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$stream=New-Object IO.MemoryStream
try {
    $graphics.Clear([Drawing.Color]::White)
    $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
    $base64=[Convert]::ToBase64String($stream.ToArray())
} finally { $graphics.Dispose(); $bitmap.Dispose(); $stream.Dispose() }
[xml]$fixture=Get-Content -LiteralPath $copy -Raw -Encoding UTF8
function XmlValue($parent,$name,$value) {
    $node=$fixture.CreateElement($name); $node.InnerText=$value; [void]$parent.AppendChild($node)
}
XmlValue $fixture.DocumentElement 'PosterImageData' $base64
XmlValue $fixture.DocumentElement 'PosterImageFormat' 'png'
XmlValue $fixture.SelectSingleNode('//Characters/Character[1]') 'AvatarImageData' $base64
XmlValue $fixture.SelectSingleNode('//Characters/Character[1]') 'AvatarImageFormat' 'png'
XmlValue $fixture.SelectSingleNode('//Locations/LocationItem[1]') 'LocationImageData' $base64
XmlValue $fixture.SelectSingleNode('//Locations/LocationItem[1]') 'LocationImageFormat' 'png'
$references=$fixture.CreateElement('References'); [void]$fixture.DocumentElement.AppendChild($references)
$reference=$fixture.CreateElement('ReferenceItem'); [void]$references.AppendChild($reference)
XmlValue $reference 'Id' '1000'; XmlValue $reference 'Title' 'Image contrast test'
XmlValue $reference 'ReferenceImageData' $base64; XmlValue $reference 'ReferenceImageFormat' 'png'
XmlValue $reference 'AspectRatio' '16:9'
$fixture.Save($photoCopy)
[IO.File]::WriteAllText((Join-Path $profile 'settings.json'),(@{ThemeMode='Darker';UiScale=1;AutosaveEnabled=$false;AskToSaveBeforeClosing=$false;DefaultProjectsFolder=$out}|ConvertTo-Json))
$oldProfile=$env:MEZOZOY_PROFILE_DIR; $env:MEZOZOY_PROFILE_DIR=$profile
function Send($w,[uint32]$m,[long]$a=0,[long]$b=0) { [NativeSmoke]::SendMessage($w,$m,[IntPtr]$a,[IntPtr]$b).ToInt64() }
function Child([int]$id) {
    $w=[NativeSmoke]::FindAnyChildById($script:main,$id)
    if($w -eq [IntPtr]::Zero) { throw "Missing control $id" }; $w
}
function Click([int]$id) { [void](Send (Child $id) 0xF5); Start-Sleep -Milliseconds 70 }
function Page([int]$id) { [void](Send $main 0x111 $id); Start-Sleep -Milliseconds 100 }
function Shot($name,$window=$script:main) {
    $image=[ThemeProbe]::Image($window)
    try { $image.Save((Join-Path $out "$name.png")) } finally { $image.Dispose() }
}
function Corners([int]$id,$background) {
    $w=Child $id
    foreach($state in @(0,1,2)) {
        if($state -eq 1) { [void](Send $w 0x200 0 (12 -bor (12 -shl 16))) }
        if($state -eq 2) { [void](Send $w 0x2A3) }
        $image=[ThemeProbe]::Image($w)
        try {
            foreach($point in @(@(0,0),@(($image.Width-1),0),@(0,($image.Height-1)),@(($image.Width-1),($image.Height-1)))) {
                if($image.GetPixel($point[0],$point[1]).ToArgb() -ne $background.ToArgb()) {
                    $image.Save((Join-Path $out "failed-corner-$id.png"))
                    throw "Control $id has the wrong corner backdrop, state $state"
                }
            }
        } finally { $image.Dispose() }
    }
}
function Category([int]$id,[int]$index) {
    $list=Child $id
    [void](Send $list 0x186 $index)
    [void](Send ([NativeSmoke]::GetParent($list)) 0x111 ($id -bor (1 -shl 16)) $list.ToInt64())
    Start-Sleep -Milliseconds 70
}
function Photo([int]$id,$panel,$name) {
    $image=[ThemeProbe]::Image((Child $id))
    try {
        if($image.GetPixel(10,$image.Height-20).ToArgb() -ne $panel.ToArgb()) { throw "Image caption backdrop not themed: $id" }
        $image.Save((Join-Path $out ($name+'.png')))
    } finally { $image.Dispose() }
}
function Roots {
    $script:tree=Child 2101
    $script:project=Send $tree 0x110A
    $script:people=Send $tree 0x110A 1 $project
    $script:worlds=Send $tree 0x110A 1 $people
    $script:places=Send $tree 0x110A 1 $worlds
    $script:refs=Send $tree 0x110A 1 $places
}
$process=$null
function Start-App([bool]$withProject) {
    if($script:process -and !$script:process.HasExited) { Stop-Process -Id $script:process.Id -Force; $script:process.WaitForExit() }
    $args=@{FilePath=$Exe;WindowStyle='Hidden';PassThru=$true}
    if($withProject) { $args.ArgumentList='"'+$copy+'"' }
    $script:process=Start-Process @args
    $script:main=Wait-Window ([uint32]$script:process.Id) 'Mezozoy' 15000
    [void][NativeSmoke]::ShowWindow($script:main,3); Start-Sleep -Milliseconds 250
}
try {
    foreach($mode in @(@('Darker',5103,18,19,22,13,14,16),@('Dark',5102,25,26,30,20,21,24),@('Light',5133,250,250,248,235,236,234))) {
        Start-App $false
        Page 6110; Category 5101 0; Click $mode[1]
        $panel=[Drawing.Color]::FromArgb($mode[2],$mode[3],$mode[4])
        $nav=[Drawing.Color]::FromArgb($mode[5],$mode[6],$mode[7])
        foreach($id in @(5102,5103,5133,5320,5321,5322,5323,5105)) { Corners $id $panel }
        Page 6101
        foreach($id in @(3101,3102,3103,6190,6197,6198,6195)) { Corners $id $panel }
        Corners 6101 $nav
        Shot ($mode[0]+'-home'); Shot ($mode[0]+'-home-button') (Child 3101)
        Shot ($mode[0]+'-navigation') (Child 6101)
        if($CornersOnly) { 'PASS: button and navigation corners'; break }
        Start-App $true
        Page 6105; Click 1119; Shot ($mode[0]+'-script'); Click 1118; Shot ($mode[0]+'-scene')
        Page 6104; Shot ($mode[0]+'-cards')
        $board=[NativeSmoke]::FindVisibleChildByClass($main,'Mezozoy.BoardCanvas')
        if(!(Send $board 0x805D)) { throw 'Board render failed' }
        Copy-Item -LiteralPath (Join-Path $profile 'board-snapshot.png') -Destination (Join-Path $out ($mode[0]+'-board.png')) -Force
        Page 6103; Roots
        [void](Send $tree 0x110B 9 $project); Shot ($mode[0]+'-project')
        [void](Send $tree 0x1102 2 $people); [void](Send $tree 0x110B 9 (Send $tree 0x110A 4 $people)); Click 2109
        $count=Send (Child 8202) 0x18B
        for($i=0;$i -lt $count;$i++) {
            Category 8202 $i; Shot ($mode[0]+"-character-$i")
            $parent=[NativeSmoke]::GetParent((Child 8202))
            for($j=0;$j -lt 8;$j++) { [void](Send $parent 0x20A (-120 -shl 16)) }
            Shot ($mode[0]+"-character-$i-scrolled")
        }
        Click 8201; Roots
        [void](Send $tree 0x1102 2 $places); [void](Send $tree 0x110B 9 (Send $tree 0x110A 4 $places)); Click 2110
        $count=Send (Child 8402) 0x18B
        for($i=0;$i -lt $count;$i++) {
            Category 8402 $i; Shot ($mode[0]+"-location-$i")
            $parent=[NativeSmoke]::GetParent((Child 8402))
            for($j=0;$j -lt 8;$j++) { [void](Send $parent 0x20A (-120 -shl 16)) }
            Shot ($mode[0]+"-location-$i-scrolled")
        }
        Click 8401; Roots
        [void](Send ([NativeSmoke]::GetParent($tree)) 0x111 7303); Roots
        [void](Send $tree 0x1102 2 $worlds); [void](Send $tree 0x110B 9 (Send $tree 0x110A 4 $worlds)); Shot ($mode[0]+'-world')
        [void](Send ([NativeSmoke]::GetParent($tree)) 0x111 7304); Roots
        [void](Send $tree 0x1102 2 $refs); [void](Send $tree 0x110B 9 (Send $tree 0x110A 4 $refs))
        foreach($id in @(8803,8804,8805)) { Click $id; Shot ($mode[0]+"-reference-$id") }
        Page 6110
        for($i=0;$i -lt 6;$i++) { Category 5101 $i; Shot ($mode[0]+"-settings-$i") }
        $copy=$photoCopy; Start-App $true; Page 6103; Roots
        [void](Send $tree 0x110B 9 $project); Photo 8601 $panel ($mode[0]+'-poster-caption')
        [void](Send $tree 0x1102 2 $people); [void](Send $tree 0x110B 9 (Send $tree 0x110A 4 $people)); Click 2109
        Photo 8203 $panel ($mode[0]+'-character-caption'); Click 8201; Roots
        [void](Send $tree 0x1102 2 $places); [void](Send $tree 0x110B 9 (Send $tree 0x110A 4 $places)); Click 2110
        Photo 8403 $panel ($mode[0]+'-location-caption'); Click 8401; Roots
        [void](Send $tree 0x1102 2 $refs); [void](Send $tree 0x110B 9 (Send $tree 0x110A 4 $refs))
        Photo 8806 $panel ($mode[0]+'-reference-caption')
        $copy=$emptyCopy
        "PASS: $($mode[0]) corners normal/hover/leave; main tabs, every dossier category, scroll states, references and settings captured."
        "PASS: $($mode[0]) loaded poster, character, location and reference image captions use the theme surface."
    }
} finally {
    if($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    $env:MEZOZOY_PROFILE_DIR=$oldProfile
}
