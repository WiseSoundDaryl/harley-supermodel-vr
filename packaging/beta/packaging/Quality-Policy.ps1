# The game applies menu-saved scale over the CLI fallback before creating its eyes.
# Accept only an unambiguous subset of the menu writer's own Global INI format.
# Unknown formats still load normally in the emulator. They keep full-eye drawing.
function Get-HarleyVRQualityVisibility([string]$PreferencesPath) {
    try {
        $item = Get-Item -LiteralPath $PreferencesPath -ErrorAction Stop
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.Length -gt 65536) { return $false }
        $bytes = [IO.File]::ReadAllBytes($item.FullName)
        if ($bytes.Length -gt 65536) { return $false }
        $text = [Text.Encoding]::GetEncoding(20127,[Text.EncoderFallback]::ExceptionFallback,[Text.DecoderFallback]::ExceptionFallback).GetString($bytes)
        if ($text.IndexOf([char]0) -ge 0) { return $false }
        $lines = $text -split '\r?\n'
        $keys = @{}
        $globalSection = $false
        foreach ($line in $lines) {
            if ($line.Length -eq 0 -or $line.StartsWith(';')) { continue }
            if ($line -cmatch '^\[ Global \]$') {
                if ($globalSection) { return $false }
                $globalSection = $true
                continue
            }
            if (-not $globalSection -or $line -cnotmatch '^([A-Za-z0-9_]+) = "([^"\x00-\x1F]*)"$') { return $false }
            $name = $Matches[1]
            if ($keys.ContainsKey($name)) { return $false }
            $keys[$name] = $Matches[2]
            if ($name -ieq 'OpenXRResolutionScale' -and $name -cne 'OpenXRResolutionScale') { return $false }
        }
        return $globalSection -and $keys.ContainsKey('OpenXRResolutionScale') -and $keys['OpenXRResolutionScale'] -cmatch '^1(?:\.0+)?$'
    } catch { return $false }
}
function Get-HarleyVRVisibilityTokens([string[]]$Tokens,[string]$PreferencesPath) {
    if (@($Tokens | Where-Object { $_ -clike '-xr-scale=*' }).Count -ne 1 -or $Tokens -cnotcontains '-xr-scale=0.5' -or
        @($Tokens | Where-Object { $_ -clike '-xr-visibility-mask=*' }).Count -gt 0) { throw 'Unexpected display options for the Quality test.' }
    $quality = Get-HarleyVRQualityVisibility $PreferencesPath
    foreach ($token in $Tokens) {
        if ($quality -and $token -ceq '-xr-scale=0.5') { '-xr-scale=1' } else { $token }
    }
    if ($quality) { '-xr-visibility-mask=1' } else { '-xr-visibility-mask=0' }
}
