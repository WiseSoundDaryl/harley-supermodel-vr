# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Msys2Root = 'C:\msys64',
    [switch]$RunTests
)
$ErrorActionPreference = 'Stop'
$toolBin = Join-Path $Msys2Root 'mingw64\bin'
$compiler = Join-Path $toolBin 'g++.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw "Missing MINGW64 C++ compiler in $toolBin." }
$target = [IO.Path]::GetFullPath($OutputDirectory)
$null = [IO.Directory]::CreateDirectory($target)
$output = Join-Path $target 'HarleyVR-Meta-Graphics.exe'
if (Test-Path -LiteralPath $output) { throw 'Choose a fresh output path for the headset graphics executable.' }
$flags = @('-std=c++17','-O2','-s','-static-libgcc','-static-libstdc++','-static','-Wl,--no-insert-timestamp','-Wall','-Wextra','-Wno-missing-field-initializers','-Wno-cast-function-type')
$previousPath = $env:PATH
try {
    $env:PATH = $toolBin + ';' + $env:PATH
    & $compiler @flags '-mwindows' (Join-Path $PSScriptRoot 'MetaGraphics.cpp') '-o' $output '-ld3d11' '-ldxgi' '-luuid' '-lgdi32' '-luser32'
    if ($LASTEXITCODE -ne 0) { throw 'Could not compile the Meta headset graphics picker.' }
    if ($RunTests) {
        $testExe = Join-Path $target 'MetaGraphicsTests.exe'
        if (Test-Path -LiteralPath $testExe) { throw 'A test executable already exists at the selected output path.' }
        & $compiler @flags (Join-Path $PSScriptRoot 'Tests.cpp') '-o' $testExe '-lgdi32' '-luser32'
        if ($LASTEXITCODE -ne 0) { throw 'Could not compile the Meta graphics tests.' }
        & $testExe
        if ($LASTEXITCODE -ne 0) { throw 'Offline Meta graphics tests failed.' }
        $frameExe = Join-Path $target 'MetaGraphicsFrameTests.exe'
        if (Test-Path -LiteralPath $frameExe) { throw 'A frame test executable already exists at the selected output path.' }
        & $compiler @flags (Join-Path $PSScriptRoot 'FrameTests.cpp') '-o' $frameExe '-ld3d11' '-ldxgi' '-luuid' '-lgdi32' '-luser32'
        if ($LASTEXITCODE -ne 0) { throw 'Could not compile the Meta graphics frame tests.' }
        & $frameExe
        if ($LASTEXITCODE -ne 0) { throw 'Offline Meta graphics frame tests failed.' }
    }
} finally {
    $env:PATH = $previousPath
}
