#requires -Version 5.1
<#
.SYNOPSIS
Checks local source references against recorded notice hashes without changing files.
.DESCRIPTION
All is strict: unresolved references fail. AuditedSubset explicitly checks the
reviewed subset and pins known unresolved differences against silent drift.
The policy is a review record, not an alternative license/source provenance.
Remote URLs and hashes embedded in prose are not fetched or authenticated.
#>
[CmdletBinding()]
param(
    [string]$ManifestPath=(Join-Path (Split-Path -Parent $PSScriptRoot) 'Docs/licenses/SHA256.json'),
    [string]$SourceRoot=(Split-Path -Parent $PSScriptRoot),
    [string]$PolicyPath=(Join-Path $PSScriptRoot 'VendoredSourcePolicy.json'),
    [ValidateSet('All','AuditedSubset')][string]$Scope='All'
)
$ErrorActionPreference='Stop'
function Read-VendoredJson([string]$Path) {
    $item=Get-Item -LiteralPath $Path -Force
    if($item -isnot [IO.FileInfo] -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
       $item.Length -le 0 -or $item.Length -gt 1048576){throw 'Expected a bounded ordinary JSON file.'}
    $decoder=New-Object Text.UTF8Encoding($false,$true)
    $text=$decoder.GetString([IO.File]::ReadAllBytes($item.FullName))
    if($text.Length -gt 0 -and $text[0] -eq [char]0xfeff){$text=$text.Substring(1)}
    if(!$text.TrimStart().StartsWith('{')){throw 'Expected a JSON object.'}
    $value=$text | ConvertFrom-Json
    if($value -isnot [pscustomobject]){throw 'Expected a JSON object.'}
    return $value
}
function Assert-VendoredLeaf($Name) {
    if($Name -isnot [string] -or $Name -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*\z' -or
       $Name.EndsWith('.') -or $Name -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') {
        throw 'Invalid source/notice path component.'
    }
}
function Get-VendoredRelativePath($Path) {
    if($Path -isnot [string] -or !$Path.StartsWith('Supermodel/Src/',[StringComparison]::Ordinal)) {
        throw 'Local source paths require the exact historical Supermodel/Src/ prefix.'
    }
    $relative=$Path.Substring('Supermodel/'.Length)
    foreach($part in $relative.Split('/')){Assert-VendoredLeaf $part}
    return $relative
}
function Assert-VendoredHash($Value) {
    if($Value -isnot [string] -or $Value -cnotmatch '^[0-9a-fA-F]{64}\z'){throw 'Invalid recorded source SHA256.'}
}
function Get-VendoredFile([string]$Base,[string]$Relative) {
    $current=$Base
    $parts=$Relative.Split('/')
    for($i=0;$i -lt $parts.Length;$i++) {
        $current=Join-Path $current $parts[$i]
        $item=Get-Item -LiteralPath $current -Force
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked source paths are not accepted.'}
        if($item.Name -cne $parts[$i]){throw 'Source path casing differs from the recorded path.'}
        if($i -lt $parts.Length-1 -and $item -isnot [IO.DirectoryInfo]){throw 'Expected a source directory.'}
    }
    if($item -isnot [IO.FileInfo] -or $item.Length -gt 16777216){throw 'Expected a bounded source file.'}
    return $item
}
function Get-VendoredCanonicalHash([IO.FileInfo]$File) {
    # The recorded hashes describe Windows CRLF bytes. Latin-1 is a reversible
    # one-byte mapping: only paired CRLF and bare LF are converted. Lone CR,
    # BOM, non-ASCII bytes, whitespace and the final newline remain significant.
    $oneByte=[Text.Encoding]::GetEncoding(28591)
    $bytes=[IO.File]::ReadAllBytes($File.FullName)
    $bytes=$oneByte.GetBytes($oneByte.GetString($bytes).Replace("`r`n","`n").Replace("`n","`r`n"))
    $sha=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','')}
    finally{$sha.Dispose()}
}
$sourceDirectory=Get-Item -LiteralPath $SourceRoot -Force
if($sourceDirectory -isnot [IO.DirectoryInfo] -or ($sourceDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'SourceRoot must be an ordinary source directory.'
}
$manifest=Read-VendoredJson $ManifestPath
$policy=Read-VendoredJson $PolicyPath
if(($policy.schema -isnot [int] -and $policy.schema -isnot [long]) -or $policy.schema -ne 1 -or
   $policy.normalization -isnot [string] -or $policy.normalization -cne 'canonical-crlf' -or
   $policy.verified -isnot [array] -or $policy.pending -isnot [array] -or
   $policy.verified.Count -lt 1 -or ($policy.verified.Count+$policy.pending.Count) -gt 256) {
    throw 'Unsupported recorded-source policy schema or inventory.'
}
$expected=New-Object 'Collections.Generic.Dictionary[string,object]' ([StringComparer]::Ordinal)
$policyKeys=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach($group in @('verified','pending')) {
    foreach($entry in $policy.$group) {
        if($entry -isnot [pscustomobject]){throw 'Invalid source policy entry.'}
        Assert-VendoredLeaf $entry.notice
        $relative=Get-VendoredRelativePath $entry.path
        $key=$entry.notice+'|'+$entry.path
        if(!$policyKeys.Add($key)){throw 'Duplicate source policy reference.'}
        if($group -ceq 'pending') {
            Assert-VendoredHash $entry.recordedSHA256
            Assert-VendoredHash $entry.publicCanonicalSHA256
            if($entry.recordedSHA256 -ieq $entry.publicCanonicalSHA256 -or
               $entry.reason -isnot [string] -or [string]::IsNullOrWhiteSpace($entry.reason)) {
                throw 'A pending policy entry must identify an unresolved difference and its reason.'
            }
        }
        $expected.Add($key,[pscustomobject]@{Kind=$group;Entry=$entry;Relative=$relative})
    }
}
if($manifest.files -isnot [array] -or $manifest.files.Count -lt 1 -or $manifest.files.Count -gt 128){throw 'Invalid notice source inventory.'}
$seen=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$notices=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$pathHashes=New-Object 'Collections.Generic.Dictionary[string,string]' ([StringComparer]::OrdinalIgnoreCase)
$local=New-Object 'Collections.Generic.List[object]'
$remoteCount=0
foreach($notice in $manifest.files) {
    if($notice -isnot [pscustomobject]){throw 'Invalid notice source entry.'}
    Assert-VendoredLeaf $notice.file
    if(!$notices.Add($notice.file)){throw 'Duplicate notice source entry.'}
    if($null -eq $notice.PSObject.Properties['sources']){continue}
    if($notice.sources -isnot [array] -or $notice.sources.Count -lt 1 -or $notice.sources.Count -gt 128){throw 'Invalid sources array.'}
    foreach($source in $notice.sources) {
        if($source -isnot [pscustomobject]){throw 'Invalid source reference.'}
        $hasPath=$null -ne $source.PSObject.Properties['path']
        $hasUrl=$null -ne $source.PSObject.Properties['url']
        if($hasPath -eq $hasUrl){throw 'A source reference needs exactly one local path or remote URL.'}
        if($hasUrl) {
            $uri=$null
            if($source.url -isnot [string] -or ![Uri]::TryCreate($source.url,[UriKind]::Absolute,[ref]$uri) -or
               $uri.Scheme -cne 'https' -or $uri.UserInfo){throw 'Invalid remote source reference; no URL is fetched.'}
            if($null -ne $source.PSObject.Properties['sha256']){Assert-VendoredHash $source.sha256}
            elseif($null -ne $source.PSObject.Properties['header_sha256']){Assert-VendoredHash $source.header_sha256}
            else{throw 'Remote reference has no recorded hash.'}
            $remoteCount++; continue
        }
        $relative=Get-VendoredRelativePath $source.path
        Assert-VendoredHash $source.sha256
        $key=$notice.file+'|'+$source.path
        if(!$seen.Add($key)){throw 'Duplicate local source reference.'}
        if(!$expected.ContainsKey($key)){throw "Unreviewed local source reference: $($source.path)"}
        if($pathHashes.ContainsKey($relative) -and $pathHashes[$relative] -ine $source.sha256){throw 'Conflicting hashes for a shared local source.'}
        $pathHashes[$relative]=$source.sha256
        $local.Add([pscustomobject]@{Key=$key;Source=$source;Notice=$notice.file;Relative=$relative})
    }
}
if($seen.Count -ne $expected.Count){throw 'Recorded local source inventory is incomplete; policy references are missing.'}
$findings=New-Object 'Collections.Generic.List[object]'
$verifiedCount=0
foreach($reference in $local) {
    $selection=$expected[$reference.Key]
    $file=Get-VendoredFile $sourceDirectory.FullName $reference.Relative
    $actual=Get-VendoredCanonicalHash $file
    $recorded=$reference.Source.sha256.ToUpperInvariant()
    if($selection.Kind -ceq 'verified') {
        if($actual -cne $recorded){throw "Recorded source SHA256 mismatch: $($reference.Relative). Investigate; do not refresh the recorded hash automatically."}
        $verifiedCount++
    } else {
        $pending=$selection.Entry
        if($recorded -ine $pending.recordedSHA256){throw "Pending recorded hash changed: $($reference.Relative). Review required."}
        if($actual -ine $pending.publicCanonicalSHA256){throw "Pending public source identity changed: $($reference.Relative). Review required."}
        $findings.Add([pscustomobject]@{Path=$reference.Relative;Notice=$reference.Notice;Status='UnresolvedRecordedSourceMismatch';RecordedSHA256=$recorded;PublicCanonicalSHA256=$actual;Reason=$pending.reason})
    }
}
if($Scope -eq 'All' -and $findings.Count -gt 0) {
    throw ('Unresolved recorded source identities: '+(($findings | ForEach-Object {$_.Path}) -join ', ')+'. Strict verification failed; recorded hashes were not changed.')
}
[pscustomobject]@{
    Passed=$true; Scope=$Scope; LocalSourceReferencesOnly=$true;
    SourceReferences=$local.Count; UniqueSourceFiles=$pathHashes.Count; VerifiedReferences=$verifiedCount;
    PendingReferences=$findings.Count; AllRecordedSourcesVerified=($findings.Count -eq 0); Findings=$findings.ToArray();
    RemoteReferencesNotChecked=$remoteCount;
    ManifestSHA256=(Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash;
    PolicySHA256=(Get-FileHash -LiteralPath $PolicyPath -Algorithm SHA256).Hash;
    SourceHashNormalization='Historical Supermodel/ prefix removed; byte-preserving CRLF-to-LF then LF-to-CRLF conversion only. Bare CR, BOM and every other byte remain significant.';
    FilesModified=$false; UpstreamProvenanceVerified=$false; CorrespondingSourceVerified=$false;
    LinkedBinaryProvenanceVerified=$false; ReleaseReady=$false;
    Limits='Checks only the selected recorded local file identities and the declared unresolved-difference identities. Policy and manifest require review; matching hashes do not authenticate provenance, confer legal clearance, establish full corresponding source, or validate a VR build.'
}
