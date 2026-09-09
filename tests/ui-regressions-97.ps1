param([Parameter(Mandatory=$true)][string]$Exe)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$sample=Join-Path $root 'examples\Horizon.mzoy'
. (Join-Path $PSScriptRoot 'gui-smoke.ps1') -Exe $Exe -Project $sample -OnlyFunctions -WindowOnly
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Drawing;
public static class Ui97 {
    [StructLayout(LayoutKind.Sequential)] public struct ScrollInfo {
        public uint size, mask; public int min, max; public uint page; public int pos, track;
    }
    [DllImport("user32.dll")] static extern bool GetScrollInfo(IntPtr w,int bar,ref ScrollInfo info);
    [StructLayout(LayoutKind.Sequential)] public struct BarInfo {
        public int size,left,top,right,bottom,button,thumbTop,thumbBottom,reserved;
        [MarshalAs(UnmanagedType.ByValArray,SizeConst=6)] public uint[] state;
    }
    [DllImport("user32.dll",SetLastError=true)] static extern bool GetScrollBarInfo(IntPtr w,int id,ref BarInfo info);
    public static string Bar(IntPtr w) {
        var b=new BarInfo { size=60 };
        bool ok=GetScrollBarInfo(w,-4,ref b);
        if(!ok || b.thumbBottom<=b.thumbTop) throw new Exception("Missing scrollbar thumb geometry");
        return "Native thumb: "+b.thumbTop+".."+b.thumbBottom;
    }
    [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtrW(IntPtr w,int index);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr w);
    [DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr w);
    [DllImport("user32.dll")] static extern IntPtr GetWindowDC(IntPtr w);
    struct Rect { public int left, top, right, bottom; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr w,out Rect r);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr w,IntPtr dc);
    [DllImport("gdi32.dll")] static extern bool BitBlt(IntPtr dst,int x,int y,int width,int height,IntPtr src,int sx,int sy,uint op);
    public static ScrollInfo Scroll(IntPtr w) {
        var info=new ScrollInfo { size=28, mask=23 };
        if(!GetScrollInfo(w,2,ref info)) throw new Exception("Scrollbar state unavailable");
        return info;
    }
    public static Bitmap Live(IntPtr w) {
        Rect r; GetWindowRect(w,out r);
        var image=new Bitmap(r.right-r.left,r.bottom-r.top);
        using(var g=Graphics.FromImage(image)) {
            var target=g.GetHdc(); var source=GetWindowDC(w);
            try { if(!BitBlt(target,0,0,image.Width,image.Height,source,0,0,0x00CC0020)) throw new Exception("Capture failed"); }
            finally { ReleaseDC(w,source); g.ReleaseHdc(target); }
        }
        return image;
    }
    public static int Different(Bitmap a,Bitmap b) {
        int n=0;
        for(int y=0;y<a.Height;y++) for(int x=0;x<a.Width;x++) if(a.GetPixel(x,y)!=b.GetPixel(x,y)) n++;
        return n;
    }
    public static int ThumbPixels(Bitmap a) {
        int n=0;
        for(int y=18;y<a.Height-18;y++) {
            var c=a.GetPixel(a.Width/2,y);
            if(c.R>50 && c.G>50 && c.B>50) n++;
        }
        return n;
    }
}
'@ -ReferencedAssemblies ([Drawing.Bitmap].Assembly.Location),([Drawing.Color].Assembly.Location)
$oldProfile=$env:MEZOZOY_PROFILE_DIR
$env:MEZOZOY_PROFILE_DIR=Join-Path $root 'test-results\ui97-profile'
$out=Join-Path $root 'test-results\ui97'
[void](New-Item -ItemType Directory -Force -Path $out,$env:MEZOZOY_PROFILE_DIR)
$copy=Join-Path $root 'test-results\ui97-project.mzoy'
Copy-Item -LiteralPath $sample -Destination $copy -Force
function Send($w,[uint32]$m,[long]$a=0,[long]$b=0) { [NativeSmoke]::SendMessage($w,$m,[IntPtr]$a,[IntPtr]$b).ToInt64() }
function Child([int]$id) {
    $w=[NativeSmoke]::FindAnyChildById($script:main,$id)
    if($w -eq [IntPtr]::Zero) { throw "Missing visible control $id" }; $w
}
function Click([int]$id) { [void](Send (Child $id) 0xF5) }
function Shot($name) { Capture-Window $script:main (Join-Path $out "$name.png") }
function PromptEdit($dialog) {
    for($i=0;$i -lt 50;$i++) {
        $edit=[NativeSmoke]::FindAnyChildById($dialog,1002)
        if($edit -ne [IntPtr]::Zero) { return $edit }
        Start-Sleep -Milliseconds 20
    }
    throw 'Rename edit missing'
}
function Select-FirstFolder([int]$menu) {
    $tree=Child 2101; $dev=[NativeSmoke]::GetParent($tree)
    [void](Send $dev 0x111 $menu)
    $project=Send $tree 0x110A 0
    $section=Send $tree 0x110A 1 $project
    if($menu -eq 7307) { $world=Send $tree 0x110A 1 $section; $section=Send $tree 0x110A 1 $world }
    [void](Send $tree 0x1102 2 $section)
    $folder=Send $tree 0x110A 4 $section
    [void](Send $tree 0x110B 9 $folder)
}
$process=$null
try {
    $process=Start-Process -FilePath $Exe -ArgumentList ('"'+$copy+'"') -WindowStyle Hidden -PassThru
    $script:main=Wait-Window ([uint32]$process.Id) 'Mezozoy' 15000
    [void][NativeSmoke]::ShowWindow($main,3)
    Open-Page $main 6105 (Join-Path $out '00-before.png')
    Click 1119
    $editor=Child 1104; $page=[NativeSmoke]::GetParent($editor); $bar=Child 1190
    $original=[NativeSmoke]::ControlText($editor)
    [void](Send $editor 0xB1 ($original.IndexOf("`n")+1) ($original.IndexOf("`n")+1))
    [void](Send $editor 0x115 6)
    [void][Ui97]::UpdateWindow($editor); [void][Ui97]::UpdateWindow($bar)
    Start-Sleep -Milliseconds 150
    [void][Ui97]::UpdateWindow($editor); [void][Ui97]::UpdateWindow($bar)
    $before=[Ui97]::Scroll($bar); $pages=Send $page 0x8054
    [Ui97]::Bar($bar)
    $image=[Ui97]::Live($bar); $image.Save((Join-Path $out 'scrollbar-live.png')); $maxDiff=0
    try {
        if([Ui97]::ThumbPixels($image) -lt 40) { throw 'Scrollbar is blank or partly erased' }
        foreach($letter in 'Stable scroll while typing text.'.ToCharArray()) {
            [void](Send $editor 0x102 ([int]$letter))
            [void][Ui97]::UpdateWindow($editor); [void][Ui97]::UpdateWindow($bar)
            $now=[Ui97]::Scroll($bar)
            if($now.max -ne $before.max -or $now.page -ne $before.page -or $now.pos -ne $before.pos) { throw 'Typing changed scrollbar range/position on unchanged pages' }
            if(([Ui97]::GetWindowLongPtrW($editor,-16).ToInt64() -band 0x300000) -ne 0) { throw 'RichEdit native scrollbars returned' }
            $frame=[Ui97]::Live($bar)
            try {
                $diff=[Ui97]::Different($image,$frame)
                if($diff -gt $maxDiff) { $frame.Save((Join-Path $out 'scrollbar-changed.png')); "diff=$diff after=$letter $([Ui97]::Bar($bar))" }
                $maxDiff=[Math]::Max($maxDiff,$diff)
            } finally { $frame.Dispose() }
        }
    } finally { $image.Dispose() }
    if($maxDiff -gt 0) { throw "Scrollbar pixels changed during typing: $maxDiff" }
    "PASS: 31 typed characters; native bars absent, view scrollbar range/position stable, live pixel difference=$maxDiff."
    Shot '01-stable-editor'
    [void](Send $bar 0x100 0x23)
    if(([Ui97]::Scroll($bar)).pos -le 0) { throw 'Scrollbar End did not scroll' }
    [void](Send $bar 0x100 0x24)
    if(([Ui97]::Scroll($bar)).pos -ne 0) { throw 'Scrollbar Home did not return to top' }
    'PASS: Native scrollbar keyboard End/Home preserved.'
    Click 1134; Click 1134; Click 1134; Click 1134
    $horizontal=Child 1191
    [void](Send $horizontal 0x100 0x23)
    if(([Ui97]::Scroll($horizontal)).pos -le 0) { throw 'Horizontal scrolling failed at enlarged zoom' }
    Click 1133
    if((Send $page 0x8054) -ne $pages) { throw 'Zoom changed pagination' }
    'PASS: Horizontal scrolling and fit-to-width preserve pagination.'
    Open-Page $main 6103 (Join-Path $out '02-development.png')
    $tree=Child 2101; $dev=[NativeSmoke]::GetParent($tree)
    $project=Send $tree 0x110A 0; $people=Send $tree 0x110A 1 $project
    [void](Send $tree 0x1102 2 $people)
    $person=Send $tree 0x110A 4 $people
    [void](Send $tree 0x110B 9 $person)
    Shot '03-detail-arrow'
    Click 2109
    [void](Child 8202); Click 8201
    'PASS: Aligned detail button opens the character with one click.'
    Select-FirstFolder 7306
    $tree=Child 2101; $dev=[NativeSmoke]::GetParent($tree)
    [void][NativeSmoke]::PostMessage($tree,0x100,[IntPtr]0x71,[IntPtr]0)
    $dialog=Wait-Window ([uint32]$process.Id) 'Переименовать папку' 3000
    $edit=PromptEdit $dialog
    [void](Send $edit 0x102 ([int][char]'Г'))
    [void][NativeSmoke]::SendMessageText($edit,0xC,[IntPtr]0,[Text.StringBuilder]::new('Главные персонажи'))
    if(![Ui97]::IsWindow($dialog)) { throw 'Editing a folder name closed its dialog' }
    Capture-Window $dialog (Join-Path $out '04-rename-dialog.png')
    [void](Send $dialog 0x111 1)
    for($i=0; $i -lt 30 -and [NativeSmoke]::ControlText((Child 2103)) -cne 'Главные персонажи'; $i++) { Start-Sleep -Milliseconds 20 }
    if([NativeSmoke]::ControlText((Child 2103)) -cne 'Главные персонажи') { throw 'Rename did not update folder properties' }
    Shot '05-renamed-folder'
    [void][NativeSmoke]::PostMessage($tree,0x100,[IntPtr]0x71,[IntPtr]0)
    $dialog=Wait-Window ([uint32]$process.Id) 'Переименовать папку' 3000
    $edit=PromptEdit $dialog
    [void][NativeSmoke]::SendMessageText($edit,0xC,[IntPtr]0,[Text.StringBuilder]::new('Отменённое имя'))
    [void](Send $dialog 0x111 2)
    Start-Sleep -Milliseconds 80
    if([NativeSmoke]::ControlText((Child 2103)) -cne 'Главные персонажи') { throw 'Cancel renamed a folder' }
    Select-FirstFolder 7307
    [void][NativeSmoke]::SendMessageText((Child 2103),0xC,[IntPtr]0,[Text.StringBuilder]::new('Места действия'))
    [void](Send $main 0x111 6190)
    [xml]$stored=Get-Content -LiteralPath $copy -Raw -Encoding UTF8
    $names=@($stored.MezozoyProject.Folders.DevFolder | ForEach-Object { $_.Name })
    if('Главные персонажи' -notin $names -or 'Места действия' -notin $names -or 'Отменённое имя' -in $names) { throw "Folder names not saved: $names" }
    'PASS: F2 rename, edit notifications, OK, Cancel, properties rename and saved names for both folder types.'
    Stop-Process -Id $process.Id -Force; $process.WaitForExit()
    $process=Start-Process -FilePath $Exe -ArgumentList ('"'+$copy+'"') -WindowStyle Hidden -PassThru
    $script:main=Wait-Window ([uint32]$process.Id) 'Mezozoy' 15000
    [void][NativeSmoke]::ShowWindow($main,3)
    Open-Page $main 6103 (Join-Path $out '06-reopened.png')
    $tree=Child 2101; $project=Send $tree 0x110A 0; $people=Send $tree 0x110A 1 $project
    [void](Send $tree 0x1102 2 $people)
    $folder=Send $tree 0x110A 4 $people; [void](Send $tree 0x110B 9 $folder)
    if([NativeSmoke]::ControlText((Child 2103)) -cne 'Главные персонажи') { throw 'Character folder rename lost after reopening' }
    $world=Send $tree 0x110A 1 $people; $locations=Send $tree 0x110A 1 $world
    [void](Send $tree 0x1102 2 $locations)
    $folder=Send $tree 0x110A 4 $locations; [void](Send $tree 0x110B 9 $folder)
    if([NativeSmoke]::ControlText((Child 2103)) -cne 'Места действия') { throw 'Location folder rename lost after reopening' }
    Shot '07-location-folder'
    'PASS: Both folder names survive closing and reopening the saved project.'
} finally {
    if($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    $env:MEZOZOY_PROFILE_DIR=$oldProfile
}
