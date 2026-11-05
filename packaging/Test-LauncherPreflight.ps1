#requires -Version 5.1
<#
.SYNOPSIS
Runs the public launcher preflight against disposable, synthetic packages.
.DESCRIPTION
No private reference manifest, ROM, executable, installed runtime or headset is
required. The launcher is copied byte-for-byte and invoked with -PreflightOnly.
Only its copied dependencies receive mock discovery/process functions and
fail-closed action sentinels. Package payloads are inert text, not runnable code
or game data. A passing synthetic fixture is not a validated player package.
#>
[CmdletBinding()]
param([string]$ScratchRoot = [IO.Path]::GetTempPath())
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$checks = 0
$cases = New-Object 'System.Collections.Generic.List[object]'
$sourceRoot = $PSScriptRoot
$sourceNames = @('Play-HarleyVR.ps1','HarleyVR-Runtime.ps1','HarleyVR-Meta-ASW.ps1',
    'Play-HarleyVR.cmd','Play-HarleyVR-Meta.cmd','Play-HarleyVR-SteamVR.cmd')
$sourceBytes = @{}
$sourceHashes = @{}
foreach ($name in $sourceNames) {
    $path = Join-Path $sourceRoot $name
    $sourceBytes[$name] = [IO.File]::ReadAllBytes($path)
    $sourceHashes[$name] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}
# Public launch contract. Deliberate option changes require an explicit test update.
# This is not a comparison against an unavailable private executable/build.
$expectedOptions = @(
    '-openxr','-xr-scale=0.5','-xr-world-scale=16','-no-xr-mirror',
    '-window','-res=1280,720','-new3d','-wide-screen','-no-stretch','-ss=1',
    '-input-system=sdl','-stereo-mono-preview','-stereo-ui','-stereo-menu-comfort',
    '-stereo-theater','-stereo-theater-size=70','-stereo-vr-hud','-stereo-full-bike',
    '-no-stereo-stable-chase','-stereo-rider-comfort','-stereo-rider-up=3',
    '-stereo-rider-back=0.75','-stereo-horizon-fov=90','-stereo-separation=0',
    '-stereo-cull-bypass','-true-hz','-show-fps','-xr-array-swapchain=0',
    '-xr-scene-interpolation=1','-xr-late-worker-present=1','-sound','-dsb',
    '-sound-volume=100','-music-volume=100','-xr-owned-hud-state=1','-xr-hud-flush=1',
    '-body-interaction=0','-body-test=0','-no-stereo-bike-hud'
)
$immutable = @('supermodel.exe','libopenxr_loader.dll','libgcc_s_seh-1.dll',
    'libstdc++-6.dll','libwinpthread-1.dll','Config\Games.xml','Config\Music.xml',
    'Assets\p1crosshair.bmp','Assets\p2crosshair.bmp','LICENSE.txt',
    'THIRD-PARTY-NOTICES.txt','README.md','Play-HarleyVR.cmd',
    'Play-HarleyVR-Meta.cmd','Play-HarleyVR-SteamVR.cmd','Play-HarleyVR.ps1',
    'HarleyVR-Runtime.ps1','HarleyVR-Meta-ASW.ps1')

function Assert-Preflight([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}
function Assert-NoLinks([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    while ($null -ne $item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Test paths must not contain links.' }
        $item = if ($item -is [IO.DirectoryInfo]) { $item.Parent } else { $item.Directory }
    }
}
$scratch = [IO.Path]::GetFullPath((Get-Item -LiteralPath $ScratchRoot -Force).FullName)
Assert-NoLinks $scratch
$fixture = Join-Path $scratch ('HarleyVR Preflight ' + [guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $fixture) { throw 'Refusing to reuse a test directory.' }
$fixtureFull = [IO.Path]::GetFullPath($fixture)
$fixturePrefix = $fixtureFull + [IO.Path]::DirectorySeparatorChar

function Fixture-Path([string]$Relative) {
    $path = [IO.Path]::GetFullPath((Join-Path $fixtureFull $Relative))
    if (-not $path.StartsWith($fixturePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Fixture path escaped its unique test directory.'
    }
    return $path
}
function Remove-OwnedFixture {
    if (-not (Test-Path -LiteralPath $fixtureFull)) { return }
    $resolved = (Resolve-Path -LiteralPath $fixtureFull).ProviderPath
    if (-not [string]::Equals($resolved, $fixtureFull, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetDirectoryName($resolved) -ine $scratch.TrimEnd('\','/') -or
        [IO.Path]::GetFileName($resolved) -notmatch '^HarleyVR Preflight [a-f0-9]{32}$') {
        throw 'Refusing cleanup outside the uniquely owned fixture.'
    }
    Assert-NoLinks $resolved
    # Check descendants without traversing junctions before any recursive removal.
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($resolved)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Fixture contains a link; cleanup refused.' }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
function Write-Fixture([string]$Relative, [string]$Text) {
    $path = Fixture-Path $Relative
    $parent = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent | Out-Null }
    [IO.File]::WriteAllText($path, $Text, (New-Object Text.UTF8Encoding($false)))
}
function Snapshot-Fixture {
    $rows = @(Get-ChildItem -LiteralPath $fixtureFull -Recurse -Force | Sort-Object FullName | ForEach-Object {
        if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unexpected fixture link.' }
        $relative = $_.FullName.Substring($fixturePrefix.Length)
        if ($_.PSIsContainer) { 'D|' + $relative }
        else { 'F|' + $relative + '|' + $_.Length + '|' + $_.LastWriteTimeUtc.Ticks + '|' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    return ($rows -join "`n")
}
function Snapshot-Environment {
    return (@(Get-ChildItem Env: | Sort-Object Name | ForEach-Object {
        $_.Name + '=' + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($_.Value))
    }) -join "`n")
}

# These adapters are appended ONLY to disposable dependency copies. The actual
# launcher remains byte-identical. Installed discovery and all action boundaries
# are replaced; the two public production dependency files remain untouched.
$runtimeMocks = @'
function Block-HarleyVRPreflightAction([string]$Action) {
    $HarleyVRPreflightTestContext.Blocked.Add($Action)
    throw ('HARLEYVR_TEST_BLOCKED:' + $Action)
}
function Find-HarleyVRRuntime([string]$Route) {
    $HarleyVRPreflightTestContext.Discovery.Add($Route)
    if (-not $HarleyVRPreflightTestContext.RuntimeAvailable) { throw 'Synthetic runtime discovery unavailable.' }
    return $HarleyVRPreflightTestContext.RuntimeInfo[$Route]
}
function Get-Process {
    [CmdletBinding()] param([string[]]$Name)
    foreach ($entry in $Name) {
        $HarleyVRPreflightTestContext.ProcessQueries.Add($entry)
        if ($entry -cnotin @('vrserver','vrcompositor','OVRServer_x64','supermodel','candidate')) {
            Block-HarleyVRPreflightAction 'unmocked process query'
        }
        foreach ($process in $HarleyVRPreflightTestContext.Processes) {
            if ($process.Name -ceq $entry) { $process }
        }
    }
}
function New-Object {
    [CmdletBinding()] param([Parameter(Position=0)][string]$TypeName)
    if ($TypeName -cne 'System.Collections.Generic.List[string]') {
        Block-HarleyVRPreflightAction ('object construction: ' + $TypeName)
    }
    Microsoft.PowerShell.Utility\New-Object -TypeName $TypeName
}
function Get-HarleyVRRuntimeCandidates { Block-HarleyVRPreflightAction 'installed runtime discovery' }
function New-HarleyVRGameStartInfo { Block-HarleyVRPreflightAction 'game start info' }
function Get-ItemProperty { Block-HarleyVRPreflightAction 'registry query' }
function Set-ItemProperty { Block-HarleyVRPreflightAction 'registry mutation' }
function Start-Process { Block-HarleyVRPreflightAction 'process launch' }
function Stop-Process { Block-HarleyVRPreflightAction 'process stop' }
function Start-Service { Block-HarleyVRPreflightAction 'service start' }
function Stop-Service { Block-HarleyVRPreflightAction 'service stop' }
function Add-Type { Block-HarleyVRPreflightAction 'native/type loading' }
'@
$aswMocks = @'
function Get-MetaServiceIdentity { Block-HarleyVRPreflightAction 'Meta service identity' }
function Test-MetaProcessIdentity { Block-HarleyVRPreflightAction 'Meta process identity' }
function Invoke-MetaAswCLI { Block-HarleyVRPreflightAction 'Meta diagnostic command' }
function Invoke-MetaAswChecked { Block-HarleyVRPreflightAction 'checked Meta command' }
function Get-MetaAswMode { Block-HarleyVRPreflightAction 'ASW mode query' }
function Save-MetaAswJournal { Block-HarleyVRPreflightAction 'ASW journal write' }
function New-MetaAswRunner { Block-HarleyVRPreflightAction 'Meta command runner' }
function Start-MetaAswGuard { Block-HarleyVRPreflightAction 'ASW start' }
function Repair-MetaAswPending { Block-HarleyVRPreflightAction 'ASW recovery' }
function Complete-MetaAswGuard { Block-HarleyVRPreflightAction 'ASW completion' }
function Set-MetaAswGame { Block-HarleyVRPreflightAction 'ASW game registration' }
'@

function Reset-Fixture([switch]$Scaffold) {
    Remove-OwnedFixture
    New-Item -ItemType Directory -Path $fixtureFull | Out-Null
    foreach ($name in $sourceNames) { [IO.File]::WriteAllBytes((Fixture-Path $name), $sourceBytes[$name]) }
    foreach ($adapter in @(@('HarleyVR-Runtime.ps1',$runtimeMocks),@('HarleyVR-Meta-ASW.ps1',$aswMocks))) {
        [IO.File]::AppendAllText((Fixture-Path $adapter[0]), "`n" + $adapter[1], (New-Object Text.UTF8Encoding($false)))
    }
    Assert-Preflight ((Get-FileHash -LiteralPath (Fixture-Path 'Play-HarleyVR.ps1') -Algorithm SHA256).Hash -ceq $sourceHashes['Play-HarleyVR.ps1']) 'Launcher fixture must be byte-identical to public source.'
    if ($Scaffold) { return }
    foreach ($name in $immutable) {
        if (-not (Test-Path -LiteralPath (Fixture-Path $name))) {
            Write-Fixture $name ('HarleyVR synthetic test placeholder only; not an executable, library, license, ROM or image: ' + $name)
        }
    }
    Write-Fixture 'Config\Supermodel.ini' '; Synthetic generic configuration; no player data.'
    Write-Fixture 'ROMs\harleyb.zip' 'HarleyVR synthetic placeholder; this is not a ZIP archive or game data.'
    $entries = @($immutable | ForEach-Object { [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath (Fixture-Path $_) -Algorithm SHA256).Hash} })
    $manifest = [ordered]@{schema=1;releaseReady=$true;mode='regular';sourceCommit=('a'*40);
        correspondingSource='https://example.invalid/harleyvr-synthetic-source';validatedRoutes=@('Meta','SteamVR');files=$entries;syntheticFixture=$true}
    Write-Fixture 'release-ready.json' ($manifest | ConvertTo-Json -Depth 8)
}
function New-Scenario([string]$Route) {
    $info = @{}
    foreach ($name in @('Meta','SteamVR')) {
        $library = if ($name -ceq 'Meta') { 'LibOVRRTImpl64_1.dll' } else { 'vrclient_x64.dll' }
        $info[$name] = [pscustomobject]@{Manifest=(Fixture-Path ('mock-runtime\'+$name+'\runtime.json'));
            Library=(Fixture-Path ('mock-runtime\'+$name+'\'+$library));DiagnosticCLI=(Fixture-Path 'mock-runtime\NEVER-RUN-diagnostic.txt')}
    }
    $processes = @()
    if ($Route -ieq 'Meta') {
        $processes = @([pscustomobject]@{Name='OVRServer_x64';Id=64001;Path=(Fixture-Path 'mock-runtime\Meta\OVRServer_x64.exe')})
    } else {
        $processes = @([pscustomobject]@{Name='vrserver';Id=64002},[pscustomobject]@{Name='vrcompositor';Id=64003})
    }
    [pscustomobject]@{RuntimeAvailable=$true;RuntimeInfo=$info;Processes=$processes;
        Discovery=(New-Object 'System.Collections.Generic.List[string]');
        ProcessQueries=(New-Object 'System.Collections.Generic.List[string]');
        Blocked=(New-Object 'System.Collections.Generic.List[string]')}
}
function Change-Manifest([scriptblock]$Edit) {
    $manifest = Get-Content -LiteralPath (Fixture-Path 'release-ready.json') -Raw | ConvertFrom-Json
    & $Edit $manifest
    Write-Fixture 'release-ready.json' ($manifest | ConvertTo-Json -Depth 8)
}
function Invoke-PreflightCase([string]$Name,[string]$Route,[bool]$Pass,[string[]]$ExpectedIssues=@(),
    [scriptblock]$Setup={},[switch]$Scaffold) {
    Reset-Fixture -Scaffold:$Scaffold
    $HarleyVRPreflightTestContext = New-Scenario $Route
    & $Setup $HarleyVRPreflightTestContext
    $before = Snapshot-Fixture
    $environmentBefore = Snapshot-Environment
    $result = & (Fixture-Path 'Play-HarleyVR.ps1') -Runtime $Route -PreflightOnly
    $normalized = if ($Route -ieq 'Meta') { 'Meta' } else { 'SteamVR' }
    Assert-Preflight ($null -ne $result -and $result.Passed -eq $Pass) ($Name + ': unexpected readiness result.')
    Assert-Preflight ($result.SelectedRoute -ceq $normalized) ($Name + ': route normalization changed.')
    $options = $expectedOptions -join ' '
    if ($normalized -ceq 'Meta') { $options = $options.Replace('-xr-scale=0.5','-xr-scale=0.75') }
    $expectedArguments = '"' + (Fixture-Path 'ROMs\harleyb.zip') + '" ' + $options
    Assert-Preflight ($result.Arguments -ceq $expectedArguments) ($Name + ': public launch arguments changed.')
    foreach ($field in @('GameLaunched','RuntimeLoaded','AswQueriedOrChanged','SystemDefaultRuntimeChanged','WriteAccessTested','HeadsetTrackingVerified')) {
        Assert-Preflight ($result.$field -eq $false) ($Name + ': preflight must not claim or perform ' + $field)
    }
    foreach ($issue in $ExpectedIssues) {
        Assert-Preflight (@($result.Issues | Where-Object { $_.Contains($issue) }).Count -gt 0) ($Name + ': required diagnostic missing: ' + $issue)
    }
    Assert-Preflight ($HarleyVRPreflightTestContext.Discovery.Count -eq 1 -and $HarleyVRPreflightTestContext.Discovery[0] -ceq $normalized) ($Name + ': discovery did not stay inside the selected mock route.')
    Assert-Preflight ($HarleyVRPreflightTestContext.ProcessQueries.Count -gt 0) ($Name + ': process-presence mock was not exercised.')
    Assert-Preflight ($HarleyVRPreflightTestContext.Blocked.Count -eq 0) ($Name + ': preflight reached a forbidden action.')
    Assert-Preflight ((Snapshot-Fixture) -ceq $before) ($Name + ': preflight created, deleted or changed fixture files/directories.')
    Assert-Preflight ((Snapshot-Environment) -ceq $environmentBefore) ($Name + ': preflight changed the process environment.')
    $cases.Add([pscustomobject]@{Name=$Name;Route=$normalized;ExpectedPassed=$Pass;Issues=$result.Issues.Count;ReadOnly=$true})
}

$sentinelProbes = 0
try {
    foreach ($route in @('SteamVR','Meta')) {
        Invoke-PreflightCase ('source-scaffold-'+$route) $route $false @('source/package draft','Required package file is missing: supermodel.exe','Default configuration is missing','Add your own legally obtained') -Scaffold
        Invoke-PreflightCase ('synthetic-ready-'+$route) $route $true
        Invoke-PreflightCase ('lowercase-route-'+$route) $route.ToLowerInvariant() $true
        Invoke-PreflightCase ('missing-config-'+$route) $route $false @('Default configuration is missing') { Remove-Item -LiteralPath (Fixture-Path 'Config\Supermodel.ini') }
        Invoke-PreflightCase ('missing-rom-'+$route) $route $false @('Add your own legally obtained') { Remove-Item -LiteralPath (Fixture-Path 'ROMs\harleyb.zip') }
        Invoke-PreflightCase ('missing-runtime-'+$route) $route $false @('Synthetic runtime discovery unavailable.') { param($context) $context.RuntimeAvailable=$false }
        Invoke-PreflightCase ('runtime-not-running-'+$route) $route $false @($(if($route -ceq 'Meta'){'Open Meta Horizon Link'}else{'Open SteamVR'})) { param($context) $context.Processes=@() }
        Invoke-PreflightCase ('existing-game-'+$route) $route $false @('An emulator is already running') { param($context) $context.Processes+= [pscustomobject]@{Name='supermodel';Id=64004} }
        Invoke-PreflightCase ('unvalidated-route-'+$route) $route $false @('has not been released for the selected') { Change-Manifest { param($m) $m.validatedRoutes=@() } }
    }
    foreach ($name in $immutable) {
        # Script dependencies must exist to invoke the launcher. Missing script
        # behavior is separately checked as a terminating, read-only failure.
        if ($name -in @('Play-HarleyVR.ps1','HarleyVR-Runtime.ps1','HarleyVR-Meta-ASW.ps1')) { continue }
        $missing = $name
        Invoke-PreflightCase ('missing-required-'+$name) 'SteamVR' $false @('Required package file is missing: '+$name) { Remove-Item -LiteralPath (Fixture-Path $missing) }
    }
    Invoke-PreflightCase 'malformed-release-json' 'SteamVR' $false @('Release metadata could not be read') { Write-Fixture 'release-ready.json' '{invalid synthetic JSON' }
    foreach ($field in @('schema','releaseReady','mode','sourceCommit','correspondingSource')) {
        $changedField = $field
        Invoke-PreflightCase ('invalid-release-'+$field) 'SteamVR' $false @('Release metadata is incomplete') {
            Change-Manifest { param($m) $m.$changedField = switch($changedField){schema{2} releaseReady{'true'} mode{'dream'} sourceCommit{'bad'} correspondingSource{'http://example.invalid/source'}} }
        }
    }
    Invoke-PreflightCase 'release-not-ready' 'Meta' $false @('Release metadata is incomplete') { Change-Manifest {param($m) $m.releaseReady=$false} }
    Invoke-PreflightCase 'duplicate-hash-entry' 'Meta' $false @('no unique hash for supermodel.exe') { Change-Manifest {param($m) $m.files+= $m.files[0]} }
    Invoke-PreflightCase 'missing-hash-entry' 'SteamVR' $false @('no unique hash for supermodel.exe') { Change-Manifest {param($m) $m.files=@($m.files | Where-Object{$_.path -cne 'supermodel.exe'})} }
    Invoke-PreflightCase 'invalid-hash-entry' 'SteamVR' $false @('no unique hash for supermodel.exe') { Change-Manifest {param($m) $m.files[0].sha256='invalid'} }
    Invoke-PreflightCase 'payload-hash-mismatch' 'Meta' $false @('Package file does not match this release: supermodel.exe') { Write-Fixture 'supermodel.exe' 'Changed inert placeholder; never executable.' }
    Invoke-PreflightCase 'steam-missing-server' 'SteamVR' $false @('Open SteamVR') {param($context) $context.Processes=@($context.Processes | Where-Object{$_.Name -cne 'vrserver'})}
    Invoke-PreflightCase 'steam-missing-compositor' 'SteamVR' $false @('Open SteamVR') {param($context) $context.Processes=@($context.Processes | Where-Object{$_.Name -cne 'vrcompositor'})}
    Invoke-PreflightCase 'meta-multiple-services' 'Meta' $false @('Open Meta Horizon Link') {param($context) $context.Processes+= $context.Processes[0]}
    Invoke-PreflightCase 'meta-service-path-mismatch' 'Meta' $false @('does not match the selected installed runtime') {param($context) $context.Processes[0].Path=Fixture-Path 'mock-runtime\Other\OVRServer_x64.exe'}
    Invoke-PreflightCase 'meta-steam-also-running' 'Meta' $false @('Close SteamVR normally') {param($context) $context.Processes+= [pscustomobject]@{Name='vrserver';Id=64002}}
    Invoke-PreflightCase 'existing-player-files-and-journal' 'Meta' $true @() {
        Write-Fixture 'Saves\synthetic.st0' 'Synthetic save sentinel, not game state.'
        Write-Fixture 'NVRAM\synthetic.nv' 'Synthetic NVRAM sentinel, not game data.'
        Write-Fixture 'Diagnostics\meta-asw-recovery.json' '{"synthetic":true,"status":"pending"}'
    }
    foreach ($dependency in @('HarleyVR-Runtime.ps1','HarleyVR-Meta-ASW.ps1')) {
        Reset-Fixture
        Remove-Item -LiteralPath (Fixture-Path $dependency)
        $before=Snapshot-Fixture
        $environmentBefore=Snapshot-Environment
        $HarleyVRPreflightTestContext=New-Scenario 'Meta'
        $failed=$false
        try { $null=& (Fixture-Path 'Play-HarleyVR.ps1') -Runtime Meta -PreflightOnly } catch { $failed=$true }
        Assert-Preflight $failed ('Missing script dependency must fail: '+$dependency)
        Assert-Preflight ((Snapshot-Fixture) -ceq $before -and (Snapshot-Environment) -ceq $environmentBefore) 'Missing dependency failure must remain read-only.'
        Assert-Preflight ($HarleyVRPreflightTestContext.Blocked.Count -eq 0) 'Missing dependency must not reach an action.'
        $cases.Add([pscustomobject]@{Name=('missing-script-'+$dependency);Route='Meta';ExpectedPassed=$false;ReadOnly=$true})
    }

    # Exercise sentinels themselves. These are deliberate blocked probes, not
    # calls made during preflight, and never invoke the underlying commands.
    Reset-Fixture
    $HarleyVRPreflightTestContext=New-Scenario 'Meta'
    $blockedActions=@('Get-HarleyVRRuntimeCandidates','New-HarleyVRGameStartInfo','Get-ItemProperty',
        'Set-ItemProperty','Start-Process','Stop-Process','Start-Service','Stop-Service','Add-Type',
        'Get-MetaServiceIdentity','Test-MetaProcessIdentity','Invoke-MetaAswCLI','Invoke-MetaAswChecked',
        'Get-MetaAswMode','Save-MetaAswJournal','New-MetaAswRunner',
        'Start-MetaAswGuard','Repair-MetaAswPending','Complete-MetaAswGuard','Set-MetaAswGame')
    $probeResult=& {
        . (Fixture-Path 'HarleyVR-Runtime.ps1')
        . (Fixture-Path 'HarleyVR-Meta-ASW.ps1')
        foreach($action in $blockedActions){
            $blocked=$false
            try { & $action | Out-Null } catch { $blocked=$_.Exception.Message.StartsWith('HARLEYVR_TEST_BLOCKED:') }
            [pscustomobject]@{Action=$action;Blocked=$blocked}
        }
        $blocked=$false
        try { New-Object Diagnostics.Process | Out-Null } catch { $blocked=$_.Exception.Message.StartsWith('HARLEYVR_TEST_BLOCKED:') }
        [pscustomobject]@{Action='process-object';Blocked=$blocked}
    }
    foreach($probe in $probeResult){Assert-Preflight $probe.Blocked ('Safety sentinel failed: '+$probe.Action);$sentinelProbes++}
    Assert-Preflight ($HarleyVRPreflightTestContext.Blocked.Count -eq $sentinelProbes) 'Every sentinel probe must be accounted for.'
    foreach($name in $sourceNames){
        Assert-Preflight ((Get-FileHash -LiteralPath (Join-Path $sourceRoot $name) -Algorithm SHA256).Hash -ceq $sourceHashes[$name]) ('Public source changed: '+$name)
    }
}
finally { Remove-OwnedFixture }

[pscustomobject]@{
    Passed=$true;Checks=$checks;Cases=$cases.Count;PowerShellVersion=$PSVersionTable.PSVersion.ToString();
    SentinelProbesBlocked=$sentinelProbes;ForbiddenActionsReachedDuringPreflight=0;
    LauncherByteIdentical=$true;OptionsMatchPublicContract=$true;RuntimeDiscoveryMocked=$true;
    ProcessIdentitiesMocked=$true;AswQueriedOrChanged=$false;GameLaunched=$false;RuntimeLoaded=$false;
    PackageFilesUnchangedDuringPreflight=$true;EnvironmentUnchanged=$true;FixturesRemoved=(-not(Test-Path -LiteralPath $fixtureFull));
    PrivateReferenceRequired=$false;PhysicalHeadsetValidated=$false;ReleaseReady=$false;
    Scope='Synthetic control-flow regression only. No installed-runtime discovery, real binary/ROM validation, packaging, headset or release validation.';
    SourceSHA256=$sourceHashes;Results=@($cases.ToArray())
}
