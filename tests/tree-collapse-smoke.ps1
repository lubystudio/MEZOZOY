param([Parameter(Mandatory=$true)][string]$Exe)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$sample=Join-Path $root 'examples\Horizon.mzoy'
. (Join-Path $PSScriptRoot 'gui-smoke.ps1') -Exe $Exe -Project $sample -OnlyFunctions -WindowOnly
Add-Type @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public static class TreeCapture {
    struct Rect { public int left, top, right, bottom; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr w,out Rect r);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr w,out Rect r);
    [DllImport("user32.dll")] static extern IntPtr GetWindowDC(IntPtr w);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr w);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr w,IntPtr dc);
    [DllImport("gdi32.dll")] static extern bool BitBlt(IntPtr dst,int x,int y,int width,int height,IntPtr src,int sx,int sy,uint op);
    [DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr w);
    public static Bitmap Live(IntPtr w) { return Capture(w,false); }
    public static Bitmap LiveClient(IntPtr w) { return Capture(w,true); }
    static Bitmap Capture(IntPtr w,bool client) {
        Rect r; if(client) GetClientRect(w,out r); else GetWindowRect(w,out r);
        var image=new Bitmap(r.right-r.left,r.bottom-r.top);
        using(var g=Graphics.FromImage(image)) {
            var target=g.GetHdc(); var source=client?GetDC(w):GetWindowDC(w);
            try { if(!BitBlt(target,0,0,image.Width,image.Height,source,0,0,0x00CC0020)) throw new Exception("Capture failed"); }
            finally { ReleaseDC(w,source); g.ReleaseHdc(target); }
        }
        return image;
    }
    public static int Ink(Bitmap image,int row,int height) {
        int count=0;
        for(int y=row*height+4;y<Math.Min(image.Height,(row+1)*height-4);y++)
            for(int x=50;x<image.Width-45;x++) {
                var c=image.GetPixel(x,y);
                if(c.R>105 && c.G>105 && c.B>105) count++;
            }
        return count;
    }
}
'@ -ReferencedAssemblies ([Drawing.Bitmap].Assembly.Location),([Drawing.Color].Assembly.Location)
$out=Join-Path $root 'test-results\tree-collapse'
$profile=Join-Path $out 'profile'
[void](New-Item -ItemType Directory -Force -Path $out,$profile)
$copy=Join-Path $out 'folders.mzoy'
[xml]$fixture=Get-Content -LiteralPath $sample -Raw -Encoding UTF8
$folders=$fixture.CreateElement('Folders')
[void]$fixture.DocumentElement.AppendChild($folders)
foreach($entry in @(@('1001','character','Главные персонажи'),@('1002','location','Места действия'))) {
    $folder=$fixture.CreateElement('DevFolder')
    $values=[ordered]@{Id=$entry[0];Section=$entry[1];Name=$entry[2];Expanded='true'}
    foreach($key in $values.Keys) { $node=$fixture.CreateElement($key); $node.InnerText=$values[$key]; [void]$folder.AppendChild($node) }
    [void]$folders.AppendChild($folder)
}
foreach($entry in @(@('//Characters/Character[1]','1001'),@('//Locations/*[1]','1002'))) {
    $node=$fixture.CreateElement('FolderId'); $node.InnerText=$entry[1]
    [void]$fixture.SelectSingleNode($entry[0]).AppendChild($node)
}
$fixture.Save($copy)
$oldProfile=$env:MEZOZOY_PROFILE_DIR; $env:MEZOZOY_PROFILE_DIR=$profile
function Send($w,[uint32]$m,[long]$a=0,[long]$b=0) { [NativeSmoke]::SendMessage($w,$m,[IntPtr]$a,[IntPtr]$b).ToInt64() }
function Check-Tree($label) {
    # Update only the existing damage region. PrintWindow/full invalidation would mask this regression.
    Start-Sleep -Milliseconds 80
    [void][TreeCapture]::UpdateWindow($script:tree)
    $image=[TreeCapture]::Live($script:tree)
    try {
        $height=Send $script:tree 0x111C
        $item=Send $script:tree 0x110A 5
        $row=0
        while($item) {
            if([TreeCapture]::Ink($image,$row,$height) -lt 10) {
                $image.Save((Join-Path $out 'failure-live.png'))
                throw "$label erased visible row $row; only partial repaint was requested"
            }
            $row++; $item=Send $script:tree 0x110A 6 $item
        }
        if($row -lt 5) { throw "Missing tree roots after $label" }
        for($empty=$row;$empty -lt [Math]::Min($row+6,[Math]::Floor($image.Height/$height));$empty++) {
            if([TreeCapture]::Ink($image,$empty,$height) -ge 10) {
                $image.Save((Join-Path $out 'failure-live.png'))
                throw "$label left a stale row below the visible tree"
            }
        }
        if((Send $script:tree 0x1105) -ne $script:total) { throw 'Expansion changed the tree data' }
        if($label -eq 'initial') { $image.Save((Join-Path $out '01-initial-tree.png')) }
        if($label -eq 'final') { $image.Save((Join-Path $out '02-collapsed-tree-live.png')) }
    } finally { $image.Dispose() }
}
$process=$null
try {
    $process=Start-Process -FilePath $Exe -ArgumentList ('"'+$copy+'"') -WindowStyle Hidden -PassThru
    $main=Wait-Window ([uint32]$process.Id) 'Mezozoy' 15000
    [void][NativeSmoke]::ShowWindow($main,3)
    Open-Page $main 6103 (Join-Path $out '00-development.png')
    $script:tree=[NativeSmoke]::FindAnyChildById($main,2101)
    if($tree -eq [IntPtr]::Zero) { throw 'Development tree not found' }
    $project=Send $tree 0x110A 0; $people=Send $tree 0x110A 1 $project
    $worlds=Send $tree 0x110A 1 $people; $places=Send $tree 0x110A 1 $worlds
    [void](Send $tree 0x1102 2 $people); [void](Send $tree 0x1102 2 $places)
    $charactersFolder=Send $tree 0x110A 4 $people; $locationsFolder=Send $tree 0x110A 4 $places
    [void](Send $tree 0x110B 9 $charactersFolder)
    $script:total=Send $tree 0x1105
    Check-Tree 'initial'
    for($i=0;$i -lt 25;$i++) {
        foreach($folder in @($charactersFolder,$locationsFolder)) {
            [void](Send $tree 0x1102 1 $folder); Check-Tree "collapse $i"
            [void](Send $tree 0x1102 2 $folder); Check-Tree "expand $i"
        }
    }
    'PASS: 100 folder collapse/expand operations preserve every visible row and the tree data.'
    foreach($section in @($people,$places)) {
        [void](Send $tree 0x1102 1 $section); Check-Tree 'section collapse'
        [void](Send $tree 0x1102 2 $section); Check-Tree 'section expand'
    }
    [void](Send $tree 0x110B 9 $charactersFolder)
    [void](Send $tree 0x100 0x25); [void](Send $tree 0x101 0x25); Check-Tree 'keyboard collapse'
    [void](Send $tree 0x100 0x27); [void](Send $tree 0x101 0x27); Check-Tree 'keyboard expand'
    [void](Send $tree 0x1102 1 $charactersFolder)
    [void](Send $tree 0x1102 1 $locationsFolder)
    Check-Tree 'final'
    $image=[TreeCapture]::LiveClient($main)
    try { $image.Save((Join-Path $out '03-collapsed-development-live.png')) } finally { $image.Dispose() }
    'PASS: Section collapse, keyboard navigation, selection and folder properties remain visible.'
} finally {
    if($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    $env:MEZOZOY_PROFILE_DIR=$oldProfile
}
