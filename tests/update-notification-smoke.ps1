param([Parameter(Mandatory=$true)][string]$Exe)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$out=Join-Path $root 'test-results\update100'
$profile=Join-Path $out 'profile'
[void](New-Item -ItemType Directory -Force -Path $out,$profile)
$profileSettings=Join-Path $profile 'settings.json'
if(Test-Path -LiteralPath $profileSettings) { Remove-Item -LiteralPath $profileSettings -Force }
$response=Join-Path $out 'release.json'
[IO.File]::WriteAllText($response,'{"tag_name":"v1.0.1","html_url":"https://github.com/lubystudio/MEZOZOY/releases/tag/v1.0.1"}',[Text.UTF8Encoding]::new($false))
$sample=Join-Path $root 'examples\Horizon.mzoy'
. (Join-Path $PSScriptRoot 'gui-smoke.ps1') -Exe $Exe -Project $sample -OnlyFunctions -WindowOnly
$oldProfile=$env:MEZOZOY_PROFILE_DIR
$oldResponse=$env:MEZOZOY_UPDATE_TEST_RESPONSE
$oldRepository=$env:MEZOZOY_UPDATE_TEST_REPOSITORY
$env:MEZOZOY_PROFILE_DIR=$profile
$env:MEZOZOY_UPDATE_TEST_RESPONSE=$response
$env:MEZOZOY_UPDATE_TEST_REPOSITORY='lubystudio/MEZOZOY'
$process=$null
try {
    $process=Start-Process -FilePath (Resolve-Path $Exe) -PassThru -WindowStyle Hidden
    $main=Wait-Window ([uint32]$process.Id) 'Mezozoy' 10000
    # The top-level HWND exists briefly before buildShell() creates its child
    # controls, especially on the first launch after linking a new executable.
    Start-Sleep -Seconds 15
    $downloadCreated=[NativeSmoke]::FindChildByIdAnyVisibility($main,6199)
    if($downloadCreated -eq [IntPtr]::Zero) { throw 'Application shell did not finish starting' }
    [void][NativeSmoke]::ShowWindow($main,3)
    [void][NativeSmoke]::SetForegroundWindow($main)
    $until=[DateTime]::UtcNow.AddSeconds(8)
    do {
        $download=[NativeSmoke]::FindAnyChildById($main,6199)
        $later=[NativeSmoke]::FindAnyChildById($main,6200)
        if($download -ne [IntPtr]::Zero -and $later -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    } while([DateTime]::UtcNow -lt $until)
    if($download -eq [IntPtr]::Zero -or $later -eq [IntPtr]::Zero) {
        Capture-Window $main (Join-Path $out 'update-banner-failed.png')
        throw ('Update banner did not appear. Download: '+[NativeSmoke]::DescribeChildrenById($main,6199)+' Later: '+[NativeSmoke]::DescribeChildrenById($main,6200))
    }
    if([NativeSmoke]::ControlText($download) -notmatch 'GitHub') { throw 'Download action has the wrong caption' }
    Start-Sleep -Milliseconds 300
    Capture-Window $main (Join-Path $out 'update-banner.png')
    [void][NativeSmoke]::SendMessage($later,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    if([NativeSmoke]::FindAnyChildById($main,6200) -ne [IntPtr]::Zero) { throw 'Later did not dismiss the banner' }
    'PASS: New GitHub release displays a native banner with Download and Later actions; Later dismisses it.'
} finally {
    if($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
    $env:MEZOZOY_PROFILE_DIR=$oldProfile
    $env:MEZOZOY_UPDATE_TEST_RESPONSE=$oldResponse
    $env:MEZOZOY_UPDATE_TEST_REPOSITORY=$oldRepository
}
