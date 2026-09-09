$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$distDir = Join-Path $projectRoot 'dist'
$outputDir = Join-Path $projectRoot 'output'
$isccCandidates = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
)
$iscc = $isccCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 not found. Install it from https://jrsoftware.org/isdl.php' }

& (Join-Path $projectRoot 'build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Mezozoy build failed' }

New-Item -ItemType Directory -Force -Path $distDir,$outputDir | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'Mezozoy.exe') -Destination (Join-Path $distDir 'Mezozoy.exe') -Force
& $iscc (Join-Path $projectRoot 'installer\Mezozoy.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed' }

$setup = Join-Path $outputDir 'Mezozoy-1.0.0-Setup.exe'
$portable = Join-Path $outputDir 'Mezozoy-1.0.0-Portable.zip'
Compress-Archive -Path (Join-Path $distDir 'Mezozoy.exe'),(Join-Path $projectRoot 'README.md'),(Join-Path $projectRoot 'LICENSE') -DestinationPath $portable -Force
Get-FileHash -Algorithm SHA256 -LiteralPath $setup,$portable | Format-Table Path,Hash
