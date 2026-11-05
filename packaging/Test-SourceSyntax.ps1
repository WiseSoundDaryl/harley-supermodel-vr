#requires -Version 5.1
<#
.SYNOPSIS
Parses every tracked PowerShell script without executing the checked scripts.
.DESCRIPTION
This read-only syntax check does not build, package or launch HarleyVR. Passing
does not establish runtime behavior, legal clearance or release readiness.
#>
[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)
$ErrorActionPreference = 'Stop'

$rootItem = Get-Item -LiteralPath $Root -Force
if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Root must be a real repository directory, not a link.'
}
$checkedRoot = $rootItem.FullName.TrimEnd([IO.Path]::DirectorySeparatorChar)
$git = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source

function Read-Git([string]$Arguments) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $git
    $start.WorkingDirectory = $checkedRoot
    $start.Arguments = $Arguments
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false)
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'Could not start Git for source enumeration.' }
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $output = $outputTask.GetAwaiter().GetResult()
        $errorText = $errorTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Git source enumeration failed: $errorText" }
        return $output
    }
    finally { $process.Dispose() }
}

$gitRoot = (Read-Git 'rev-parse --show-toplevel').Trim()
if (-not [string]::Equals([IO.Path]::GetFullPath($gitRoot), $checkedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Root must be the top-level directory of the checkout being checked.'
}
# NUL separation preserves spaces and other unusual filename characters.
$tracked = (Read-Git 'ls-files -z -- ":(icase)*.ps1"').Split([char[]]@([char]0), [StringSplitOptions]::RemoveEmptyEntries)
if ($tracked.Count -eq 0) { throw 'No tracked PowerShell scripts were found.' }

$failures = New-Object 'System.Collections.Generic.List[string]'
foreach ($relative in $tracked) {
    if ([IO.Path]::IsPathRooted($relative)) { throw 'Git returned an absolute source path.' }
    $parts = $relative -split '[/\\]'
    $path = $checkedRoot
    foreach ($part in $parts) {
        if ([string]::IsNullOrEmpty($part) -or $part -eq '.' -or $part -eq '..') {
            throw "Unsafe source path: $relative"
        }
        $path = Join-Path $path $part
        $item = Get-Item -LiteralPath $path -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Source paths must not use links: $relative"
        }
    }
    if ($item.PSIsContainer) { throw "Expected a PowerShell file: $relative" }
    $tokens = $null
    $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$parseErrors) | Out-Null
    foreach ($parseError in $parseErrors) {
        $failures.Add(('{0}:{1}:{2}: {3}' -f $relative, $parseError.Extent.StartLineNumber,
            $parseError.Extent.StartColumnNumber, $parseError.Message))
    }
}
if ($failures.Count -gt 0) {
    throw ("PowerShell syntax validation failed:`n" + ($failures -join "`n"))
}

[pscustomobject]@{
    Passed = $true
    ScriptsParsed = $tracked.Count
    SyntaxOnly = $true
    CheckedScriptsExecuted = $false
    FilesModified = $false
    ReleaseReady = $false
    Limits = 'Syntax only; not runtime behavior, a clean build, legal clearance or headset validation.'
}
