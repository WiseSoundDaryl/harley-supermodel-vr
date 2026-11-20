# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$compiler = Join-Path ([Environment]::GetFolderPath('Windows')) 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'The Windows x64 .NET Framework C# compiler is required.' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Choose a new directory for the launcher build.' }
$null = New-Item -ItemType Directory -Path $output
$exe = Join-Path $output 'HarleyVR.exe'
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /debug- /reference:System.Windows.Forms.dll /reference:System.Drawing.dll "/out:$exe" (Join-Path $PSScriptRoot 'HarleyVR.cs')
if ($LASTEXITCODE -ne 0) { throw 'Launcher compilation failed.' }
foreach ($name in 'HarleyVR-Meta.exe','HarleyVR-SteamVR.exe') {
    Copy-Item -LiteralPath $exe -Destination (Join-Path $output $name)
}
