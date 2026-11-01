#requires -Version 5.1
# USER PLAY ONLY. Automated checks must use -PreflightOnly; never launch with sound.
[CmdletBinding()]
param([ValidateSet('Meta','SteamVR')][string]$Runtime = 'SteamVR', [switch]$PreflightOnly)

$ErrorActionPreference = 'Stop'
$Runtime = $(if ($Runtime -ieq 'Meta') { 'Meta' } else { 'SteamVR' })
$root = [IO.Path]::GetFullPath($PSScriptRoot)
$issues = New-Object 'System.Collections.Generic.List[string]'
$exe = Join-Path $root 'supermodel.exe'
$rom = Join-Path $root 'ROMs\harleyb.zip'
$manifestPath = Join-Path $root 'release-ready.json'
. (Join-Path $root 'HarleyVR-Runtime.ps1')
. (Join-Path $root 'HarleyVR-Meta-ASW.ps1')
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
if ($Runtime -eq 'Meta') { $arguments = $arguments.Replace('-xr-scale=0.5', '-xr-scale=0.75') }

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
        if ($manifest.schema -ne 1 -or $manifest.releaseReady -isnot [bool] -or $manifest.releaseReady -ne $true -or
            $manifest.mode -ne 'regular' -or $manifest.sourceCommit -notmatch '^[a-fA-F0-9]{40}$' -or
            $manifest.correspondingSource -notmatch '^https://[^\s]+$') {
            $issues.Add('Release metadata is incomplete. No game will be launched.')
        }
        if (@($manifest.validatedRoutes) -cnotcontains $Runtime) {
            $issues.Add("This package has not been released for the selected $Runtime route.")
        }
    } catch {
        $issues.Add('Release metadata could not be read. Extract the complete release ZIP again.')
    }
}
$immutable = @(
    'supermodel.exe', 'libopenxr_loader.dll', 'libgcc_s_seh-1.dll',
    'libstdc++-6.dll', 'libwinpthread-1.dll', 'Config\Games.xml', 'Config\Music.xml',
    'Assets\p1crosshair.bmp', 'Assets\p2crosshair.bmp',
    'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'README.md',
    'Play-HarleyVR.cmd', 'Play-HarleyVR-Meta.cmd', 'Play-HarleyVR-SteamVR.cmd',
    'Play-HarleyVR.ps1', 'HarleyVR-Runtime.ps1', 'HarleyVR-Meta-ASW.ps1'
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
$runtimeInfo = $null
try {
    $runtimeInfo = Find-HarleyVRRuntime $Runtime
    $runtimePath = $runtimeInfo.Manifest; $runtimeLibrary = $runtimeInfo.Library
    $runtimeIsSteamVR = $Runtime -eq 'SteamVR'
} catch {
    $issues.Add($_.Exception.Message)
}
$steamRunning = [bool](Get-Process -Name vrserver,vrcompositor -ErrorAction SilentlyContinue)
if ($Runtime -eq 'Meta') {
    $metaProcesses = @(Get-Process -Name OVRServer_x64 -ErrorAction SilentlyContinue)
    $runtimeRunning = $metaProcesses.Count -eq 1
    if (-not $runtimeRunning) { $issues.Add('Open Meta Horizon Link and confirm Rift S and both controllers are ready before using the Meta launcher.') }
    if ($runtimeRunning -and $runtimeInfo) {
        $expectedService = Join-Path (Split-Path -Parent $runtimeLibrary) 'OVRServer_x64.exe'
        if ([string]$metaProcesses[0].Path -ine $expectedService) { $issues.Add('The running Meta service does not match the selected installed runtime. Close other VR software and use the matching Meta installation.') }
    }
    if ($steamRunning) { $issues.Add('Close SteamVR normally before using the direct Meta launcher. It has not been stopped automatically.') }
} else {
    $runtimeRunning = [bool](Get-Process -Name vrserver -ErrorAction SilentlyContinue) -and [bool](Get-Process -Name vrcompositor -ErrorAction SilentlyContinue)
    if (-not $runtimeRunning) { $issues.Add('Open SteamVR and connect the headset and controllers before starting the game.') }
}
$gameRunning = [bool](Get-Process -Name supermodel,candidate -ErrorAction SilentlyContinue)
if ($gameRunning) { $issues.Add('An emulator is already running. Close it normally first; it has not been interrupted.') }

$result = [pscustomobject]@{
    Passed = ($issues.Count -eq 0)
    PackageRoot = $root
    Executable = $exe
    ROM = $rom
    Arguments = $arguments
    ActiveRuntime = $runtimePath
    SelectedRoute = $Runtime
    SystemDefaultRuntimeChanged = $false
    RuntimeLibrary = $runtimeLibrary
    RuntimeIsSteamVR = $runtimeIsSteamVR
    RuntimeProcessesRunning = $runtimeRunning
    GameAlreadyRunning = $gameRunning
    WriteAccessTested = $false
    HeadsetTrackingVerified = $false
    GameLaunched = $false
    RuntimeLoaded = $false
    AswQueriedOrChanged = $false
    Issues = @($issues.ToArray())
}
if ($PreflightOnly) { return $result }
if (-not $result.Passed) { throw ($issues -join [Environment]::NewLine) }

# Only an actual USER launch creates writable folders. Existing files are untouched.
try {
    foreach ($folder in @('Saves', 'NVRAM', 'Diagnostics')) {
        $path = Join-Path $root $folder
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            New-Item -ItemType Directory -Path $path -ErrorAction Stop | Out-Null
        }
    }
    foreach ($directory in @($root, (Join-Path $root 'Config'), (Join-Path $root 'Saves'), (Join-Path $root 'NVRAM'), (Join-Path $root 'Diagnostics'))) {
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
Write-Host "HarleyVR - regular game / $Runtime"
if ($Runtime -eq 'Meta') {
    Write-Host 'A: start. X: coin. Left stick: steer. Right trigger: gas. Left trigger: brake.'
    Write-Host 'Y: view. B: music. Right stick click: recenter. Both grips for 2 seconds: exit.'
} else {
    Write-Host 'Cross: start. Square: coin. Left stick: steer. R2: gas. L2: brake.'
    Write-Host 'Triangle: view. Circle: music. R3: recenter. Both grips for 2 seconds: exit.'
}

# Child-only environment: no global or parent-process runtime changes.
$game = $null; $gameStarted = $false; $asw = $null; $metaMutex = $null; $ownsMutex = $false
$journal = Join-Path $root 'Diagnostics\meta-asw-recovery.json'
try {
    if ($Runtime -eq 'Meta') {
        $metaMutex = New-Object Threading.Mutex($false, 'Local\HarleyVR-RiftS-ASW-Guard')
        try { $ownsMutex = $metaMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
        if (-not $ownsMutex) { throw 'Another HarleyVR Meta launcher is active. Close its game normally first.' }
        $runner = New-MetaAswRunner $runtimeInfo.DiagnosticCLI
        $null = Repair-MetaAswPending -Path $journal -Runner $runner
        $asw = Start-MetaAswGuard -Path $journal -Runner $runner
    }
    $game = New-Object Diagnostics.Process
    $game.StartInfo = New-HarleyVRGameStartInfo $exe $arguments $root $runtimePath
    if (-not $game.Start()) { throw 'The game process did not start.' }
    $gameStarted = $true
    if ($asw) { Set-MetaAswGame $asw $game }
    $game.WaitForExit()
    if ($game.ExitCode -ne 0) { throw "Game exited with code $($game.ExitCode). See Supermodel.log in the extracted game folder." }
} finally {
    # A journal-write failure after starting the game must not restore ASW mid-play.
    try {
        if ($gameStarted -and -not $game.HasExited) { $game.WaitForExit() }
        if ($asw) {
            try { $null = Complete-MetaAswGuard $asw }
            catch { throw "Meta ASW restoration needs attention: $($_.Exception.Message) Recovery is saved in $journal. Restart this same Meta launcher after closing the game to retry safely." }
        }
    } finally {
        if ($game) { $game.Dispose() }
        if ($ownsMutex) { $metaMutex.ReleaseMutex() }
        if ($metaMutex) { $metaMutex.Dispose() }
    }
}
