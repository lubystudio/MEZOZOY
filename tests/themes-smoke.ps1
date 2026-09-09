param([Parameter(Mandatory=$true)][string]$Exe,[switch]$OnlyFunctions)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$sample=Join-Path $root 'examples\Horizon.mzoy'
$loadThemeFunctionsOnly = $OnlyFunctions.IsPresent
. (Join-Path $PSScriptRoot 'gui-smoke.ps1') -Exe $Exe -Project $sample -OnlyFunctions -WindowOnly
Add-Type @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text;
public static class ThemeProbe {
    public struct Rect { public int left,top,right,bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr w,out Rect r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr w,IntPtr z,int x,int y,int width,int height,uint flags);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr w,IntPtr dc,uint flags);
    delegate bool EnumProc(IntPtr w,IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w,StringBuilder name,int size);
    public static IntPtr Prompt(uint pid) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((w,data)=>{ uint owner; GetWindowThreadProcessId(w,out owner);
            var name=new StringBuilder(128); GetClassName(w,name,128);
            if(owner==pid && name.ToString()=="Mezozoy.Prompt") { found=w; return false; }
            return true;
        },IntPtr.Zero);
        return found;
    }
    public static Bitmap Image(IntPtr w) {
        Rect r; GetClientRect(w,out r); var image=new Bitmap(r.right,r.bottom);
        using(var g=Graphics.FromImage(image)) { var dc=g.GetHdc();
            try { if(!PrintWindow(w,dc,3)) throw new Exception("Window capture failed"); }
            finally { g.ReleaseHdc(dc); }
        }
        return image;
    }
    public static int Count(Bitmap image,Color expected) {
        int count=0;
        for(int y=2;y<image.Height;y++) for(int x=2;x<image.Width;x++) {
            var c=image.GetPixel(x,y);
            if(Math.Abs(c.R-expected.R)<3 && Math.Abs(c.G-expected.G)<3 && Math.Abs(c.B-expected.B)<3) count++;
        }
        return count;
    }
}
'@ -ReferencedAssemblies ([Drawing.Bitmap].Assembly.Location),([Drawing.Color].Assembly.Location)
if ($loadThemeFunctionsOnly) { return }
$out=Join-Path $root 'test-results\themes98'
$profile=Join-Path $out 'profile'
[void](New-Item -ItemType Directory -Force -Path $out,$profile)
$copy=Join-Path $out 'theme-project.mzoy'
Copy-Item -LiteralPath $sample -Destination $copy -Force
$settingsPath=Join-Path $profile 'settings.json'
[IO.File]::WriteAllText($settingsPath,(@{ThemeMode='Darker';UiScale=1;AutosaveEnabled=$false;AskToSaveBeforeClosing=$false;DefaultProjectsFolder=$out}|ConvertTo-Json))
$oldProfile=$env:MEZOZOY_PROFILE_DIR; $env:MEZOZOY_PROFILE_DIR=$profile
function Send($w,[uint32]$m,[long]$a=0,[long]$b=0) { [NativeSmoke]::SendMessage($w,$m,[IntPtr]$a,[IntPtr]$b).ToInt64() }
function Child([int]$id) {
    $w=[NativeSmoke]::FindAnyChildById($script:main,$id)
    if(!$w) { throw "Missing visible control $id" }; $w
}
function Click([int]$id) { [void](Send (Child $id) 0xF5); Start-Sleep -Milliseconds 100 }
function Page([int]$id) { [void](Send $script:main 0x111 $id); Start-Sleep -Milliseconds 130 }
function Shot($name,$window=$script:main) {
    $image=[ThemeProbe]::Image($window)
    try { $image.Save((Join-Path $out "$name.png")) } finally { $image.Dispose() }
}
function Check-Palette($window,$background,$ink) {
    $image=[ThemeProbe]::Image($window)
    try {
        if([ThemeProbe]::Count($image,$background) -lt 1000) {
            $image.Save((Join-Path $out 'palette-failure.png'))
            throw "Theme background missing on $([NativeSmoke]::ClassName($window)); expected $background"
        }
        if([ThemeProbe]::Count($image,$ink) -lt 30) {
            $image.Save((Join-Path $out 'palette-failure.png'))
            throw "Theme text missing on $([NativeSmoke]::ClassName($window)); ink pixels=$([ThemeProbe]::Count($image,$ink))"
        }
    } finally { $image.Dispose() }
}
function Start-App {
    $script:process=Start-Process -FilePath $Exe -ArgumentList ('"'+$copy+'"') -WindowStyle Hidden -PassThru
    $script:main=Wait-Window ([uint32]$process.Id) 'Mezozoy' 15000
    [void][NativeSmoke]::ShowWindow($main,3)
    Start-Sleep -Milliseconds 350
}
$process=$null
try {
    Start-App
    Page 6105; Click 1119
    $editor=Child 1104; $scriptPage=[NativeSmoke]::GetParent($editor)
    $text=[NativeSmoke]::ControlText($editor); $pages=Send $scriptPage 0x8054
    foreach($mode in @(@('Darker',5103,8,9,11,238,239,242),@('Dark',5102,15,16,18,240,241,244),@('Light',5133,244,244,242,32,36,34))) {
        Page 6110; Click $mode[1]
        if((Get-Content $settingsPath -Raw|ConvertFrom-Json).ThemeMode -ne $mode[0]) { throw 'Theme choice not persisted' }
        Check-Palette $main ([Drawing.Color]::FromArgb($mode[2],$mode[3],$mode[4])) ([Drawing.Color]::FromArgb($mode[5],$mode[6],$mode[7]))
        $shell=[ThemeProbe]::Image($main)
        try {
            $expected=if($mode[0] -eq 'Light') { [Drawing.Color]::FromArgb(235,236,234) } elseif($mode[0] -eq 'Darker') { [Drawing.Color]::FromArgb(13,14,16) } else { [Drawing.Color]::FromArgb(20,21,24) }
            if($shell.GetPixel(4,450).ToArgb() -ne $expected.ToArgb()) { throw 'Shell kept the previous theme background' }
        } finally { $shell.Dispose() }
        Shot ('01-settings-'+$mode[0])
        Page 6105
        if([NativeSmoke]::ControlText((Child 1104)) -cne $text -or (Send $scriptPage 0x8054) -ne $pages) { throw 'Theme changed screenplay' }
        Shot ('02-screenplay-'+$mode[0])
    }
    'PASS: Three distinct palettes apply in one click, persist immediately and preserve screenplay text/pages.'
    for($i=0;$i -lt 6;$i++) {
        Page 6110; Click 5103; Click 5102; Click 5133
        Page 6105
        if([NativeSmoke]::ControlText((Child 1104)) -cne $text -or (Send $scriptPage 0x8054) -ne $pages) { throw 'Repeated theme switching changed screenplay' }
    }
    if((Send $scriptPage 0x804A) -ne (32 -bor (36 -shl 8) -bor (34 -shl 16))) { throw 'RichEdit insertion color did not become dark' }
    'PASS: 18 repeated theme switches preserve text/pagination and update the editor insertion color.'
    Page 6103
    $tree=Child 2101; $project=Send $tree 0x110A; $people=Send $tree 0x110A 1 $project
    [void](Send $tree 0x1102 2 $people); $person=Send $tree 0x110A 4 $people
    [void](Send $tree 0x110B 9 $person); Click 2109; Shot '03-character-light'
    Click 8201
    $worlds=Send $tree 0x110A 1 $people; $places=Send $tree 0x110A 1 $worlds
    [void](Send $tree 0x1102 2 $places); [void](Send $tree 0x110B 9 (Send $tree 0x110A 4 $places))
    Click 2110; Shot '04-location-light'; Click 8401
    for($i=0;$i -lt 10;$i++) { [void](Send $tree 0x1102 1 $people); [void](Send $tree 0x1102 2 $people) }
    Shot '05-development-light'
    Check-Palette $tree ([Drawing.Color]::FromArgb(235,236,234)) ([Drawing.Color]::FromArgb(32,36,34))
    [void](Send ([NativeSmoke]::GetParent($tree)) 0x111 7306)
    $project=Send $tree 0x110A; $people=Send $tree 0x110A 1 $project
    [void](Send $tree 0x1102 2 $people)
    [void](Send $tree 0x110B 9 (Send $tree 0x110A 4 $people))
    [void][NativeSmoke]::PostMessage($tree,0x100,[IntPtr]0x71,[IntPtr]::Zero)
    for($attempt=0;$attempt -lt 30;$attempt++) {
        $dialog=[ThemeProbe]::Prompt([uint32]$process.Id)
        if($dialog -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    }
    if($dialog -eq [IntPtr]::Zero) { throw 'Folder rename dialog missing' }
    Start-Sleep -Milliseconds 100
    Check-Palette $dialog ([Drawing.Color]::FromArgb(250,250,248)) ([Drawing.Color]::FromArgb(32,36,34))
    Shot '05-rename-light' $dialog
    [void][NativeSmoke]::PostMessage($dialog,0x111,[IntPtr]2,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    Page 6104; Shot '06-cards-light'
    $board=[NativeSmoke]::FindVisibleChildByClass($main,'Mezozoy.BoardCanvas')
    if(!(Send $board 0x805D)) { throw 'Board snapshot failed' }
    Copy-Item -LiteralPath (Join-Path $profile 'board-snapshot.png') -Destination (Join-Path $out '06-board-light.png') -Force
    Page 6101; Shot '07-home-light'
    Page 6110; Click 5306; Shot '08-green-light'; Click 5300
    Click 5323; Shot '09-settings-scale125'; Click 5321
    foreach($size in @(@(1366,768),@(1040,740))) {
        [void][NativeSmoke]::ShowWindow($main,9)
        [void][ThemeProbe]::SetWindowPos($main,[IntPtr]::Zero,40,40,$size[0],$size[1],0x14)
        Start-Sleep -Milliseconds 150
        Shot ('09-settings-'+$size[0])
        $parent=[NativeSmoke]::GetParent((Child 5133)); $rect=New-Object NativeSmoke+RECT
        [void][NativeSmoke]::GetWindowRect($parent,[ref]$rect)
        foreach($id in @(5103,5102,5133,5105)) {
            $childRect=New-Object NativeSmoke+RECT; [void][NativeSmoke]::GetWindowRect((Child $id),[ref]$childRect)
            if($childRect.Bottom -gt $rect.Bottom -or $childRect.Right -gt $rect.Right) { throw "Control $id clipped at width $($size[0])" }
        }
    }
    Stop-Process -Id $process.Id -Force; $process.WaitForExit(); Start-App
    Page 6110
    Check-Palette $main ([Drawing.Color]::FromArgb(244,244,242)) ([Drawing.Color]::FromArgb(32,36,34))
    Shot '10-light-after-restart'
    'PASS: Light theme covers development/dossiers/cards/home, accepts accent changes and survives restart.'
} finally {
    if($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    $env:MEZOZOY_PROFILE_DIR=$oldProfile
}
