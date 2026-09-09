$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$toolchainCandidates = @()
if ($env:MEZOZOY_TOOLCHAIN) { $toolchainCandidates += $env:MEZOZOY_TOOLCHAIN }
$toolchainCandidates += (Join-Path $root '.toolchain\llvm-mingw-20260616-ucrt-x86_64\bin')
$toolchainCandidates += (Join-Path $root '..\..\..\.toolchain\llvm-mingw-20260616-ucrt-x86_64\bin')
$toolchain = $toolchainCandidates | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'clang++.exe') } | Select-Object -First 1

if ($toolchain) {
    $compiler = Join-Path $toolchain 'clang++.exe'
} else {
    $compilerCommand = Get-Command clang++.exe -ErrorAction SilentlyContinue
    if (-not $compilerCommand) {
        throw 'C++ compiler not found. Install LLVM/MinGW, add clang++.exe to PATH, or set MEZOZOY_TOOLCHAIN.'
    }
    $compiler = $compilerCommand.Source
    $toolchain = Split-Path -Parent $compiler
}

$resourceCompiler = Join-Path $toolchain 'llvm-windres.exe'
if (-not (Test-Path -LiteralPath $resourceCompiler)) { $resourceCompiler = Join-Path $toolchain 'windres.exe' }
if (-not (Test-Path -LiteralPath $resourceCompiler)) { throw "Resource compiler not found beside: $compiler" }

$objects = Join-Path $root 'build'
$output = Join-Path $root 'Mezozoy.exe'
New-Item -ItemType Directory -Force -Path $objects | Out-Null

$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $root 'src') -Recurse -Filter *.cpp -File
$objectFiles = @()
foreach ($source in $sourceFiles) {
    $relative = $source.FullName.Substring((Join-Path $root 'src').Length).TrimStart('\')
    $object = Join-Path $objects (($relative -replace '[\\/]', '_') -replace '\.cpp$', '.o')
    & $compiler -std=c++20 -O2 -Wall -Wextra -Wpedantic -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX `
        -I (Join-Path $root 'src') -c $source.FullName -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $($source.Name)" }
    $objectFiles += $object
}

$resourceObject = Join-Path $objects 'Mezozoy.res.o'
& $resourceCompiler --codepage=65001 -i (Join-Path $root 'Mezozoy.rc') -o $resourceObject -O coff
if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed' }

& $compiler -municode -mwindows -O2 -static -static-libgcc -static-libstdc++ @objectFiles $resourceObject -o $output `
    -lcomctl32 -lcomdlg32 -ld2d1 -ldwrite -ldwmapi -lgdi32 -lgdiplus -lole32 -lshell32 -lshlwapi -luser32 -luxtheme -lwinhttp -lwinspool
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }
$selfTest = Start-Process -FilePath $output -ArgumentList '--self-test' -Wait -PassThru -WindowStyle Hidden
if ($selfTest.ExitCode -ne 0) {
    $report = Join-Path $env:TEMP 'Mezozoy-0.9-self-test.log'
    throw "Self-test failed with exit code $($selfTest.ExitCode). Report: $report"
}
Write-Host "Built: $output"
