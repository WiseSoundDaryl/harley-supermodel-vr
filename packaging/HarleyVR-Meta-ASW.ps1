# Dot-source only. No command, runtime or setting is touched by loading this file.
# Portable user launcher: journal stays in the package's Diagnostics folder.
# Developer tests reserve storage separately and inject a mock runner.
#requires -Version 5.1
function Get-MetaServiceIdentity {
    $items=@(Get-Process OVRServer_x64 -ErrorAction SilentlyContinue)
    if($items.Count -ne 1){throw 'Open the Meta/Oculus app first. Exactly one Meta runtime service must already be running.'}
    $p=$items[0]
    [pscustomobject]@{ProcessId=$p.Id;StartedUTC=$p.StartTime.ToUniversalTime().ToString('o');Executable=$p.Path}
}
function Test-MetaProcessIdentity($Identity) {
    if(!$Identity -or !$Identity.ProcessId){return $false}
    $p=Get-Process -Id $Identity.ProcessId -ErrorAction SilentlyContinue
    return [bool]($p -and $p.StartTime.ToUniversalTime().Ticks -eq ([DateTimeOffset]$Identity.StartedUTC).UtcDateTime.Ticks)
}
function Test-MetaSameService($A,$B) {
    return [bool]($A -and $B -and $A.ProcessId -eq $B.ProcessId -and
        ([DateTimeOffset]$A.StartedUTC).UtcDateTime.Ticks -eq ([DateTimeOffset]$B.StartedUTC).UtcDateTime.Ticks -and $A.Executable -eq $B.Executable)
}
function Invoke-MetaAswCLI([string]$Command, [string]$DiagnosticCLI) {
    if($Command -cnotin @('server:asw.Mode','server:asw.Off','server:asw.Auto')){throw 'Unsupported Meta ASW command.'}
    $cli=$DiagnosticCLI
    if(!(Test-Path -LiteralPath $cli -PathType Leaf)){throw 'The installed Meta diagnostic command tool is missing.'}
    $si=[Diagnostics.ProcessStartInfo]::new($cli)
    $si.UseShellExecute=$false; $si.CreateNoWindow=$true
    $si.RedirectStandardInput=$true; $si.RedirectStandardOutput=$true; $si.RedirectStandardError=$true
    $p=[Diagnostics.Process]::new(); $p.StartInfo=$si
    try {
        if(!$p.Start()){throw 'Meta diagnostic command tool did not start.'}
        $output=$p.StandardOutput.ReadToEndAsync(); $errors=$p.StandardError.ReadToEndAsync()
        $p.StandardInput.WriteLine($Command); $p.StandardInput.WriteLine('exit'); $p.StandardInput.Close()
        if(!$p.WaitForExit(10000)) {
            $p.Kill(); $null=$p.WaitForExit(2000)
            return [pscustomobject]@{ExitCode=-1;TimedOut=$true;Output=$(if($output.IsCompleted){$output.Result}else{''});Error=$(if($errors.IsCompleted){$errors.Result}else{''})}
        }
        return [pscustomobject]@{ExitCode=$p.ExitCode;TimedOut=$false;Output=$output.GetAwaiter().GetResult();Error=$errors.GetAwaiter().GetResult()}
    } finally {$p.Dispose()}
}
function Invoke-MetaAswChecked([string]$Command,[scriptblock]$Runner,$Events) {
    $result=& $Runner $Command
    if($null -ne $Events){$Events.Add([pscustomobject]@{UTC=[DateTime]::UtcNow.ToString('o');Command=$Command;Result=$result})}
    if(!$result -or $result.TimedOut -or $result.ExitCode -ne 0 -or
       ($result.Output + "`n" + $result.Error) -match '(?i)failed|error|unknown|not allowed|unable|invalid|timed?\s*out') {
        throw "Meta ASW command failed or timed out: $Command. The game was not started by this command."
    }
    if(![string]::IsNullOrWhiteSpace($result.Error)){throw 'Meta ASW command returned unexpected diagnostic errors.'}
    return $result.Output
}
function Get-MetaAswMode([scriptblock]$Runner,$Events=$null) {
    $output=Invoke-MetaAswChecked 'server:asw.Mode' $Runner $Events
    $lines=@($output -split '\r?\n' | ForEach-Object {$_.Trim()} | Where-Object {$_ -and $_ -notmatch '^=>'})
    if($lines.Count -ne 1 -or $lines[0] -cnotin @('auto','off')) {
        throw 'Meta ASW mode is unknown or not safely restorable. No new mode change is authorized by this result.'
    }
    return $lines[0]
}
function Save-MetaAswJournal([string]$Path,$Journal) {
    $temporary=$Path+'.tmp'
    $Journal | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}
function Test-MetaJournalInteger($Value) {
    # JSON numbers must be integral identities, not strings, booleans or fractions.
    return ($Value -is [int] -or $Value -is [long] -or $Value -is [short] -or $Value -is [byte])
}
function Test-MetaJournalTimestamp($Value) {
    # PowerShell 7 can deserialize ISO timestamps as DateTime; 5.1 keeps strings.
    if ($Value -is [DateTime]) { return $Value.Kind -ne [DateTimeKind]::Unspecified }
    if ($Value -is [DateTimeOffset]) { return $true }
    if ($Value -isnot [string] -or $Value -cnotmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,7})?(?:Z|[+-]\d{2}:\d{2})$') { return $false }
    $parsed=[DateTimeOffset]::MinValue
    return [DateTimeOffset]::TryParse($Value,[Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::None,[ref]$parsed)
}
function Test-MetaJournalIdentity($Identity,[switch]$Service) {
    if ($Identity -isnot [Collections.IDictionary] -or
        -not $Identity.Contains('ProcessId') -or -not $Identity.Contains('StartedUTC')) { return $false }
    if (-not (Test-MetaJournalInteger $Identity.ProcessId) -or
        $Identity.ProcessId -le 0 -or $Identity.ProcessId -gt [int]::MaxValue -or
        -not (Test-MetaJournalTimestamp $Identity.StartedUTC)) { return $false }
    if ($Service -and (-not $Identity.Contains('Executable') -or
        $Identity.Executable -isnot [string] -or [string]::IsNullOrWhiteSpace($Identity.Executable) -or
        -not [IO.Path]::IsPathRooted($Identity.Executable))) { return $false }
    return $true
}
function Read-MetaAswJournal([string]$Path) {
    $json=Get-Content -Raw -LiteralPath $Path
    # PowerShell 7 unwraps one-element JSON arrays. Recovery records must be
    # objects, regardless of the JSON pipeline's enumeration behavior.
    if ([string]::IsNullOrWhiteSpace($json) -or -not $json.TrimStart().StartsWith('{')) {
        throw 'Unrecognized ASW recovery journal. Preserve it for review.'
    }
    $journal=$json | ConvertFrom-Json | ConvertTo-MetaJournalMap
    if ($journal -isnot [Collections.IDictionary] -or
        -not $journal.Contains('Schema') -or -not (Test-MetaJournalInteger $journal.Schema) -or $journal.Schema -ne 1 -or
        -not $journal.Contains('BeforeMode') -or $journal.BeforeMode -isnot [string] -or $journal.BeforeMode -cnotin @('auto','off') -or
        -not $journal.Contains('Status') -or $journal.Status -isnot [string] -or
        $journal.Status -cnotin @('pending','restored','unchanged-off','external-change-preserved','service-changed-no-restore')) {
        throw 'Unrecognized ASW recovery journal. Preserve it for review.'
    }
    if ($journal.Status -ceq 'pending' -and (
        -not $journal.Contains('Owner') -or -not (Test-MetaJournalIdentity $journal.Owner) -or
        -not $journal.Contains('Service') -or -not (Test-MetaJournalIdentity $journal.Service -Service) -or
        -not $journal.Contains('Game') -or ($null -ne $journal.Game -and -not (Test-MetaJournalIdentity $journal.Game)))) {
        throw 'Incomplete or invalid ASW recovery identity. Preserve the journal for review.'
    }
    return $journal
}
function Complete-MetaAswGuard($Context,[scriptblock]$ServiceProvider={Get-MetaServiceIdentity}) {
    $journal=$Context.Journal
    if($journal.Status -ne 'pending'){return $journal}
    try {
        $now=& $ServiceProvider
        if(!(Test-MetaSameService $journal.Service $now)) {
            # A restarted service is a new owner's state. Never replay an old override into it.
            $journal.Status='service-changed-no-restore'
        } else {
            $current=Get-MetaAswMode $Context.Runner $Context.Events
            if($current -ne 'off') {
                # Someone changed the mode during play; retain their latest choice.
                $journal.Status='external-change-preserved'
            } elseif($journal.BeforeMode -eq 'auto') {
                if(!(Test-MetaSameService $journal.Service (& $ServiceProvider))){throw 'Meta runtime changed during ASW restoration; the saved mode was not applied to the replacement service.'}
                $null=Invoke-MetaAswChecked 'server:asw.Auto' $Context.Runner $Context.Events
                if((Get-MetaAswMode $Context.Runner $Context.Events) -ne 'auto'){throw 'Meta ASW restore did not take effect.'}
                $journal.Status='restored'
            } else {$journal.Status='unchanged-off'}
        }
        $journal.CompletedUTC=[DateTime]::UtcNow.ToString('o')
        Save-MetaAswJournal $Context.Path $journal
        return $journal
    } catch {
        $journal.LastRestoreError=$_.Exception.Message
        Save-MetaAswJournal $Context.Path $journal
        throw
    }
}
function Repair-MetaAswPending([string]$Path,[scriptblock]$Runner,
    [scriptblock]$ServiceProvider={Get-MetaServiceIdentity},[scriptblock]$ProcessAlive={param($i) Test-MetaProcessIdentity $i},$Events=$null) {
    if(!(Test-Path -LiteralPath $Path)){return $null}
    $journal=Read-MetaAswJournal $Path
    if($journal.Status -ne 'pending'){return $journal}
    if((& $ProcessAlive $journal.Owner) -or (& $ProcessAlive $journal.Game)) {
        throw 'The earlier Rift S launcher/game is still running. Its ASW setting has not been changed.'
    }
    $context=[pscustomobject]@{Path=$Path;Journal=$journal;Runner=$Runner;Events=$Events}
    Complete-MetaAswGuard $context $ServiceProvider
}
function Start-MetaAswGuard([string]$Path,[scriptblock]$Runner,
    [scriptblock]$ServiceProvider={Get-MetaServiceIdentity},$Events=$null) {
    if(Test-Path -LiteralPath $Path) {
        $old=Read-MetaAswJournal $Path
        if($old.Status -eq 'pending'){throw 'An outstanding ASW recovery must be resolved before starting another game.'}
    }
    $service=& $ServiceProvider
    $before=Get-MetaAswMode $Runner $Events
    if(!(Test-MetaSameService $service (& $ServiceProvider))){throw 'Meta runtime changed during preparation. No ASW setting was changed.'}
    $owner=Get-Process -Id $PID
    $journal=[ordered]@{Schema=1;Status='pending';BeforeMode=$before;Service=$service;
        Owner=[ordered]@{ProcessId=$PID;StartedUTC=$owner.StartTime.ToUniversalTime().ToString('o')};Game=$null;
        CreatedUTC=[DateTime]::UtcNow.ToString('o');CompletedUTC=$null;LastRestoreError=$null}
    $context=[pscustomobject]@{Path=$Path;Journal=$journal;Runner=$Runner;Events=$Events}
    # Durable BEFORE changing the service. An interrupted launcher can recover next time.
    Save-MetaAswJournal $Path $journal
    try {
        if(!(Test-MetaSameService $service (& $ServiceProvider))){throw 'Meta runtime changed before ASW setup. The game will not start.'}
        if($before -ne 'off'){$null=Invoke-MetaAswChecked 'server:asw.Off' $Runner $Events}
        if((Get-MetaAswMode $Runner $Events) -ne 'off'){throw 'Meta ASW did not disable. The game will not start.'}
        return $context
    } catch {
        $failure=$_.Exception.Message
        try {$null=Complete-MetaAswGuard $context $ServiceProvider}
        catch {throw "$failure Restore also failed: $($_.Exception.Message). Recovery remains recorded at $Path"}
        throw $failure
    }
}
function Set-MetaAswGame($Context,[Diagnostics.Process]$Game) {
    $Context.Journal.Game=[ordered]@{ProcessId=$Game.Id;StartedUTC=$Game.StartTime.ToUniversalTime().ToString('o')}
    Save-MetaAswJournal $Context.Path $Context.Journal
}

# PowerShell 5.1 has no ConvertFrom-Json -AsHashtable. Keep the journal mutable.
function ConvertTo-MetaJournalMap {
    param([Parameter(ValueFromPipeline=$true)]$Value)
    process {
        if ($Value -is [Management.Automation.PSCustomObject]) {
            $map = [ordered]@{}
            foreach ($property in $Value.PSObject.Properties) { $map[$property.Name] = ConvertTo-MetaJournalMap $property.Value }
            return $map
        }
        if ($Value -is [Array]) { return ,@($Value | ForEach-Object { ConvertTo-MetaJournalMap $_ }) }
        return $Value
    }
}
function New-MetaAswRunner([string]$DiagnosticCLI) {
    if (-not [IO.Path]::IsPathRooted($DiagnosticCLI) -or -not (Test-Path -LiteralPath $DiagnosticCLI -PathType Leaf)) {
        throw 'The selected installed Meta diagnostic tool is missing.'
    }
    return { param($command) Invoke-MetaAswCLI $command $DiagnosticCLI }.GetNewClosure()
}
