#requires -Version 5.1
# Read-only installed-runtime discovery. Dot-sourcing performs no discovery or launch.
function Test-HarleyVRX64PE([string]$Path) {
    $stream = $null; $reader = $null
    try {
        $stream = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
        $reader = New-Object IO.BinaryReader($stream)
        if ($stream.Length -lt 128 -or $reader.ReadUInt16() -ne 0x5A4D) { return $false }
        $stream.Position = 0x3c; $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt ($stream.Length - 26)) { return $false }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { return $false }
        $stream.Position = $offset + 24
        return $reader.ReadUInt16() -eq 0x20b
    } catch { return $false } finally {
        if ($reader) { $reader.Dispose() } elseif ($stream) { $stream.Dispose() }
    }
}
function Read-HarleyVRRuntime([string]$Path, [ValidateSet('Meta','SteamVR')][string]$Route) {
    $Path = [Environment]::ExpandEnvironmentVariables($Path)
    if (-not [IO.Path]::IsPathRooted($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'Runtime manifest is missing or not absolute.' }
    $Path = [IO.Path]::GetFullPath($Path)
    $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($manifest.file_format_version -notmatch '^1\.0\.' -or -not $manifest.runtime) { throw 'Invalid OpenXR runtime manifest.' }
    $library = [Environment]::ExpandEnvironmentVariables([string]$manifest.runtime.library_path)
    if ([string]::IsNullOrWhiteSpace($library)) { throw 'Runtime library_path is missing.' }
    if (-not [IO.Path]::IsPathRooted($library)) { $library = Join-Path (Split-Path -Parent $Path) $library }
    $library = [IO.Path]::GetFullPath($library)
    $fileName = [IO.Path]::GetFileName($library)
    if ($Route -eq 'Meta') {
        if ($manifest.runtime.name -cne 'Oculus OpenXR' -or $fileName -ine 'LibOVRRTImpl64_1.dll') { throw 'Manifest is not the Meta x64 OpenXR runtime.' }
    } elseif ($manifest.runtime.name -cne 'SteamVR' -or $manifest.runtime.VALVE_runtime_is_steamvr -ne $true -or $fileName -ine 'vrclient_x64.dll') {
        throw 'Manifest is not the SteamVR x64 OpenXR runtime.'
    }
    if (-not (Test-HarleyVRX64PE $library)) { throw 'Matching runtime x64 DLL is missing or invalid.' }
    $diagnostic = $null
    if ($Route -eq 'Meta') {
        $diagnostic = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $library) '..\oculus-diagnostics\OculusDebugToolCLI.exe'))
        if (-not (Test-Path -LiteralPath $diagnostic -PathType Leaf)) { throw 'Installed Meta diagnostic command tool is missing.' }
    }
    [pscustomobject]@{Route=$Route;Manifest=$Path;Library=$library;DiagnosticCLI=$diagnostic}
}
function Get-HarleyVRRuntimeCandidates([ValidateSet('Meta','SteamVR')][string]$Route) {
    $paths = New-Object 'System.Collections.Generic.List[string]'
    if ($env:XR_RUNTIME_JSON) { $paths.Add($env:XR_RUNTIME_JSON) }
    try { $active = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -Name ActiveRuntime -ErrorAction Stop; if ($active.ActiveRuntime) { $paths.Add([string]$active.ActiveRuntime) } } catch {}
    if ($Route -eq 'Meta') {
        $bases = New-Object 'System.Collections.Generic.List[string]'
        if ($env:OculusBase) { $bases.Add($env:OculusBase) }
        foreach ($key in @('HKLM:\SOFTWARE\Oculus VR, LLC\Oculus','HKLM:\SOFTWARE\WOW6432Node\Oculus VR, LLC\Oculus')) {
            try { $data = Get-ItemProperty -LiteralPath $key -ErrorAction Stop; if ($data.Base) { $bases.Add([string]$data.Base) } } catch {}
        }
        if ($env:ProgramFiles) { $bases.Add((Join-Path $env:ProgramFiles 'Oculus')) }
        foreach ($base in $bases) { $paths.Add((Join-Path $base 'Support\oculus-runtime\oculus_openxr_64.json')) }
    } else {
        if ($env:VR_OVERRIDE) { $paths.Add((Join-Path $env:VR_OVERRIDE 'steamxr_win64.json')) }
        if ($env:LOCALAPPDATA) {
            try {
                $openvr = Get-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath') -Raw -ErrorAction Stop | ConvertFrom-Json
                foreach ($base in @($openvr.runtime)) { if ($base) { $paths.Add((Join-Path $base 'steamxr_win64.json')) } }
            } catch {}
        }
        foreach ($key in @('HKCU:\SOFTWARE\Valve\Steam','HKLM:\SOFTWARE\WOW6432Node\Valve\Steam','HKLM:\SOFTWARE\Valve\Steam')) {
            try {
                $data = Get-ItemProperty -LiteralPath $key -ErrorAction Stop
                foreach ($base in @($data.SteamPath,$data.InstallPath)) { if ($base) { $paths.Add((Join-Path $base 'steamapps\common\SteamVR\steamxr_win64.json')) } }
            } catch {}
        }
    }
    @($paths | Select-Object -Unique)
}
function Find-HarleyVRRuntime([ValidateSet('Meta','SteamVR')][string]$Route, [string[]]$Candidates) {
    if (-not $PSBoundParameters.ContainsKey('Candidates')) { $Candidates = @(Get-HarleyVRRuntimeCandidates $Route) }
    foreach ($candidate in $Candidates) {
        try { return Read-HarleyVRRuntime $candidate $Route } catch { continue }
    }
    throw "No valid installed $Route x64 OpenXR runtime was found. Complete the headset software installation; the system default was not changed."
}
function New-HarleyVRGameStartInfo([string]$Executable, [string]$Arguments, [string]$WorkingDirectory, [string]$RuntimeManifest) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$Executable; $info.Arguments=$Arguments; $info.WorkingDirectory=$WorkingDirectory
    $info.UseShellExecute=$false; $info.CreateNoWindow=$true; $info.WindowStyle='Hidden'
    foreach ($name in @($info.EnvironmentVariables.Keys)) {
        if ($name -like 'XR_*' -or $name -like 'SMVR_*' -or $name -eq 'VR_OVERRIDE') { $info.EnvironmentVariables.Remove($name) }
    }
    $info.EnvironmentVariables['XR_RUNTIME_JSON']=$RuntimeManifest
    $info.EnvironmentVariables['SMVR_LIVE_TRACE_RENDER_STAGES']='0'
    return $info
}
