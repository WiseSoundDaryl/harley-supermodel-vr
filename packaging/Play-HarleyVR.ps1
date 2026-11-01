#requires -Version 5.1
# USER PLAY ONLY. Automated checks must use -PreflightOnly; never launch with sound.
[CmdletBinding()]
param([switch]$PreflightOnly)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($PSScriptRoot)
$issues = New-Object 'System.Collections.Generic.List[string]'
$exe = Join-Path $root 'supermodel.exe'
$rom = Join-Path $root 'ROMs\harleyb.zip'
$manifestPath = Join-Path $root 'release-ready.json'
$options = @(
    '-openxr', '-xr-scale=0.5', '-xr-world-scale=16', '-no-xr-mirror',
    '-window', '-res=1280,720', '-new3d', '-wide-screen', '-no-stretch', '-ss=1',
    '-input-system=sdl', '-stereo-mono-preview', '-stereo-ui', '-stereo-menu-comfort',
    '-stereo-theater', '-stereo-theater-size=70', '-stereo-vr-hud', '-stereo-full-bike',
    '-no-stereo-stable-chase', '-stereo-rider-comfort', '-stereo-rider-up=3',
    '-stereo-rider-back=0.75', '-stereo-horizon-fov=90', '-stereo-separation=0',
    '-stereo-cull-bypass', '-true-hz', '-show-fps', '-xr-array-swapchain=0',
    '-xr-scene-interpolation=1', '-xr-late-worker-present=1', '-sound', '-dsb',
    '-sound-volume=100', '-music-volume=100', '-xr-owned-hud-state=1', '-xr-hud-flush=1',
    '-body-interaction=0', '-body-test=0', '-no-stereo-bike-hud'
)
$arguments = '"' + $rom + '" ' + ($options -join ' ')

if (-not [Environment]::Is64BitProcess) {
    $issues.Add('Use the 64-bit Windows launcher. This package is Windows x64 only.')
}
# A source-tree scaffold must never become launchable just by dropping in an EXE.
# Release packaging creates this manifest only after source/licenses and tests pass.
$manifest = $null
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    $issues.Add('This is a source/package draft, not a prepared release. Download a completed Windows release ZIP when available.')
} else {
    try {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if ($manifest.schema -ne 1 -or $manifest.releaseReady -ne $true -or
            $manifest.mode -ne 'regular' -or $manifest.sourceCommit -notmatch '^[a-fA-F0-9]{40}$' -or
            $manifest.correspondingSource -notmatch '^https://[^\s]+$') {
            $issues.Add('Release metadata is incomplete. No game will be launched.')
        }
    } catch {
        $issues.Add('Release metadata could not be read. Extract the complete release ZIP again.')
    }
}
$immutable = @(
    'supermodel.exe', 'libopenxr_loader.dll', 'libgcc_s_seh-1.dll',
    'libstdc++-6.dll', 'libwinpthread-1.dll', 'Config\Games.xml', 'Config\Music.xml',
    'Assets\p1crosshair.bmp', 'Assets\p2crosshair.bmp',
    'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'README.md'
)
foreach ($name in $immutable) {
    $path = Join-Path $root $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $issues.Add("Required package file is missing: $name")
        continue
    }
    if ($null -ne $manifest) {
        $entry = @($manifest.files | Where-Object { $_.path -ceq $name })
        if ($entry.Count -ne 1 -or $entry[0].sha256 -notmatch '^[a-fA-F0-9]{64}$') {
            $issues.Add("Release manifest has no unique hash for $name")
        } elseif ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry[0].sha256) {
            $issues.Add("Package file does not match this release: $name")
        }
    }
}
# Editable user settings are intentionally not hash-locked after installation.
if (-not (Test-Path -LiteralPath (Join-Path $root 'Config\Supermodel.ini') -PathType Leaf)) {
    $issues.Add('Default configuration is missing: Config\Supermodel.ini')
}
if (-not (Test-Path -LiteralPath $rom -PathType Leaf)) {
    $issues.Add('Add your own legally obtained Revision B ROM set as ROMs\harleyb.zip. ROM files are not supplied.')
}

$runtimePath = $null
$runtimeLibrary = $null
$runtimeIsSteamVR = $false
try {
    $key = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -Name ActiveRuntime
    $runtimePath = [Environment]::ExpandEnvironmentVariables([string]$key.ActiveRuntime)
    if (-not [IO.Path]::IsPathRooted($runtimePath) -or
        -not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
        throw 'The registered OpenXR runtime file is missing.'
    }
    $runtime = Get-Content -LiteralPath $runtimePath -Raw | ConvertFrom-Json
    $library = [Environment]::ExpandEnvironmentVariables([string]$runtime.runtime.library_path)
    if ([string]::IsNullOrWhiteSpace($library)) { throw 'Runtime has no library_path.' }
    if ([IO.Path]::IsPathRooted($library)) { $runtimeLibrary = [IO.Path]::GetFullPath($library) }
    else { $runtimeLibrary = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $runtimePath) $library)) }
    if (-not (Test-Path -LiteralPath $runtimeLibrary -PathType Leaf)) {
        throw 'The registered OpenXR runtime library is missing.'
    }
    $runtimeIsSteamVR = ($runtime.runtime.VALVE_runtime_is_steamvr -eq $true) -or
        ([string]$runtime.runtime.name -eq 'SteamVR')
    if (-not $runtimeIsSteamVR) {
        $issues.Add('This first release targets SteamVR. Set SteamVR as the active OpenXR runtime in SteamVR settings, then try again.')
    }
} catch {
    $issues.Add('A valid active OpenXR runtime was not found. Open SteamVR, make it the active OpenXR runtime, and try again.')
}
$runtimeRunning = [bool](Get-Process -Name vrserver -ErrorAction SilentlyContinue) -and
    [bool](Get-Process -Name vrcompositor -ErrorAction SilentlyContinue)
if (-not $runtimeRunning) { $issues.Add('Open SteamVR and connect the headset and controllers before starting the game.') }
$gameRunning = [bool](Get-Process -Name supermodel,candidate -ErrorAction SilentlyContinue)
if ($gameRunning) { $issues.Add('An emulator is already running. Close it normally first; it has not been interrupted.') }

$result = [pscustomobject]@{
    Passed = ($issues.Count -eq 0)
    PackageRoot = $root
    Executable = $exe
    ROM = $rom
    Arguments = $arguments
    ActiveRuntime = $runtimePath
    RuntimeLibrary = $runtimeLibrary
    RuntimeIsSteamVR = $runtimeIsSteamVR
    RuntimeProcessesRunning = $runtimeRunning
    GameAlreadyRunning = $gameRunning
    WriteAccessTested = $false
    HeadsetTrackingVerified = $false
    GameLaunched = $false
    RuntimeLoaded = $false
    Issues = @($issues.ToArray())
}
if ($PreflightOnly) { return $result }
if (-not $result.Passed) { throw ($issues -join [Environment]::NewLine) }

# Only an actual USER launch creates writable folders. Existing files are untouched.
try {
    foreach ($folder in @('Saves', 'NVRAM')) {
        $path = Join-Path $root $folder
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            New-Item -ItemType Directory -Path $path -ErrorAction Stop | Out-Null
        }
    }
    foreach ($directory in @($root, (Join-Path $root 'Saves'), (Join-Path $root 'NVRAM'))) {
        $probe = Join-Path $directory ('.harleyvr-write-' + [guid]::NewGuid().ToString('N') + '.tmp')
        $stream = $null
        try {
            $stream = [IO.File]::Open($probe, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        } finally {
            if ($null -ne $stream) {
                $stream.Dispose()
                Remove-Item -LiteralPath $probe -ErrorAction Stop
            }
        }
    }
} catch {
    throw 'The game folder is not writable. Extract the ZIP into a folder you own, such as Documents\HarleyVR, then try again. Do not run it directly inside the ZIP.'
}
Write-Host 'HarleyVR - regular game'
Write-Host 'Cross: start. Square: coin. Left stick: steer. R2: gas. L2: brake.'
Write-Host 'Triangle: view. Circle: music. R3: recenter. Both grips for 2 seconds: exit.'

# Select only this invocation's registered real runtime. Do not edit system settings.
# Remove inherited development traces and synthetic-runtime selectors for the child.
$previous = @{}
foreach ($entry in Get-ChildItem Env:) {
    if ($entry.Name -like 'SMVR_*' -or $entry.Name -like 'XR_*') {
        $previous[$entry.Name] = $entry.Value
    }
}
try {
    foreach ($name in @($previous.Keys)) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    $env:XR_RUNTIME_JSON = $runtimePath
    $env:SMVR_LIVE_TRACE_RENDER_STAGES = '0'
    $game = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
    $game.WaitForExit()
    if ($game.ExitCode -ne 0) { throw "Game exited with code $($game.ExitCode). See Supermodel.log in the extracted game folder." }
} finally {
    [Environment]::SetEnvironmentVariable('XR_RUNTIME_JSON', $null, 'Process')
    [Environment]::SetEnvironmentVariable('SMVR_LIVE_TRACE_RENDER_STAGES', $null, 'Process')
    foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
