#requires -Version 5.1
# The headset and desktop menus share this Meta-only startup preference.
function Get-HarleyVRMetaRenderPreset([string]$Path) {
    try {
        $file = [IO.FileInfo]::new($Path)
        if ($file.Exists -and $file.Length -le 128) {
            $lines = [IO.File]::ReadAllLines($Path)
            if ($lines.Length -eq 1 -and $lines[0] -cmatch '^RenderPreset=(Performance|Balanced|Quality)$') { return $Matches[1] }
        }
    } catch [IO.IOException] {} catch [UnauthorizedAccessException] {}
    return 'Balanced'
}

function Set-HarleyVRMetaRenderPreset([string]$Path, [ValidateSet('Performance','Balanced','Quality')][string]$Preset) {
    $temporary = "$Path.$([guid]::NewGuid().ToString('N')).tmp"
    try {
        [IO.File]::WriteAllText($temporary, "RenderPreset=$Preset`r`n", [Text.Encoding]::ASCII)
        if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temporary, $Path, [System.Management.Automation.Language.NullString]::Value) }
        else { [IO.File]::Move($temporary, $Path) }
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}

function Invoke-HarleyVRMetaGraphicsPicker {
    param([string]$Executable, [string]$RuntimeManifest, [string]$WorkingDirectory,
          [ValidateSet('Performance','Balanced','Quality')][string]$Preset)

    $picker = New-Object Diagnostics.Process
    try {
        $picker.StartInfo = New-HarleyVRGameStartInfo $Executable "--preset $Preset" $WorkingDirectory $RuntimeManifest
        $picker.StartInfo.RedirectStandardError = $true
        if (-not $picker.Start()) { throw 'The headset graphics menu could not start.' }
        $detail = $picker.StandardError.ReadToEnd().Trim()
        $picker.WaitForExit()
        switch ($picker.ExitCode) {
            0   { return $null }
            50  { return 'Performance' }
            75  { return 'Balanced' }
            100 { return 'Quality' }
            default {
                if (-not $detail) { $detail = "Exit code $($picker.ExitCode)." }
                throw "The headset graphics menu could not finish. $detail You can also select Meta graphics on this PC in HarleyVR.exe."
            }
        }
    } finally { $picker.Dispose() }
}
