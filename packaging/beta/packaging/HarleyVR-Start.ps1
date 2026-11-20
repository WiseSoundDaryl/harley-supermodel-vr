#requires -Version 5.1
# Windowless EXE entry point. Automated validation MUST use -PreflightOnly.
[CmdletBinding()]
param([ValidateSet('Meta','SteamVR')][string]$Runtime = 'SteamVR', [switch]$PreflightOnly)
$ErrorActionPreference = 'Stop'
try {
    $launchScript = Join-Path $PSScriptRoot 'Play-HarleyVR.ps1'
    $check = & $launchScript -Runtime $Runtime -PreflightOnly
    if (-not $check.Passed) { throw ($check.Issues -join [Environment]::NewLine) }
    if ($PreflightOnly) {
        [Console]::Out.WriteLine('Package and installed software checks passed. Headset tracking has not been tested.')
        exit 0
    }
    & $launchScript -Runtime $Runtime
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
