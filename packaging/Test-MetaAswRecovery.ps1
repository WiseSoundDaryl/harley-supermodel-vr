#requires -Version 5.1
<#
.SYNOPSIS
Exercises Meta ASW recovery using synthetic identities, commands and journals.
.DESCRIPTION
Runs directly from a source checkout. No Meta installation, emulator, ROM or VR
hardware is used. Native service/process/diagnostic entry points are replaced by
fail-closed test doubles before any recovery function is called. Only a uniquely
owned temporary directory is written and removed. A passing result is not real
runtime, headset or final-player-package validation.
#>
[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$TemporaryRoot = [IO.Path]::GetTempPath()
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$guardPath = Join-Path $Root 'packaging/HarleyVR-Meta-ASW.ps1'
$guardHash = (Get-FileHash -LiteralPath $guardPath -Algorithm SHA256).Hash
$tokens = $null; $parseErrors = $null
$guardAst = [Management.Automation.Language.Parser]::ParseFile($guardPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw 'The ASW guard has syntax errors.' }
function Test-RecoveryDeclarationOnly([Management.Automation.Language.ScriptBlockAst]$Ast) {
    # No initializer, trap, module import or named execution block may run while
    # dot-sourcing the helper, before fail-closed test doubles have been installed.
    if ($Ast.BeginBlock -or $Ast.ProcessBlock -or $Ast.DynamicParamBlock -or $Ast.ParamBlock -or
        ($null -ne $Ast.UsingStatements -and $Ast.UsingStatements.Count -gt 0)) { return $false }
    $cleanProperty=$Ast.PSObject.Properties['CleanBlock']
    if ($null -ne $cleanProperty -and $null -ne $cleanProperty.Value) { return $false }
    $requirements=$Ast.ScriptRequirements
    if ($requirements -and ($requirements.RequiredModules.Count -gt 0 -or $requirements.RequiredAssemblies.Count -gt 0)) { return $false }
    if ($null -eq $Ast.EndBlock -or ($null -ne $Ast.EndBlock.Traps -and $Ast.EndBlock.Traps.Count -gt 0)) { return $false }
    foreach ($statement in $Ast.EndBlock.Statements) {
        if ($statement -isnot [Management.Automation.Language.FunctionDefinitionAst]) { return $false }
    }
    return $true
}
if (-not (Test-RecoveryDeclarationOnly $guardAst)) {
    throw 'ASW helper must contain only function declarations; review before running synthetic tests.'
}
. $guardPath
$script:productionSaveJournal=(Get-Item -LiteralPath 'Function:\Save-MetaAswJournal').ScriptBlock

$script:checks = 0
$script:cases = New-Object 'System.Collections.Generic.List[string]'
$script:commandCount = 0
$script:nativeAttempts = 0
$script:caseState = $null
$script:syntheticOwnerStart = [DateTime]::Parse('2001-02-03T04:05:06Z').ToUniversalTime()
function Assert-Recovery([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw ('ASW synthetic regression: ' + $Message) }
    $script:checks++
}
function Assert-RecoveryThrows([scriptblock]$Action, [string]$Pattern) {
    $caught = $null
    try { $null = & $Action } catch { $caught = $_.Exception.Message }
    Assert-Recovery ($null -ne $caught) 'Expected a rejected recovery operation.'
    Assert-Recovery ($caught -match $Pattern) ('Unexpected rejection; expected ' + $Pattern)
}
function Deny-RecoveryNativeCall {
    $script:nativeAttempts++
    throw 'A synthetic ASW test tried to cross a native runtime/process boundary.'
}
# None of these test replacements contacts the machine's actual Meta service.
function Invoke-MetaAswCLI { Deny-RecoveryNativeCall }
function Get-MetaServiceIdentity { Deny-RecoveryNativeCall }
function Test-MetaProcessIdentity { Deny-RecoveryNativeCall }
function New-MetaAswRunner { Deny-RecoveryNativeCall }
function Get-Process {
    param([int[]]$Id)
    # Start-MetaAswGuard records its host identity. Supply a synthetic start time
    # without inspecting even that process; every other process lookup is denied.
    if ($Id.Count -ne 1 -or $Id[0] -ne $PID) { Deny-RecoveryNativeCall }
    [pscustomobject]@{ Id=$PID; StartTime=$script:syntheticOwnerStart }
}
function Save-MetaAswJournal([string]$Path,$Journal) {
    $script:caseState.SaveCalls++
    if($script:caseState.FailSaveCalls -contains $script:caseState.SaveCalls) { throw 'Synthetic journal write failure.' }
    & $script:productionSaveJournal $Path $Journal
}
function New-RecoveryIdentity([int]$Number, [string]$Started='2001-02-03T05:00:00.0000000Z') {
    [ordered]@{ProcessId=$Number; StartedUTC=$Started; Executable='X:\synthetic-only\MetaService.exe'}
}
function New-RecoveryCase([string]$Name, [string]$Mode='auto') {
    Assert-Recovery (-not $script:cases.Contains($Name)) 'Duplicate case name.'
    $script:cases.Add($Name)
    $script:caseState = @{
        Path=(Join-Path $scratch ('case-' + $script:cases.Count + '.json'));
        Mode=$Mode; Calls=(New-Object 'System.Collections.Generic.List[string]'); Counts=@{};
        Faults=@(); Service=(New-RecoveryIdentity 7101); ServiceSequence=@(); ServiceCalls=0;
        Processes=@{}; ProcessChecks=(New-Object 'System.Collections.Generic.List[int]'); SaveCalls=0; FailSaveCalls=@()
    }
}
function Read-RecoveryJournal {
    Get-Content -LiteralPath $script:caseState.Path -Raw | ConvertFrom-Json
}
function Get-RecoveryService {
    $index=$script:caseState.ServiceCalls; $script:caseState.ServiceCalls++
    if ($script:caseState.ServiceSequence.Count -gt 0) {
        return $script:caseState.ServiceSequence[[Math]::Min($index,$script:caseState.ServiceSequence.Count-1)]
    }
    return $script:caseState.Service
}
function Test-RecoveryProcess($Identity) {
    if ($null -eq $Identity) { return $false }
    # A process provider is not responsible for validating recovery journal data.
    # Reject malformed identities in the guard before this provider is called.
    if ($Identity -isnot [Collections.IDictionary] -or -not $Identity.Contains('ProcessId') -or
        -not $Identity.Contains('StartedUTC')) { return $false }
    $script:caseState.ProcessChecks.Add([int]$Identity.ProcessId)
    if (-not $script:caseState.Processes.ContainsKey([int]$Identity.ProcessId)) { return $false }
    $actual=$script:caseState.Processes[[int]$Identity.ProcessId]
    return ([DateTimeOffset]$actual.StartedUTC).UtcDateTime.Ticks -eq ([DateTimeOffset]$Identity.StartedUTC).UtcDateTime.Ticks
}
function Invoke-RecoveryCommand([string]$Command) {
    Assert-Recovery ($Command -cin @('server:asw.Mode','server:asw.Off','server:asw.Auto')) 'Unexpected mock command.'
    $script:commandCount++; $script:caseState.Calls.Add($Command)
    if (-not $script:caseState.Counts.ContainsKey($Command)) { $script:caseState.Counts[$Command]=0 }
    $script:caseState.Counts[$Command]++
    $fault=@($script:caseState.Faults | Where-Object { $_.Command -ceq $Command -and $_.At -eq $script:caseState.Counts[$Command] })
    Assert-Recovery ($fault.Count -le 1) 'Ambiguous mock fault.'
    $kind=if($fault.Count){$fault[0].Kind}else{''}
    if ($kind -ceq 'throw') { throw 'Synthetic command runner failure.' }
    $result=[pscustomobject]@{ExitCode=0; TimedOut=$false; Output=''; Error=''}
    if ($Command -cne 'server:asw.Mode') {
        $saved=Read-RecoveryJournal
        Assert-Recovery ($saved.Status -ceq 'pending' -and $saved.BeforeMode -cin @('auto','off')) 'A durable recovery journal must precede every setting command.'
        Assert-Recovery ($saved.Service.ProcessId -eq 7101) 'Recovery must retain the original synthetic service.'
        if ($kind -cnotin @('no-effect','failure','timeout','stderr','error-output')) {
            $script:caseState.Mode=if($Command -ceq 'server:asw.Off'){'off'}else{'auto'}
        }
    } else { $result.Output=$script:caseState.Mode }
    switch -CaseSensitive ($kind) {
        'failure' { $result.ExitCode=9 }
        'failure-after-apply' { $result.ExitCode=9 }
        'timeout' { $result.TimedOut=$true }
        'timeout-after-apply' { $result.TimedOut=$true }
        'stderr' { $result.Error='synthetic diagnostic detail' }
        'error-output' { $result.Output='error: synthetic failure' }
        'empty' { $result.Output='' }
        'multiple' { $result.Output="auto`noff" }
        'unknown' { $result.Output='forced-45' }
        'uppercase' { $result.Output='AUTO' }
        'prompt' { $result.Output="=> server:asw.Mode`r`n auto `r`n=>" }
    }
    return $result
}
$runner={param($Command) Invoke-RecoveryCommand $Command}
$serviceProvider={Get-RecoveryService}
$processAlive={param($Identity) Test-RecoveryProcess $Identity}
function Start-RecoveryTest { Start-MetaAswGuard -Path $script:caseState.Path -Runner $runner -ServiceProvider $serviceProvider }
function Complete-RecoveryTest($Context) { Complete-MetaAswGuard -Context $Context -ServiceProvider $serviceProvider }
function Repair-RecoveryTest { Repair-MetaAswPending -Path $script:caseState.Path -Runner $runner -ServiceProvider $serviceProvider -ProcessAlive $processAlive }
function Write-RecoveryFixture($Journal) { $Journal | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $script:caseState.Path -Encoding utf8 }
function Assert-JournalUnchanged([string]$Hash) { Assert-Recovery ((Get-FileHash -LiteralPath $script:caseState.Path).Hash -ceq $Hash) 'Rejected recovery changed its journal.' }
function Assert-NoSettingCommand {
    Assert-Recovery (@($script:caseState.Calls | Where-Object { $_ -cne 'server:asw.Mode' }).Count -eq 0) 'Unsafe recovery issued a setting command.'
}
function Clear-RecoveryCalls { $script:caseState.Calls.Clear(); $script:caseState.Counts.Clear(); $script:caseState.ServiceCalls=0 }

$temporaryBase=(Get-Item -LiteralPath $TemporaryRoot -Force).FullName
if (-not [IO.Directory]::Exists($temporaryBase)) { throw 'TemporaryRoot must be an existing directory.' }
$ownedName='harleyvr-asw-tests-'+[guid]::NewGuid().ToString('N')
$scratch=[IO.Path]::GetFullPath((Join-Path $temporaryBase $ownedName))
$scratchPrefix=$scratch+[IO.Path]::DirectorySeparatorChar
if ([IO.Path]::GetDirectoryName($scratch) -cne $temporaryBase.TrimEnd([IO.Path]::DirectorySeparatorChar)) { throw 'Unsafe temporary test path.' }
$null=New-Item -ItemType Directory -Path $scratch -ErrorAction Stop
$marker=Join-Path $scratch '.test-owned'
Set-Content -LiteralPath $marker -Value $ownedName -Encoding ascii
$cleaned=$false
try {
    # These fixtures are parsed only, never dot-sourced or otherwise executed.
    $declarationFixtures=@(
        @{Name='function declarations';Code='function SyntheticStub { param($x) $x }';Allowed=$true},
        @{Name='begin block';Code="begin { throw 'never execute' } end { function SyntheticStub {} }";Allowed=$false},
        @{Name='process block';Code="process { throw 'never execute' } end { function SyntheticStub {} }";Allowed=$false},
        @{Name='dynamic parameter block';Code="dynamicparam { throw 'never execute' } end { function SyntheticStub {} }";Allowed=$false},
        @{Name='parameter initializer';Code='param($value = $(throw "never execute")); function SyntheticStub {}';Allowed=$false},
        @{Name='end trap';Code="trap { throw 'never execute' }; function SyntheticStub {}";Allowed=$false},
        @{Name='end statement';Code="function SyntheticStub {}; throw 'never execute'";Allowed=$false},
        @{Name='using declaration';Code="using namespace System`nfunction SyntheticStub {}";Allowed=$false},
        @{Name='required module';Code="#requires -Modules NeverLoadThisSyntheticModule`nfunction SyntheticStub {}";Allowed=$false},
        @{Name='required assembly';Code="#requires -Assembly NeverLoadThisSyntheticAssembly`nfunction SyntheticStub {}";Allowed=$false},
        @{Name='clean block';Code="end { function SyntheticStub {} } clean { throw 'never execute' }";Allowed=$false}
    )
    foreach($fixture in $declarationFixtures) {
        New-RecoveryCase ('declaration-only gate '+$fixture.Name)
        $fixtureTokens=$null; $fixtureErrors=$null
        $fixtureAst=[Management.Automation.Language.Parser]::ParseInput($fixture.Code,[ref]$fixtureTokens,[ref]$fixtureErrors)
        # Windows PowerShell5.1 does not understand clean blocks. A syntax error
        # is also a rejection, before the declaration-only gate could be reached.
        $accepted=$fixtureErrors.Count -eq 0 -and (Test-RecoveryDeclarationOnly $fixtureAst)
        Assert-Recovery ($accepted -eq $fixture.Allowed) ('Unexpected declaration gate result: '+$fixture.Name)
        Assert-Recovery ($script:caseState.Calls.Count -eq 0 -and $script:nativeAttempts -eq 0) 'AST fixtures executed recovery actions.'
    }
    New-RecoveryCase 'auto disables and restores'
    $context=Start-RecoveryTest
    Assert-Recovery ($script:caseState.Mode -ceq 'off') 'Auto was not disabled.'
    Assert-Recovery ($context.Journal.BeforeMode -ceq 'auto') 'Original auto mode not saved.'
    $saved=Read-RecoveryJournal
    Assert-Recovery ($saved.Status -ceq 'pending' -and ([DateTimeOffset]$saved.Owner.StartedUTC).UtcDateTime.Ticks -eq $script:syntheticOwnerStart.Ticks) 'Synthetic owner/pending status missing.'
    $done=Complete-RecoveryTest $context
    Assert-Recovery ($done.Status -ceq 'restored' -and $script:caseState.Mode -ceq 'auto') 'Auto restoration failed.'
    Assert-Recovery ((Read-RecoveryJournal).Status -ceq 'restored') 'Restoration not recorded.'
    $count=$script:caseState.Calls.Count; $null=Complete-RecoveryTest $context
    Assert-Recovery ($script:caseState.Calls.Count -eq $count) 'Completed guard must be idempotent.'

    New-RecoveryCase 'previously disabled stays disabled' 'off'
    $context=Start-RecoveryTest; $done=Complete-RecoveryTest $context
    Assert-Recovery ($done.Status -ceq 'unchanged-off' -and $script:caseState.Mode -ceq 'off') 'Previously disabled ASW changed.'
    Assert-NoSettingCommand

    New-RecoveryCase 'external auto is preserved'
    $context=Start-RecoveryTest; Clear-RecoveryCalls; $script:caseState.Mode='auto'
    $done=Complete-RecoveryTest $context
    Assert-Recovery ($done.Status -ceq 'external-change-preserved') 'External mode was not preserved.'
    Assert-NoSettingCommand

    New-RecoveryCase 'unknown external mode is untouched'
    $context=Start-RecoveryTest; Clear-RecoveryCalls; $script:caseState.Mode='forced-45'
    Assert-RecoveryThrows { Complete-RecoveryTest $context } 'unknown|not safely restorable'
    Assert-NoSettingCommand
    $saved=Read-RecoveryJournal
    Assert-Recovery ($saved.Status -ceq 'pending' -and $saved.BeforeMode -ceq 'auto' -and $saved.LastRestoreError) 'Unknown external mode lost recovery information.'

    foreach($kind in @('empty','multiple','unknown','uppercase','failure','timeout','stderr','error-output','throw')) {
        New-RecoveryCase ('initial query rejects '+$kind)
        $script:caseState.Faults=@(@{Command='server:asw.Mode';At=1;Kind=$kind})
        Assert-RecoveryThrows { Start-RecoveryTest } 'unknown|restorable|failed|unexpected|Synthetic'
        Assert-NoSettingCommand
        Assert-Recovery (-not (Test-Path -LiteralPath $script:caseState.Path)) 'Initial query failure created a recovery journal.'
    }
    New-RecoveryCase 'diagnostic prompt parsing'
    $script:caseState.Faults=@(@{Command='server:asw.Mode';At=1;Kind='prompt'})
    $context=Start-RecoveryTest; $null=Complete-RecoveryTest $context
    Assert-Recovery ($script:caseState.Mode -ceq 'auto') 'Supported CLI prompt parsing failed.'

    foreach($kind in @('failure','timeout','stderr','error-output','no-effect','failure-after-apply','timeout-after-apply','throw')) {
        New-RecoveryCase ('disable failure recovers '+$kind)
        $script:caseState.Faults=@(@{Command='server:asw.Off';At=1;Kind=$kind})
        Assert-RecoveryThrows { Start-RecoveryTest } 'failed|unexpected|disable|Synthetic'
        Assert-Recovery ($script:caseState.Mode -ceq 'auto') 'Setup failure left a synthetic override behind.'
        $saved=Read-RecoveryJournal
        Assert-Recovery ($saved.BeforeMode -ceq 'auto' -and $saved.Status -cin @('restored','external-change-preserved')) 'Setup failure lost original state or recovery status.'
    }

    foreach($kind in @('failure','timeout','stderr','error-output','no-effect','throw')) {
        New-RecoveryCase ('restore failure remains recoverable '+$kind)
        $context=Start-RecoveryTest; Clear-RecoveryCalls
        $script:caseState.Faults=@(@{Command='server:asw.Auto';At=1;Kind=$kind})
        Assert-RecoveryThrows { Complete-RecoveryTest $context } 'failed|unexpected|take effect|Synthetic'
        $saved=Read-RecoveryJournal
        Assert-Recovery ($saved.Status -ceq 'pending' -and $saved.BeforeMode -ceq 'auto' -and $saved.LastRestoreError) 'Failed restoration lost pending recovery information.'
        Assert-Recovery ($saved.Service.ProcessId -eq 7101 -and $saved.Owner.StartedUTC) 'Failed restoration lost identities.'
        $script:caseState.Faults=@(); Clear-RecoveryCalls
        $done=Repair-RecoveryTest
        Assert-Recovery ($done.Status -ceq 'restored' -and $script:caseState.Mode -ceq 'auto') 'Later interrupted-session recovery failed.'
    }
    foreach($kind in @('failure-after-apply','timeout-after-apply')) {
        New-RecoveryCase ('restore uncertain after application '+$kind)
        $context=Start-RecoveryTest; Clear-RecoveryCalls
        $script:caseState.Faults=@(@{Command='server:asw.Auto';At=1;Kind=$kind})
        Assert-RecoveryThrows { Complete-RecoveryTest $context } 'failed'
        Assert-Recovery ((Read-RecoveryJournal).Status -ceq 'pending' -and $script:caseState.Mode -ceq 'auto') 'Uncertain restoration lost its pending record.'
        $script:caseState.Faults=@(); Clear-RecoveryCalls
        $done=Repair-RecoveryTest
        Assert-Recovery ($done.Status -ceq 'external-change-preserved') 'Recovery overwrote a mode already restored or changed externally.'
        Assert-NoSettingCommand
    }
    New-RecoveryCase 'initial journal write failure prevents any override'
    $script:caseState.FailSaveCalls=@(1)
    Assert-RecoveryThrows { Start-RecoveryTest } 'journal write failure'
    Assert-NoSettingCommand
    Assert-Recovery ($script:caseState.Mode -ceq 'auto' -and -not (Test-Path -LiteralPath $script:caseState.Path)) 'Journal preparation failure changed the setting.'

    New-RecoveryCase 'completion write failure retains original pending record'
    $context=Start-RecoveryTest; Clear-RecoveryCalls
    $before=(Get-FileHash -LiteralPath $script:caseState.Path).Hash
    $script:caseState.FailSaveCalls=@(2,3)
    Assert-RecoveryThrows { Complete-RecoveryTest $context } 'journal write failure'
    Assert-JournalUnchanged $before
    Assert-Recovery ((Read-RecoveryJournal).Status -ceq 'pending' -and $script:caseState.Mode -ceq 'auto') 'Unwritable completion destroyed durable recovery information.'
    $script:caseState.FailSaveCalls=@(); Clear-RecoveryCalls
    $done=Repair-RecoveryTest
    Assert-Recovery ($done.Status -ceq 'external-change-preserved') 'Retry did not preserve the recovered mode.'
    Assert-NoSettingCommand

    New-RecoveryCase 'failed restoration plus write failure preserves earlier journal'
    $context=Start-RecoveryTest; Clear-RecoveryCalls
    $before=(Get-FileHash -LiteralPath $script:caseState.Path).Hash
    $script:caseState.Faults=@(@{Command='server:asw.Auto';At=1;Kind='failure'})
    $script:caseState.FailSaveCalls=@(2)
    Assert-RecoveryThrows { Complete-RecoveryTest $context } 'journal write failure'
    Assert-JournalUnchanged $before
    Assert-Recovery ((Read-RecoveryJournal).Status -ceq 'pending' -and $script:caseState.Mode -ceq 'off') 'Failed restoration destroyed original recovery information.'
    $script:caseState.FailSaveCalls=@(); $script:caseState.Faults=@(); Clear-RecoveryCalls
    Assert-Recovery ((Repair-RecoveryTest).Status -ceq 'restored') 'Recovery after persistent failures did not succeed.'

    New-RecoveryCase 'interrupted session with exited owner and child'
    $context=Start-RecoveryTest
    $context.Journal.Game=New-RecoveryIdentity 7102
    Write-RecoveryFixture $context.Journal; Clear-RecoveryCalls
    $done=Repair-RecoveryTest
    Assert-Recovery ($done.Status -ceq 'restored' -and $script:caseState.Mode -ceq 'auto') 'Interrupted session was not recovered.'
    Assert-Recovery ($script:caseState.ProcessChecks.Contains(7102)) 'Child liveness was not checked.'

    foreach($live in @('owner','child')) {
        New-RecoveryCase ('live '+$live+' blocks interrupted recovery')
        $context=Start-RecoveryTest; $context.Journal.Game=New-RecoveryIdentity 7102
        Write-RecoveryFixture $context.Journal; Clear-RecoveryCalls
        $identity=if($live -ceq 'owner'){$context.Journal.Owner}else{$context.Journal.Game}
        $script:caseState.Processes[[int]$identity.ProcessId]=$identity
        $before=(Get-FileHash -LiteralPath $script:caseState.Path).Hash
        Assert-RecoveryThrows { Repair-RecoveryTest } 'still running'
        Assert-Recovery ($script:caseState.Calls.Count -eq 0 -and $script:caseState.ServiceCalls -eq 0) 'Live-session refusal queried the runtime.'
        Assert-JournalUnchanged $before
    }
    New-RecoveryCase 'reused owner and child PIDs are not live identities'
    $context=Start-RecoveryTest; $context.Journal.Game=New-RecoveryIdentity 7102
    Write-RecoveryFixture $context.Journal; Clear-RecoveryCalls
    foreach($identity in @($context.Journal.Owner,$context.Journal.Game)) {
        $script:caseState.Processes[[int]$identity.ProcessId]=New-RecoveryIdentity ([int]$identity.ProcessId) '2002-02-03T05:00:00.0000000Z'
    }
    $done=Repair-RecoveryTest
    Assert-Recovery ($done.Status -ceq 'restored') 'PID reuse incorrectly blocked recovery.'

    foreach($change in @('pid','start','executable')) {
        New-RecoveryCase ('replacement service '+$change+' is untouched')
        $context=Start-RecoveryTest; Clear-RecoveryCalls
        $replacement=New-RecoveryIdentity 7101
        switch($change) {
            'pid' { $replacement.ProcessId=7201 }
            'start' { $replacement.StartedUTC='2002-02-03T05:00:00.0000000Z' }
            'executable' { $replacement.Executable='X:\synthetic-other\MetaService.exe' }
        }
        $script:caseState.Service=$replacement
        $done=Complete-RecoveryTest $context
        Assert-Recovery ($done.Status -ceq 'service-changed-no-restore') 'Replacement service was not recognized.'
        Assert-Recovery ($script:caseState.Calls.Count -eq 0) 'Replacement service was queried or modified.'
    }
    New-RecoveryCase 'service changes between restore query and write'
    $context=Start-RecoveryTest; Clear-RecoveryCalls
    $script:caseState.ServiceSequence=@((New-RecoveryIdentity 7101),(New-RecoveryIdentity 7201))
    Assert-RecoveryThrows { Complete-RecoveryTest $context } 'changed during ASW restoration'
    Assert-NoSettingCommand
    Assert-Recovery ((Read-RecoveryJournal).Status -ceq 'pending') 'Restore race lost pending recovery.'

    foreach($checkAt in @(2,3)) {
        New-RecoveryCase ('service changes during setup check '+$checkAt)
        $sequence=@(); for($n=1;$n -lt $checkAt;$n++){$sequence+=,(New-RecoveryIdentity 7101)}
        $sequence+=,(New-RecoveryIdentity 7201); $script:caseState.ServiceSequence=$sequence
        Assert-RecoveryThrows { Start-RecoveryTest } 'changed during preparation|changed before ASW setup'
        Assert-NoSettingCommand
        if($checkAt -eq 2){Assert-Recovery (-not (Test-Path -LiteralPath $script:caseState.Path)) 'Early service change created a journal.'}
        else {Assert-Recovery ((Read-RecoveryJournal).Status -ceq 'service-changed-no-restore') 'Recorded service change was not safely finalized.'}
    }

    New-RecoveryCase 'pending journal blocks a second guard'
    $null=Start-RecoveryTest; Clear-RecoveryCalls
    $before=(Get-FileHash -LiteralPath $script:caseState.Path).Hash
    Assert-RecoveryThrows { Start-RecoveryTest } 'outstanding ASW recovery'
    Assert-Recovery ($script:caseState.Calls.Count -eq 0 -and $script:caseState.ServiceCalls -eq 0) 'Pending journal triggered another setup.'
    Assert-JournalUnchanged $before

    New-RecoveryCase 'missing journal is a read-only no-op'
    Assert-Recovery ($null -eq (Repair-RecoveryTest)) 'Missing journal returned recovery work.'
    Assert-Recovery ($script:caseState.Calls.Count -eq 0 -and $script:caseState.ServiceCalls -eq 0) 'Missing journal contacted a mock runtime.'
    foreach($status in @('restored','unchanged-off','external-change-preserved','service-changed-no-restore')) {
        New-RecoveryCase ('terminal journal '+$status+' is a read-only no-op')
        Write-RecoveryFixture @{Schema=1;BeforeMode='auto';Status=$status}
        $before=(Get-FileHash -LiteralPath $script:caseState.Path).Hash
        $done=Repair-RecoveryTest
        Assert-Recovery ($done.Status -ceq $status -and $script:caseState.Calls.Count -eq 0 -and $script:caseState.ServiceCalls -eq 0) 'Terminal journal retried restoration.'
        Assert-JournalUnchanged $before
    }
    foreach($bad in @('json','array-root','empty-root','null-root','schema','schema-string','schema-bool','schema-fraction','before-mode','status','owner-missing','service-missing','owner-date','service-date',
        'owner-pid-zero','owner-pid-negative','owner-pid-string','owner-pid-bool','owner-pid-fraction','owner-pid-overflow',
        'service-pid-negative','service-executable-empty','service-executable-relative','game-missing-field','game-missing-date','game-date',
        'game-pid-zero','game-pid-string','game-pid-fraction','game-empty','game-array','owner-date-no-zone')) {
        New-RecoveryCase ('malformed journal '+$bad+' is preserved')
        $journal=@{Schema=1;BeforeMode='auto';Status='pending';Owner=(New-RecoveryIdentity 7103);Service=(New-RecoveryIdentity 7101);Game=$null}
        switch($bad) {
            'schema' { $journal.Schema=17 }
            'schema-string' { $journal.Schema='1' }
            'schema-bool' { $journal.Schema=$true }
            'schema-fraction' { $journal.Schema=1.5 }
            'before-mode' { $journal.BeforeMode='forced-45' }
            'status' { $journal.Status='unrecognized' }
            'owner-missing' { $journal.Owner=$null }
            'service-missing' { $journal.Service=$null }
            'owner-date' { $journal.Owner.StartedUTC='not-a-date' }
            'service-date' { $journal.Service.StartedUTC='not-a-date' }
            'owner-date-no-zone' { $journal.Owner.StartedUTC='2001-02-03T05:00:00' }
            'owner-pid-zero' { $journal.Owner.ProcessId=0 }
            'owner-pid-negative' { $journal.Owner.ProcessId=-7 }
            'owner-pid-string' { $journal.Owner.ProcessId='7103' }
            'owner-pid-bool' { $journal.Owner.ProcessId=$true }
            'owner-pid-fraction' { $journal.Owner.ProcessId=7103.5 }
            'owner-pid-overflow' { $journal.Owner.ProcessId=2147483648 }
            'service-pid-negative' { $journal.Service.ProcessId=-7 }
            'service-executable-empty' { $journal.Service.Executable='' }
            'service-executable-relative' { $journal.Service.Executable='MetaService.exe' }
            'game-missing-field' { $journal.Remove('Game') }
            'game-missing-date' { $journal.Game=@{ProcessId=7102} }
            'game-date' { $journal.Game=New-RecoveryIdentity 7102 'not-a-date' }
            'game-pid-zero' { $journal.Game=New-RecoveryIdentity 0 }
            'game-pid-string' { $journal.Game=New-RecoveryIdentity 7102; $journal.Game.ProcessId='7102' }
            'game-pid-fraction' { $journal.Game=New-RecoveryIdentity 7102; $journal.Game.ProcessId=7102.5 }
            'game-empty' { $journal.Game=@{} }
            'game-array' { $journal.Game=@((New-RecoveryIdentity 7102)) }
        }
        if($bad -ceq 'json'){Set-Content -LiteralPath $script:caseState.Path -Value '{ not valid json' -Encoding utf8}
        elseif($bad -ceq 'array-root'){Set-Content -LiteralPath $script:caseState.Path -Value ('['+($journal | ConvertTo-Json -Depth 10)+']') -Encoding utf8}
        elseif($bad -ceq 'empty-root'){Set-Content -LiteralPath $script:caseState.Path -Value '' -Encoding utf8}
        elseif($bad -ceq 'null-root'){Set-Content -LiteralPath $script:caseState.Path -Value 'null' -Encoding utf8}
        else {Write-RecoveryFixture $journal}
        $before=(Get-FileHash -LiteralPath $script:caseState.Path).Hash
        Assert-RecoveryThrows { Repair-RecoveryTest } '.'
        Assert-Recovery ($script:caseState.Calls.Count -eq 0 -and $script:caseState.ServiceCalls -eq 0) 'Malformed journal reached the runtime.'
        Assert-JournalUnchanged $before
        Assert-RecoveryThrows { Start-RecoveryTest } '.'
        Assert-Recovery ($script:caseState.Calls.Count -eq 0 -and $script:caseState.ServiceCalls -eq 0) 'Malformed journal was overwritten by new setup.'
        Assert-JournalUnchanged $before
    }

    Assert-Recovery ($script:nativeAttempts -eq 0) 'A native runtime/process boundary was attempted.'
    Assert-Recovery ((Get-FileHash -LiteralPath $guardPath -Algorithm SHA256).Hash -ceq $guardHash) 'The guard source was modified by tests.'
} finally {
    # Exact owned path, marker and link checks precede the sole recursive removal.
    if ([IO.Path]::GetFullPath($scratch) -cne $scratch -or
        [IO.Path]::GetDirectoryName($scratch) -cne $temporaryBase.TrimEnd([IO.Path]::DirectorySeparatorChar) -or
        (Get-Content -LiteralPath $marker -Raw).Trim() -cne $ownedName) { throw 'Refusing unsafe synthetic test cleanup.' }
    $items=@(Get-Item -LiteralPath $scratch -Force)+@(Get-ChildItem -LiteralPath $scratch -Force -Recurse)
    foreach($item in $items) {
        if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
           ($item.FullName -cne $scratch -and -not $item.FullName.StartsWith($scratchPrefix,[StringComparison]::Ordinal))) {
            throw 'Refusing linked or escaped synthetic test cleanup.'
        }
    }
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction Stop
    $cleaned= -not (Test-Path -LiteralPath $scratch)
}
[pscustomobject]@{
    Passed=$true; Cases=$script:cases.Count; Checks=$script:checks; MockCommands=$script:commandCount;
    PowerShellVersion=$PSVersionTable.PSVersion.ToString(); GuardSHA256=$guardHash;
    NativeBoundaryAttempts=$script:nativeAttempts; TemporaryFilesRemoved=$cleaned;
    RealMetaContacted=$false; EmulatorLaunched=$false; RealASWChanged=$false; HeadsetRequired=$false;
    CaseNames=$script:cases.ToArray();
    Scope='Synthetic recovery logic only; not actual Meta command behavior, race-free runtime control, headset compatibility or release readiness.'
}
