#requires -Version 5.1
# Windowless EXE entry point. Automated validation MUST use -PreflightOnly.
[CmdletBinding()]
param([ValidateSet('Meta','SteamVR')][string]$Runtime = 'SteamVR', [switch]$PreflightOnly,
      [ValidateSet('Performance','Balanced','Quality')][string]$MetaRenderPreset)
$ErrorActionPreference = 'Stop'
try {
    $launchScript = Join-Path $PSScriptRoot 'Play-HarleyVR.ps1'
    $launchOptions = @{ Runtime=$Runtime }
    if ($PSBoundParameters.ContainsKey('MetaRenderPreset')) { $launchOptions.MetaRenderPreset=$MetaRenderPreset }
    $check = & $launchScript @launchOptions -PreflightOnly
    if (-not $check.Passed) { throw ($check.Issues -join [Environment]::NewLine) }
    if ($PreflightOnly) {
        [Console]::Out.WriteLine('Package and installed software checks passed. Headset tracking has not been tested.')
        exit 0
    }
    & $launchScript @launchOptions
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
