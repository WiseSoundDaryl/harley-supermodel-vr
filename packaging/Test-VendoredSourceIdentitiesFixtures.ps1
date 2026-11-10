#requires -Version 5.1
<#
.SYNOPSIS
Exercises recorded-source verification with disposable synthetic files.
.DESCRIPTION
No real vendor source, license record, ROM, runtime or provenance is fabricated.
Expected digests describe only tiny test strings. Every checker invocation is
surrounded by a byte/metadata snapshot; only this harness's owned fixtures change.
#>
[CmdletBinding()]
param([string]$TemporaryRoot = [IO.Path]::GetTempPath())
$ErrorActionPreference='Stop'
$checker=Join-Path $PSScriptRoot 'Test-VendoredSourceIdentities.ps1'
$checkerHash=(Get-FileHash -LiteralPath $checker -Algorithm SHA256).Hash
$parent=Get-Item -LiteralPath $TemporaryRoot -Force
if ($parent -isnot [IO.DirectoryInfo] -or ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'TemporaryRoot must be an ordinary directory.' }
$token=[Guid]::NewGuid().ToString('N')
$fixture=Join-Path $parent.FullName ('HarleyVR-vendored-identities-'+$token)
$marker=Join-Path $fixture '.fixture-owner'
$source=Join-Path $fixture 'source'
$manifestPath=Join-Path $fixture 'manifest.json'
$policyPath=Join-Path $fixture 'policy.json'
$checks=0; $cases=New-Object 'Collections.Generic.List[string]'; $created=$false
$encoding=New-Object Text.UTF8Encoding($false)
function Assert-Fixture([bool]$Condition,[string]$Message) {
    $script:checks++
    if (-not $Condition) { throw ('Vendored identity fixture failed: '+$Message) }
}
function Fixture-Path([string]$Relative) {
    $path=[IO.Path]::GetFullPath((Join-Path $fixture $Relative))
    if (-not $path.StartsWith($fixture+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture path escaped its owned directory.' }
    return $path
}
function Write-Fixture([string]$Relative,[byte[]]$Bytes) {
    $path=Fixture-Path $Relative
    $directory=Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $directory)) { $null=New-Item -ItemType Directory -Path $directory }
    [IO.File]::WriteAllBytes($path,$Bytes)
}
function Source-Bytes([string]$Text) { return ,$encoding.GetBytes($Text) }
function Canonical-FixtureBytes([byte[]]$Bytes) {
    # Independent byte-state oracle; no text decoding or production helper reuse.
    $output=New-Object 'Collections.Generic.List[byte]'
    for($i=0;$i -lt $Bytes.Length;$i++) {
        if($Bytes[$i] -eq 13 -and $i+1 -lt $Bytes.Length -and $Bytes[$i+1] -eq 10) {
            $output.Add(13); $output.Add(10); $i++
        } elseif($Bytes[$i] -eq 10) { $output.Add(13); $output.Add(10) }
        else { $output.Add($Bytes[$i]) }
    }
    return ,$output.ToArray()
}
function Fixture-Digest([byte[]]$Bytes) {
    $hash=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($Bytes)).Replace('-','') }
    finally { $hash.Dispose() }
}
function Source-Digest([byte[]]$Bytes) { return Fixture-Digest (Canonical-FixtureBytes $Bytes) }
function Save-Records {
    [IO.File]::WriteAllText($manifestPath,($script:manifest | ConvertTo-Json -Depth 12),$encoding)
    [IO.File]::WriteAllText($policyPath,($script:policy | ConvertTo-Json -Depth 12),$encoding)
}
function Reset-Fixture {
    $script:aBytes=Source-Bytes "// synthetic A`nint a = 1;`n"
    $script:bBytes=Source-Bytes "// synthetic B`nint b = 2;`n"
    $script:pendingBytes=Source-Bytes "// synthetic public pending version`n"
    Write-Fixture 'source/Src/Synthetic/A.cpp' $aBytes
    Write-Fixture 'source/Src/Synthetic/B.h' $bBytes
    Write-Fixture 'source/Src/Synthetic/Pending.cpp' $pendingBytes
    $script:manifest=[ordered]@{files=@(
        [ordered]@{file='Synthetic-A-NOTICE.txt';sources=@([ordered]@{path='Supermodel/Src/Synthetic/A.cpp';sha256=(Source-Digest $aBytes)})},
        [ordered]@{file='Synthetic-B-NOTICE.txt';sources=@([ordered]@{path='Supermodel/Src/Synthetic/B.h';sha256=(Source-Digest $bBytes)})},
        [ordered]@{file='Synthetic-Shared-NOTICE.txt';sources=@([ordered]@{path='Supermodel/Src/Synthetic/A.cpp';sha256=(Source-Digest $aBytes)})},
        [ordered]@{file='Synthetic-Pending-NOTICE.txt';sources=@([ordered]@{path='Supermodel/Src/Synthetic/Pending.cpp';sha256=(Source-Digest (Source-Bytes "// synthetic historic pending version`n"))})},
        [ordered]@{file='Synthetic-Remote-NOTICE.txt';sources=@([ordered]@{url='https://example.invalid/synthetic-only';sha256=('A'*64)})}
    )}
    $script:policy=[ordered]@{schema=1;normalization='canonical-crlf';verified=@(
        [ordered]@{notice='Synthetic-A-NOTICE.txt';path='Supermodel/Src/Synthetic/A.cpp'},
        [ordered]@{notice='Synthetic-B-NOTICE.txt';path='Supermodel/Src/Synthetic/B.h'},
        [ordered]@{notice='Synthetic-Shared-NOTICE.txt';path='Supermodel/Src/Synthetic/A.cpp'}
    );pending=@([ordered]@{notice='Synthetic-Pending-NOTICE.txt';path='Supermodel/Src/Synthetic/Pending.cpp';recordedSHA256=$manifest.files[3].sources[0].sha256;publicCanonicalSHA256=(Source-Digest $pendingBytes);reason='Synthetic known mismatch; no upstream provenance is asserted.'})}
    Save-Records
}
function Snapshot-Fixture {
    $snapshot=New-Object 'Collections.Generic.List[string]'
    foreach($item in (Get-ChildItem -LiteralPath $fixture -Recurse -Force | Sort-Object FullName)) {
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unexpected linked fixture path.' }
        $relative=$item.FullName.Substring($fixture.Length)
        if($item.PSIsContainer) { $snapshot.Add('D:'+ $relative) }
        else { $snapshot.Add('F:'+$relative+':'+$item.Length+':'+$item.LastWriteTimeUtc.Ticks+':'+(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash) }
    }
    return $snapshot -join "`n"
}
function Case([string]$Name,[scriptblock]$Mutation,[bool]$ExpectedSuccess=$false,[string]$Scope='AuditedSubset',[int]$Pending=1) {
    Reset-Fixture
    & $Mutation
    $before=Snapshot-Fixture; $result=$null; $failure=$null
    try {
        if($Scope -ceq 'Default') { $result=& $checker -ManifestPath $manifestPath -SourceRoot $source -PolicyPath $policyPath }
        else { $result=& $checker -ManifestPath $manifestPath -SourceRoot $source -PolicyPath $policyPath -Scope $Scope }
    } catch { $failure=$_.Exception.Message }
    Assert-Fixture ((Snapshot-Fixture) -ceq $before) ($Name+': checker leaves all fixture files and directories unchanged')
    if($ExpectedSuccess) {
        Assert-Fixture (-not $failure -and $result.Passed) ($Name+': expected success; '+$failure)
        Assert-Fixture ($result.VerifiedReferences -eq 3 -and $result.PendingReferences -eq $Pending) ($Name+': exact scoped counts')
        Assert-Fixture ($result.SourceReferences -eq (3+$Pending) -and $result.UniqueSourceFiles -eq (2+$Pending) -and
            $result.RemoteReferencesNotChecked -eq 1 -and $result.LocalSourceReferencesOnly -eq $true) ($Name+': local references, shared files and excluded remote count')
        Assert-Fixture ($result.AllRecordedSourcesVerified -eq ($Pending -eq 0)) ($Name+': full-source status is explicit')
        Assert-Fixture ($result.FilesModified -eq $false -and $result.ReleaseReady -eq $false -and
            $result.CorrespondingSourceVerified -eq $false -and $result.LinkedBinaryProvenanceVerified -eq $false -and
            $result.UpstreamProvenanceVerified -eq $false) ($Name+': no provenance, build or release claim')
        Assert-Fixture (@($result.Findings).Count -eq $Pending) ($Name+': unresolved findings are visible')
    } else { Assert-Fixture (-not [string]::IsNullOrWhiteSpace($failure)) ($Name+': expected rejection') }
    $cases.Add($Name)
}
try {
    $null=New-Item -ItemType Directory -Path $fixture
    $created=$true
    [IO.File]::WriteAllText($marker,$token,$encoding)
    Case 'Audited subset accepts LF checkout, shared source and explicit pending finding' {} $true
    Case 'CRLF checkout is the same recorded source identity' {Write-Fixture 'source/Src/Synthetic/A.cpp' (Canonical-FixtureBytes $aBytes)} $true
    Case 'Mixed LF and CRLF checkout is accepted without other normalization' {Write-Fixture 'source/Src/Synthetic/A.cpp' (Source-Bytes "// synthetic A`r`nint a = 1;`n")} $true
    Case 'Lowercase valid SHA256 is accepted' {$manifest.files[0].sources[0].sha256=$manifest.files[0].sources[0].sha256.ToLowerInvariant();Save-Records} $true
    Case 'UTF8 BOM on manifest JSON is accepted without changing source bytes' {[IO.File]::WriteAllBytes($manifestPath,([byte[]](@(239,187,191)+@([IO.File]::ReadAllBytes($manifestPath)))))} $true
    Case 'UTF8 BOM on policy JSON is accepted without changing source bytes' {[IO.File]::WriteAllBytes($policyPath,([byte[]](@(239,187,191)+@([IO.File]::ReadAllBytes($policyPath)))))} $true
    Case 'Strict scope rejects the known unresolved record' {} $false 'All'
    Case 'Default scope is strict' {} $false 'Default'
    Case 'Strict scope succeeds only when every recorded local reference is verified' {$policy.pending=@();$manifest.files=@($manifest.files|Where-Object {$_.file -ne 'Synthetic-Pending-NOTICE.txt'});Save-Records} $true 'All' 0
    Case 'Default strict scope succeeds with complete synthetic identities' {$policy.pending=@();$manifest.files=@($manifest.files|Where-Object {$_.file -ne 'Synthetic-Pending-NOTICE.txt'});Save-Records} $true 'Default' 0
    Case 'Missing verified source fails' {Remove-Item -LiteralPath (Fixture-Path 'source/Src/Synthetic/A.cpp')}
    Case 'Missing pending source fails' {Remove-Item -LiteralPath (Fixture-Path 'source/Src/Synthetic/Pending.cpp')}
    Case 'Changed same-length source fails' {Write-Fixture 'source/Src/Synthetic/A.cpp' (Source-Bytes "// synthetic A`nint a = 9;`n")}
    Case 'CRLF conversion cannot hide changed source' {Write-Fixture 'source/Src/Synthetic/A.cpp' (Source-Bytes "// synthetic A`r`nint a = 9;`r`n")}
    Case 'Added UTF8 BOM is preserved and fails' {Write-Fixture 'source/Src/Synthetic/A.cpp' ([byte[]](@(239,187,191)+@($aBytes)))}
    Case 'Bare carriage return is preserved and fails' {Write-Fixture 'source/Src/Synthetic/A.cpp' (Source-Bytes "// synthetic A`rint a = 1;`n")}
    Case 'Trailing whitespace is preserved and fails' {Write-Fixture 'source/Src/Synthetic/A.cpp' (Source-Bytes "// synthetic A `nint a = 1;`n")}
    Case 'Changed case is preserved and fails' {Write-Fixture 'source/Src/Synthetic/A.cpp' (Source-Bytes "// Synthetic A`nint a = 1;`n")}
    Case 'Missing terminal newline is preserved and fails' {Write-Fixture 'source/Src/Synthetic/A.cpp' (Source-Bytes "// synthetic A`nint a = 1;")}
    Case 'Nontext byte is preserved and fails' {Write-Fixture 'source/Src/Synthetic/A.cpp' ([byte[]](@($aBytes)+@(255)))}
    Case 'Pending public source drift fails instead of inheriting an exception' {Write-Fixture 'source/Src/Synthetic/Pending.cpp' (Source-Bytes "// synthetic different public pending version`n")}
    Case 'Pending recorded hash substitution fails' {$manifest.files[3].sources[0].sha256='F'*64;Save-Records}
    Case 'Pending source cannot silently promote to historical recorded version' {Write-Fixture 'source/Src/Synthetic/Pending.cpp' (Source-Bytes "// synthetic historic pending version`n")}
    Case 'Pending record cannot claim identical public and recorded hashes' {$policy.pending[0].publicCanonicalSHA256=$policy.pending[0].recordedSHA256;Save-Records}
    Case 'Conflicting hashes for the same source across notices fail' {$manifest.files[2].sources[0].sha256='E'*64;Save-Records}
    foreach($badHash in @('short',('G'*64),('0'*63),('0'*65),('0'*64)+"`n",'', $null,42,$true,@('0'*64))) {
        $value=$badHash
        Case ('Malformed source SHA256 '+$cases.Count) {$manifest.files[0].sources[0].sha256=$value;Save-Records}
    }
    foreach($badPath in @('../outside.cpp','C:/outside.cpp','/outside.cpp','//server/share/outside.cpp',
        'Supermodel/Src/../outside.cpp','Supermodel/Src/Synthetic/../../outside.cpp','Supermodel/Src/Synthetic/A.cpp:stream',
        'Supermodel\Src\Synthetic\A.cpp','Src/Synthetic/A.cpp','supermodel/Src/Synthetic/A.cpp',
        'Supermodel/Src//Synthetic/A.cpp','Supermodel/Src/./Synthetic/A.cpp','Supermodel/Src/Synthetic/A.cpp.',
        'Supermodel/Src/Synthetic/CON.cpp','Supermodel/Src/Synthetic/NUL','Supermodel/Src/Synthetic/A*.cpp',
        'Supermodel/Src/Synthetic/A.cpp ','Supermodel/Src/Synthetic/%2e%2e/outside.cpp')) {
        $value=$badPath
        Case ('Invalid source path '+$cases.Count) {$manifest.files[0].sources[0].path=$value;$policy.verified[0].path=$value;Save-Records}
    }
    Case 'Null source path fails' {$manifest.files[0].sources[0].path=$null;Save-Records}
    Case 'Trailing LF cannot pass the source path component validator' {$manifest.files[0].sources[0].path+="`n";$policy.verified[0].path+="`n";Save-Records}
    Case 'Nonstring source path fails' {$manifest.files[0].sources[0].path=123;Save-Records}
    Case 'Removed local reference fails fixed inventory' {$manifest.files[0].sources=@();Save-Records}
    Case 'Additional local reference requires review' {$manifest.files[0].sources+= [ordered]@{path='Supermodel/Src/Synthetic/B.h';sha256=(Source-Digest $bBytes)};Save-Records}
    Case 'Duplicate local reference in one notice fails' {$manifest.files[0].sources+= $manifest.files[0].sources[0];Save-Records}
    Case 'Duplicate notice entry fails' {$manifest.files+= $manifest.files[0];Save-Records}
    Case 'Case-aliased duplicate notice fails' {$extra=[ordered]@{file='synthetic-a-notice.txt';sources=@($manifest.files[0].sources[0])};$manifest.files+= $extra;Save-Records}
    Case 'Duplicate policy pair fails' {$policy.verified+= $policy.verified[0];Save-Records}
    Case 'Same policy pair cannot be verified and pending' {$policy.pending+= [ordered]@{notice=$policy.verified[0].notice;path=$policy.verified[0].path;recordedSHA256=$manifest.files[0].sources[0].sha256;publicCanonicalSHA256='D'*64;reason='Synthetic overlap'};Save-Records}
    Case 'Policy cannot silently drop a pending reference' {$policy.pending=@();Save-Records}
    Case 'Unsupported normalization fails' {$policy.normalization='trim-whitespace';Save-Records}
    Case 'Array normalization cannot impersonate a string' {$policy.normalization=@('canonical-crlf');Save-Records}
    Case 'Nonstring normalization fails' {$policy.normalization=123;Save-Records}
    Case 'Null normalization fails' {$policy.normalization=$null;Save-Records}
    Case 'Unsupported policy schema fails' {$policy.schema=2;Save-Records}
    Case 'String policy schema fails' {$policy.schema='1';Save-Records}
    Case 'Empty verified policy fails' {$policy.verified=@();Save-Records}
    Case 'Verified policy object cannot impersonate an array' {$policy.verified=$policy.verified[0];Save-Records}
    Case 'Verified policy scalar array item fails' {$policy.verified=@('fabricated');Save-Records}
    Case 'Pending policy object cannot impersonate an array' {$policy.pending=$policy.pending[0];Save-Records}
    Case 'Null pending policy fails' {$policy.pending=$null;Save-Records}
    Case 'Manifest files object cannot impersonate an array' {$manifest.files=$manifest.files[0];Save-Records}
    Case 'Sources object cannot impersonate an array' {$manifest.files[0].sources=$manifest.files[0].sources[0];Save-Records}
    Case 'Null sources fails' {$manifest.files[0].sources=$null;Save-Records}
    Case 'Malformed pending recorded hash fails' {$policy.pending[0].recordedSHA256='bad';Save-Records}
    Case 'Malformed pending public hash fails' {$policy.pending[0].publicCanonicalSHA256='bad';Save-Records}
    Case 'Missing pending reason fails' {$policy.pending[0].Remove('reason');Save-Records}
    Case 'A local path cannot also claim a remote URL' {$manifest.files[0].sources[0].url='https://example.invalid/fabricated';Save-Records}
    Case 'Remote URL cannot substitute for a required local record' {$manifest.files[0].sources[0].Remove('path');$manifest.files[0].sources[0].url='https://example.invalid/fabricated';Save-Records}
    Case 'Nonobject local source reference fails' {$manifest.files[0].sources=@('fabricated');Save-Records}
    Case 'Manifest root array fails' {[IO.File]::WriteAllText($manifestPath,('['+($manifest|ConvertTo-Json -Depth 12)+']'),$encoding)}
    Case 'Policy root array fails' {[IO.File]::WriteAllText($policyPath,('['+($policy|ConvertTo-Json -Depth 12)+']'),$encoding)}
    Case 'Malformed manifest JSON fails' {[IO.File]::WriteAllText($manifestPath,'{bad',$encoding)}
    Case 'Malformed policy JSON fails' {[IO.File]::WriteAllText($policyPath,'{bad',$encoding)}
    Assert-Fixture ((Get-FileHash -LiteralPath $checker -Algorithm SHA256).Hash -ceq $checkerHash) 'checker file unchanged during test'
    $result=[pscustomobject]@{Passed=$true;Cases=$cases.Count;Checks=$checks;CaseNames=$cases.ToArray();SyntheticOnly=$true;
        FixturesRemoved=$false;FilesModified=$false;SourceCheckerSHA256=$checkerHash;PowerShellVersion=$PSVersionTable.PSVersion.ToString();
        GameOrRuntimeLaunched=$false;RealSourceOrNoticeModified=$false;UpstreamProvenanceVerified=$false;ReleaseReady=$false}
} finally {
    if($created -and (Test-Path -LiteralPath $fixture)) {
        $resolved=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $fixture).Path)
        $expected=[IO.Path]::GetFullPath((Join-Path $parent.FullName ('HarleyVR-vendored-identities-'+$token)))
        if($resolved -cne $expected -or [IO.File]::ReadAllText($marker) -cne $token) { throw 'Fixture ownership check failed; nothing removed.' }
        $items=@(Get-Item -LiteralPath $fixture -Force)+@(Get-ChildItem -LiteralPath $fixture -Recurse -Force)
        if(@($items|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count) { throw 'Fixture cleanup refused a linked path.' }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
$result.FixturesRemoved=$true
$result
