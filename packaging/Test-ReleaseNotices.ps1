#requires -Version 5.1
<#
.SYNOPSIS
Checks the recorded release notices without modifying files or starting software.
.DESCRIPTION
This is a file-integrity gate only. It cannot establish legal clearance, complete
corresponding source, or the exact versions/licenses of code linked into a binary.
With -SourceRoot it also checks the recorded 15 Dear ImGui source blobs and notice,
plus the audited local-source subset; unresolved references remain explicit.
#>
[CmdletBinding()]
param(
    [string]$Root=(Split-Path -Parent $PSScriptRoot),
    [string]$SourceRoot
)
$ErrorActionPreference='Stop'

function Get-NoticeRoot([string]$Path) {
    if([string]::IsNullOrWhiteSpace($Path)){throw 'A notice root directory is required.'}
    # Resolve relative roots from PowerShell's current location, which can differ
    # from the process working directory after Set-Location or Push-Location.
    $item=Get-Item -LiteralPath $Path -Force
    if($item -isnot [IO.DirectoryInfo] -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Notice root must be a real directory, not a link: $Path"
    }
    return $item.FullName
}
function Assert-NoticeLeaf([string]$Name) {
    if([string]::IsNullOrWhiteSpace($Name) -or $Name -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$' -or
       $Name.EndsWith('.') -or $Name -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') {
        throw "Unsafe notice filename: $Name"
    }
}
function Get-NoticeFile([string]$Base,[string]$Relative) {
    if([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative)){throw 'Expected a relative notice path.'}
    $parts=$Relative -split '[/\\]'
    $current=$Base
    for($i=0;$i -lt $parts.Count;$i++) {
        Assert-NoticeLeaf $parts[$i]
        $current=Join-Path $current $parts[$i]
        $item=Get-Item -LiteralPath $current -Force
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked notice/source paths are not accepted: $current"}
        if($i -lt $parts.Count-1 -and !$item.PSIsContainer){throw "Expected directory: $current"}
    }
    if($item.PSIsContainer){throw "Expected notice/source file: $current"}
    return $item
}
function Read-NoticeJson([IO.FileInfo]$File) {
    if($File.Length -le 0 -or $File.Length -gt 1048576){throw "Notice JSON size is invalid: $($File.FullName)"}
    return Get-Content -LiteralPath $File.FullName -Raw | ConvertFrom-Json
}
function Get-NoticeGitBlobHash([IO.FileInfo]$File,[switch]$NormalizeCheckoutCRLF) {
    if($File.Length -gt 16777216){throw "Unexpectedly large vendored source: $($File.FullName)"}
    $sha=[Security.Cryptography.SHA1]::Create()
    $stream=$null
    try {
        if($NormalizeCheckoutCRLF) {
            # Git stores these text source blobs with LF. A Windows checkout may
            # expand LF to CRLF. Preserve every other byte, including bare CR.
            # Latin-1 is a reversible one-byte mapping, not a source-code decode.
            $oneByte=[Text.Encoding]::GetEncoding(28591)
            $bytes=[IO.File]::ReadAllBytes($File.FullName)
            $bytes=$oneByte.GetBytes($oneByte.GetString($bytes).Replace("`r`n","`n"))
            $header=[Text.Encoding]::ASCII.GetBytes(('blob '+$bytes.Length.ToString([Globalization.CultureInfo]::InvariantCulture)+[char]0))
            $null=$sha.TransformBlock($header,0,$header.Length,$header,0)
            $null=$sha.TransformFinalBlock($bytes,0,$bytes.Length)
            return ([BitConverter]::ToString($sha.Hash)).Replace('-','').ToLowerInvariant()
        }
        $header=[Text.Encoding]::ASCII.GetBytes(('blob '+$File.Length.ToString([Globalization.CultureInfo]::InvariantCulture)+[char]0))
        $null=$sha.TransformBlock($header,0,$header.Length,$header,0)
        $buffer=New-Object byte[] 65536
        $stream=[IO.File]::OpenRead($File.FullName)
        while(($read=$stream.Read($buffer,0,$buffer.Length)) -gt 0){$null=$sha.TransformBlock($buffer,0,$read,$buffer,0)}
        $null=$sha.TransformFinalBlock([byte[]]@(),0,0)
        return ([BitConverter]::ToString($sha.Hash)).Replace('-','').ToLowerInvariant()
    } finally {
        if($stream){$stream.Dispose()}; $sha.Dispose()
    }
}

$checkedRoot=Get-NoticeRoot $Root
$manifestFile=Get-NoticeFile $checkedRoot 'Docs/licenses/SHA256.json'
$manifest=Read-NoticeJson $manifestFile
$entries=@($manifest.files)
if($entries.Count -eq 0 -or $entries.Count -gt 128){throw 'Notice manifest must contain 1 through 128 files.'}
$required=@($manifest.RequiredFiles)
if($required.Count -eq 0 -or $required.Count -gt 128){throw 'Notice manifest RequiredFiles is missing or invalid.'}
$requiredSet=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach($name in $required) {
    if($name -isnot [string]){throw 'RequiredFiles must contain filenames, not objects.'}
    Assert-NoticeLeaf $name
    if(!$requiredSet.Add($name)){throw "Duplicate required notice: $name"}
}
# This floor prevents silently dropping known obligations from RequiredFiles.
# Newly audited components are added through the manifest's RequiredFiles list.
$critical=@('OpenXR-Apache-2.0.txt','GCC-GPL-3.0.txt','GCC-Runtime-Library-Exception-3.1.txt',
    'winpthreads-COPYING.txt','MinGW-w64-runtime-notices.txt','SDL2-LICENSE.txt','SDL2_net-LICENSE.txt',
    'zlib-LICENSE.txt','Dear-ImGui-LICENSE.txt','Dear-ImGui-SOURCE.json','BarlowCondensed-OFL.txt','RussoOne-OFL.txt')
foreach($name in $critical){if(!$requiredSet.Contains($name)){throw "Known critical notice omitted from RequiredFiles: $name"}}
$seen=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach($entry in $entries) {
    if($entry.file -isnot [string]){throw 'Each notice manifest entry requires a filename.'}
    Assert-NoticeLeaf $entry.file
    if(!$seen.Add($entry.file)){throw "Duplicate notice manifest entry: $($entry.file)"}
    if($entry.sha256 -isnot [string] -or $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$'){throw "Invalid SHA256 for notice: $($entry.file)"}
    if($null -eq $entry.bytes -or $entry.bytes -is [bool] -or $entry.bytes -is [string] -or
       $entry.bytes -le 0 -or $entry.bytes -gt 16777216 -or [decimal]$entry.bytes -ne [math]::Truncate([decimal]$entry.bytes)) {
        throw "Invalid byte count for notice: $($entry.file)"
    }
    $file=Get-NoticeFile $checkedRoot ('Docs/licenses/'+$entry.file)
    if($file.Length -ne $entry.bytes){throw "Notice byte count mismatch: $($entry.file)"}
    if((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -cne $entry.sha256.ToUpperInvariant()) {
        throw "Notice SHA256 mismatch: $($entry.file)"
    }
}
foreach($name in $required){if(!$seen.Contains($name)){throw "Required notice missing from manifest: $name"}}

$sourceChecked=$false; $sourceCount=0; $upstreamRevision=$null; $recordedSources=$null
if($SourceRoot) {
    $checkedSource=Get-NoticeRoot $SourceRoot
    $staged=Get-NoticeFile $checkedRoot 'Docs/licenses/Dear-ImGui-LICENSE.txt'
    $vendored=Get-NoticeFile $checkedSource 'Src/Pkgs/imgui/LICENSE.txt'
    if($vendored.Length -ne $staged.Length -or
       (Get-FileHash -LiteralPath $vendored.FullName -Algorithm SHA256).Hash -ne
       (Get-FileHash -LiteralPath $staged.FullName -Algorithm SHA256).Hash){throw 'Vendored ImGui license differs from the staged notice.'}
    $sourceRecord=Read-NoticeJson (Get-NoticeFile $checkedRoot 'Docs/licenses/Dear-ImGui-SOURCE.json')
    if($sourceRecord.verifiedUpstreamRevision -notmatch '^[0-9a-fA-F]{40}$' -or
       $sourceRecord.licenseGitBlobSHA -notmatch '^[0-9a-fA-F]{40}$'){throw 'Invalid ImGui source revision/license identity.'}
    if((Get-NoticeGitBlobHash $vendored) -ne $sourceRecord.licenseGitBlobSHA){throw 'ImGui license Git blob identity mismatch.'}
    $expectedNames=@('imconfig.h','imgui.cpp','imgui.h','imgui_draw.cpp','imgui_impl_opengl3.cpp','imgui_impl_opengl3.h',
        'imgui_impl_opengl3_loader.h','imgui_impl_sdl2.cpp','imgui_impl_sdl2.h','imgui_internal.h','imgui_tables.cpp',
        'imgui_widgets.cpp','imstb_rectpack.h','imstb_textedit.h','imstb_truetype.h')
    $sourceEntries=@($sourceRecord.matchingVendorFiles)
    if($sourceEntries.Count -ne $expectedNames.Count){throw 'Expected exactly 15 recorded ImGui source/header files.'}
    $sourceSeen=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in $sourceEntries) {
        if($entry.vendoredPath -isnot [string] -or $entry.vendoredPath -cnotmatch '^Src/Pkgs/imgui/([A-Za-z0-9_.-]+)$') {
            throw 'Unexpected ImGui vendored path.'
        }
        $leaf=$entry.vendoredPath.Substring('Src/Pkgs/imgui/'.Length)
        if($leaf -cnotin $expectedNames -or !$sourceSeen.Add($leaf)){throw "Unexpected or duplicate ImGui source: $leaf"}
        if($entry.gitBlobSHA -isnot [string] -or $entry.gitBlobSHA -notmatch '^[0-9a-fA-F]{40}$' -or
           $entry.exactGitBlobMatch -ne $true){throw "Invalid recorded ImGui identity: $leaf"}
        $file=Get-NoticeFile $checkedSource $entry.vendoredPath
        if((Get-NoticeGitBlobHash $file -NormalizeCheckoutCRLF) -ne $entry.gitBlobSHA){throw "ImGui source differs from recorded upstream blob: $leaf"}
    }
    $sourceChecked=$true; $sourceCount=$sourceEntries.Count; $upstreamRevision=$sourceRecord.verifiedUpstreamRevision
    $sourceChecker=Get-NoticeFile $PSScriptRoot 'Test-VendoredSourceIdentities.ps1'
    $recordedSources=& $sourceChecker.FullName -ManifestPath $manifestFile.FullName -SourceRoot $checkedSource -Scope AuditedSubset
    if(!$recordedSources.Passed){throw 'Audited recorded-source verification did not complete.'}
}
[pscustomobject]@{
    Passed=$true; NoticeCheckOnly=$true; ReleaseReady=$false; Root=$checkedRoot;
    ManifestSHA256=(Get-FileHash -LiteralPath $manifestFile.FullName -Algorithm SHA256).Hash;
    VerifiedNoticeFiles=$entries.Count; RequiredNoticeFiles=$required.Count;
    ImGuiSourceChecked=$sourceChecked; ImGuiSourceBlobs=$sourceCount; ImGuiUpstreamRevision=$upstreamRevision;
    ImGuiSourceHashNormalization='CRLF to LF for the 15 recorded source/header files only. Every other byte, including bare CR, is preserved. Notice and license hashes use raw bytes.';
    RecordedSourceSubsetChecked=($null -ne $recordedSources);
    VerifiedRecordedSourceReferences=if($recordedSources){$recordedSources.VerifiedReferences}else{0};
    PendingRecordedSourceReferences=if($recordedSources){$recordedSources.PendingReferences}else{0};
    AllRecordedSourcesVerified=if($recordedSources){$recordedSources.AllRecordedSourcesVerified}else{$false};
    RecordedSourceFindings=if($recordedSources){@($recordedSources.Findings)}else{@()};
    RecordedSourcePolicySHA256=if($recordedSources){$recordedSources.PolicySHA256}else{$null};
    RecordedSourceHashNormalization=if($recordedSources){$recordedSources.SourceHashNormalization}else{$null};
    FilesModified=$false; CorrespondingSourceVerified=$false; LinkedBinaryProvenanceVerified=$false;
    Limits='Only recorded notice integrity and optional ImGui/audited local-source identities were checked. Pending source differences remain unresolved. Complete corresponding source, exact linked source/version auditing, final package contents, artwork rights and release validation remain separate requirements.'
}
