#requires -Version 5.1
# USER PLAY ONLY. Automated checks must use -PreflightOnly; never launch with sound.
[CmdletBinding()]
param([ValidateSet('Meta','SteamVR')][string]$Runtime = 'SteamVR', [switch]$PreflightOnly)

$ErrorActionPreference = 'Stop'
# A shortcut started by a PowerShell 7 host can pass its module path into Windows
# PowerShell 5.1. Use the built-in 5.1 utility module for package hash checks.
if ($PSVersionTable.PSEdition -eq 'Desktop') {
    Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
}
$Runtime = $(if ($Runtime -ieq 'Meta') { 'Meta' } else { 'SteamVR' })
$root = [IO.Path]::GetFullPath($PSScriptRoot)
$issues = New-Object 'System.Collections.Generic.List[string]'
$bin = "bin\$Runtime"
$exe = Join-Path $root "$bin\supermodel.exe"
$rom = Join-Path $root 'ROMs\harleyb.zip'
$manifestPath = Join-Path $root 'release-ready.json'
. (Join-Path $root 'HarleyVR-Runtime.ps1')
. (Join-Path $root 'HarleyVR-Meta-ASW.ps1')
. (Join-Path $root 'HarleyVR-SessionModes.ps1')
. (Join-Path $root 'HarleyVR-LaunchOptions.ps1')
. (Join-Path $root 'Quality-Policy.ps1')
$preferencesPath = Join-Path $root 'Config\HarleyVRMenu.ini'
[string[]]$tokens = @($rom) + @(Get-HarleyVRLaunchOptions $Runtime)
[string[]]$restartTokens = @($rom) + @(Get-HarleyVRLaunchOptions $Runtime -AfterStartup)
$initialTokens = if ($Runtime -eq 'SteamVR') { @(Get-HarleyVRVisibilityTokens $tokens $preferencesPath) } else { $tokens }
$arguments = ConvertTo-HarleyVRCommandLine $initialTokens

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
        $source = @($manifest.sources | Where-Object { $_.route -ceq $Runtime })
        $launcherSource = @($manifest.sources | Where-Object { $_.route -ceq 'Launchers' })
        $urlBase = 'https://github.com/WiseSoundDaryl/harley-supermodel-vr/releases/download/'
        $gameSourceName = if ($Runtime -eq 'SteamVR') {'HarleyVR-PSVR2-source.zip'} else {'HarleyVR-RiftS-source.zip'}
        if ($manifest.schema -ne 2 -or $manifest.sessionContract -ne 2 -or $manifest.releaseReady -isnot [bool] -or $manifest.releaseReady -ne $true -or
            $manifest.mode -ne 'regular' -or $manifest.tag -notmatch '^v[0-9][0-9A-Za-z.-]*$' -or $source.Count -ne 1 -or
            $source[0].sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $source[0].url -cne ($urlBase+$manifest.tag+'/'+$gameSourceName) -or
            $launcherSource.Count -ne 1 -or $launcherSource[0].sha256 -notmatch '^[a-fA-F0-9]{64}$' -or
            $launcherSource[0].url -cne ($urlBase+$manifest.tag+'/HarleyVR-Launchers-source.zip')) {
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
    "$bin\supermodel.exe", "$bin\libopenxr_loader.dll", "$bin\libgcc_s_seh-1.dll",
    "$bin\libstdc++-6.dll", "$bin\libwinpthread-1.dll", 'Config\Games.xml', 'Config\Music.xml',
    'Assets\p1crosshair.bmp', 'Assets\p2crosshair.bmp',
    'LICENSE.txt', 'Docs\THIRD-PARTY-NOTICES.md', 'Docs\licenses\SHA256.json','README.md',
    "Play-HarleyVR-$Runtime.cmd",
    'Play-HarleyVR.ps1', 'HarleyVR-Runtime.ps1', 'HarleyVR-Meta-ASW.ps1',
    'HarleyVR-SessionModes.ps1', 'HarleyVR-LaunchOptions.ps1', 'HarleyVR-Start.ps1','Quality-Policy.ps1',
    "HarleyVR-$Runtime.exe"
)
try {
    $licenseManifest = Get-Content -LiteralPath (Join-Path $root 'Docs\licenses\SHA256.json') -Raw | ConvertFrom-Json
    foreach ($leaf in $licenseManifest.RequiredFiles) {
        if ($leaf -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]+\.(txt|json)$') { throw 'Unexpected notice filename' }
        $immutable += "Docs\licenses\$leaf"
    }
} catch { $issues.Add('The license directory is incomplete. Extract the complete player package again.') }
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
$packageReady = $issues.Count -eq 0

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
    PackageReady = $packageReady
    PackageRoot = $root
    Executable = $exe
    ROM = $rom
    Arguments = $arguments
    StartupMenu = ($Runtime -eq 'SteamVR')
    MaximumStartupSettingsRestarts = [int]($Runtime -eq 'SteamVR')
    SessionContract = 2
    SupportedRestartCodes = @(if ($Runtime -eq 'SteamVR') {75,76,77,78,79,80})
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
$asw = $null; $metaMutex = $null; $ownsMutex = $false
$sessionMutex = $null; $ownsSessionMutex = $false
$journal = Join-Path $root 'Diagnostics\meta-asw-recovery.json'
try {
    $sessionMutex = New-Object Threading.Mutex($false, 'Local\HarleyVR-Game-Session')
    try { $ownsSessionMutex = $sessionMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsSessionMutex = $true }
    if (-not $ownsSessionMutex) { throw 'Another HarleyVR launcher is active. Close its game normally first.' }
    if (Get-Process -Name supermodel,candidate -ErrorAction SilentlyContinue) { throw 'A game is already running. It has not been interrupted.' }
    if ($Runtime -eq 'Meta') {
        $metaMutex = New-Object Threading.Mutex($false, 'Local\HarleyVR-RiftS-ASW-Guard')
        try { $ownsMutex = $metaMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
        if (-not $ownsMutex) { throw 'Another HarleyVR Meta launcher is active. Close its game normally first.' }
        $runner = New-MetaAswRunner $runtimeInfo.DiagnosticCLI
        $null = Repair-MetaAswPending -Path $journal -Runner $runner
        $asw = Start-MetaAswGuard -Path $journal -Runner $runner
    }
    $makeChild = {
        param([string[]]$childTokens)
        $child = New-Object Diagnostics.Process
        if ($Runtime -eq 'SteamVR') {
            $childTokens = @(Get-HarleyVRVisibilityTokens $childTokens $preferencesPath)
        }
        $child.StartInfo = New-HarleyVRGameStartInfo $exe (ConvertTo-HarleyVRCommandLine $childTokens) $root $runtimePath
        $child.StartInfo.EnvironmentVariables.Remove('SMVR_HEADSET_GPU_TIMING')
        return $child
    }
    if ($Runtime -eq 'SteamVR') {
        $null = Invoke-HarleyVRModeSession -Arguments $tokens -RestartArguments $restartTokens -ProcessFactory $makeChild
    } else {
        $child = & $makeChild $tokens
        $started = $false
        try {
            $started = $child.Start()
            if (-not $started) { throw 'The game process did not start.' }
            Set-MetaAswGame $asw $child
            $child.WaitForExit()
            if ($child.ExitCode -ne 0) { throw "The game exited with code $($child.ExitCode). See Supermodel.log." }
        } finally {
            try { if ($started -and -not $child.HasExited) { $child.WaitForExit() } }
            finally { $child.Dispose() }
        }
    }
} finally {
    # The session helper waits for each actual child even if journaling fails.
    # One startup-settings restart stays inside this same ASW guard and mutex.
    try {
        if ($asw) {
            try { $null = Complete-MetaAswGuard $asw }
            catch { throw "Meta ASW restoration needs attention: $($_.Exception.Message) Recovery is saved in $journal. Restart this same Meta launcher after closing the game to retry safely." }
        }
    } finally {
        if ($ownsMutex) { $metaMutex.ReleaseMutex() }
        if ($metaMutex) { $metaMutex.Dispose() }
        if ($ownsSessionMutex) { $sessionMutex.ReleaseMutex() }
        if ($sessionMutex) { $sessionMutex.Dispose() }
    }
}
