#requires -Version 5.1
<#
.SYNOPSIS
Checks the staged source tree for player data and distribution/build artifacts.
.DESCRIPTION
Reads Git index blobs, including files excluded by sparse checkout. It never
opens a ROM, launcher or runtime outside Git and never changes the checkout.
Path policy and recognizable binary signatures are preventive checks, not an
exhaustive copyright, secret, personal-data or Git-history audit.
#>
[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [switch]$ReportOnly
)
$ErrorActionPreference = 'Stop'
$rootItem = Get-Item -LiteralPath $Root -Force
if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Root must be an ordinary Git checkout directory.'
}
$checkedRoot = $rootItem.FullName.TrimEnd([IO.Path]::DirectorySeparatorChar)
$git = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source

function New-SafetyGit([string]$Arguments, [bool]$InputPipe = $false) {
    $si = New-Object System.Diagnostics.ProcessStartInfo
    $si.FileName = $git; $si.WorkingDirectory = $checkedRoot; $si.Arguments = $Arguments
    $si.UseShellExecute = $false; $si.CreateNoWindow = $true
    $si.RedirectStandardOutput = $true; $si.RedirectStandardError = $true
    $si.RedirectStandardInput = $InputPipe
    $si.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false)
    $p = New-Object System.Diagnostics.Process; $p.StartInfo = $si
    try {
        if ($InputPipe -and $si.PSObject.Properties['StandardInputEncoding']) {
            $si.StandardInputEncoding=New-Object Text.UTF8Encoding($false)
            $started=$p.Start()
        } elseif ($InputPipe) {
            # Framework creates and auto-flushes its stdin writer in Start.
            # It lacks StandardInputEncoding, so normalize only that creation
            # and restore the caller immediately, including on start failure.
            $savedInputEncoding=[Console]::InputEncoding
            try {
                [Console]::InputEncoding=New-Object Text.UTF8Encoding($false)
                $started=$p.Start()
            } finally { [Console]::InputEncoding=$savedInputEncoding }
        } else { $started=$p.Start() }
        if (-not $started) { throw 'Could not read Git source objects.' }
        return $p
    } catch { $p.Dispose(); throw }
}
function Read-SafetyGit([string]$Arguments) {
    $p = New-SafetyGit $Arguments
    try {
        $out = $p.StandardOutput.ReadToEndAsync(); $err = $p.StandardError.ReadToEndAsync()
        $p.WaitForExit(); $value = $out.GetAwaiter().GetResult(); $errors = $err.GetAwaiter().GetResult()
        if ($p.ExitCode -ne 0) { throw 'Git could not enumerate the staged source tree.' }
        return $value
    } finally { $p.Dispose() }
}
$actualRoot = (Read-SafetyGit 'rev-parse --show-toplevel').Trim()
if (-not [string]::Equals([IO.Path]::GetFullPath($actualRoot), $checkedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Root must be the top-level Git checkout directory.'
}
$entries = (Read-SafetyGit 'ls-files --stage -z').Split([char[]]@([char]0), [StringSplitOptions]::RemoveEmptyEntries)
if ($entries.Count -eq 0) { throw 'No tracked source files were found.' }
$findings = New-Object 'System.Collections.Generic.List[object]'
$checked = 0; [long]$bytesRead = 0; $permittedImages = 0
function Add-SafetyFinding([string]$Path, [string]$Reason) {
    $findings.Add([pscustomobject]@{Path=$Path;Reason=$Reason})
}
function Test-SafetyPath([string]$Path) {
    if ($Path -match '[\x00-\x1f\x7f\\:]' -or $Path.StartsWith('/') -or $Path -match '(^|/)\.\.?(/|$)') {
        return 'Unsafe or nonportable Git path'
    }
    if ($Path -match '(?i)(^|/)(roms?|saves?|nvram|diagnostics)(/|$)') {
        return 'Player ROM, save, NVRAM or runtime-diagnostic directory'
    }
    if ($Path -match '(?i)^(runs|reports|captures|profiles|pgo|private|dist|bin(?:32|64)?|obj(?:32|64)?)/') {
        return 'Private run, capture, profile, build or distribution directory'
    }
    if ($Path -match '(?i)\.(exe|dll|so(?:\.\d+)*|dylib|o|obj|a|lib|pdb|pch|ilk|exp|iobj|ipdb|gcda|gcno|profraw|profdata|pgc|pgd|etl|dmp|log|mp4|mkv|avi|zip|7z|rar|tar|gz|tgz|xz|bz2|pfx|p12)$') {
        return 'Compiled, profiling, capture or archive artifact'
    }
    if ($Path -match '(?i)\.(rom|bin|nv|nvram|st\d+|sav|state|save)(?:\.[^/]+)?$') {
        return 'ROM, save-state or NVRAM payload'
    }
    if ($Path -match '(?i)(^|/)(supermodel\.ini|harleyvr-menu\.ini|release-ready\.json|meta-asw-(pending|recovery)\.json|steamvr\.vrsettings|openvrpaths\.vrpath)$') {
        return 'Player configuration, recovery journal or player-release marker'
    }
    if ($Path -match '(?i)(^|/)config/.*\.(ini|cfg|json|xml)$' -and $Path -cnotin @('Config/Games.xml','Config/Music.xml')) {
        return 'Unreviewed runtime configuration; source keeps Games.xml and Music.xml only'
    }
    if ($Path -match '(?i)(^|/)(game-trace\.csv(?:\.meta\.json)?|offline-runtime(?:-resources)?\.(csv|json)|frame-timing\.csv)$') {
        return 'Private diagnostic capture'
    }
    if ($Path -match '(?i)(^|/)(id_rsa|id_dsa|id_ecdsa|id_ed25519)$' -or
        ($Path -match '(?i)(^|/)\.env(?:\.[^/]+)?$' -and $Path -notmatch '(?i)\.example$')) {
        return 'Private credential or environment file'
    }
    return $null
}
function Test-SafetySignature([byte[]]$Prefix) {
    $hex = [BitConverter]::ToString($Prefix)
    if ($hex -match '^(50-4B-(03-04|05-06|07-08)|1F-8B|37-7A-BC-AF-27-1C|52-61-72-21-1A-07|21-3C-61-72-63-68-3E-0A)') { return 'Archive signature, regardless of filename' }
    if ($hex -match '^(4D-5A|7F-45-4C-46|CA-FE-BA-BE|CE-FA-ED-FE|CF-FA-ED-FE|FE-ED-FA-CE|FE-ED-FA-CF|42-43-C0-DE|DE-C0-17-0B)') { return 'Executable/object signature, regardless of filename' }
    return $null
}
function Test-PermittedImage([string]$Path, [byte[]]$Prefix) {
    $hex = [BitConverter]::ToString($Prefix)
    # Reviewed source assets and documentation images; no blanket *.png exemption.
    if ($Path -cin @('Assets/p1crosshair.bmp','Assets/p2crosshair.bmp')) { return $hex.StartsWith('42-4D') }
    if ($Path -cnotmatch '^Docs/Images/[^/]+\.(?i:png|gif|jpg|jpeg|bmp|ico|webp)$') { return $false }
    switch -Regex ($Path) {
        '\.png$' { return $hex.StartsWith('89-50-4E-47-0D-0A-1A-0A') }
        '\.gif$' { return $hex.StartsWith('47-49-46-38-37-61') -or $hex.StartsWith('47-49-46-38-39-61') }
        '\.jpe?g$' { return $hex.StartsWith('FF-D8-FF') }
        '\.bmp$' { return $hex.StartsWith('42-4D') }
        '\.ico$' { return $hex.StartsWith('00-00-01-00') }
        '\.webp$' { return $hex.StartsWith('52-49-46-46') -and $Prefix.Length -ge 12 -and [Text.Encoding]::ASCII.GetString($Prefix,8,4) -ceq 'WEBP' }
    }
    return $false
}
function Read-SafetyHeader($Stream) {
    $line = New-Object 'System.Collections.Generic.List[byte]'
    while ($true) {
        $b = $Stream.ReadByte()
        if ($b -eq -1) { throw 'Git object stream ended unexpectedly.' }
        if ($b -eq 10) { break }
        $line.Add([byte]$b)
        if ($line.Count -gt 200) { throw 'Invalid Git object header.' }
    }
    return [Text.Encoding]::ASCII.GetString($line.ToArray())
}
# Replacement refs must not substitute harmless content for the indexed blob.
$batch = New-SafetyGit '--no-replace-objects cat-file --batch' $true
$errorTask = $batch.StandardError.ReadToEndAsync()
try {
    $stream = $batch.StandardOutput.BaseStream
    $inputStream = $batch.StandardInput.BaseStream
    $buffer = New-Object byte[] 65536
    foreach ($entry in $entries) {
        if ($entry -notmatch '(?s)^(?<mode>\d{6}) (?<oid>[0-9a-f]{40,64}) (?<stage>[0-3])\t(?<path>.+)$') { throw 'Unexpected Git index entry.' }
        $mode=$Matches.mode; $oid=$Matches.oid; $stage=$Matches.stage; $path=$Matches.path
        $checked++
        if ($stage -ne '0') { Add-SafetyFinding $path 'Unresolved index entry'; continue }
        if ($mode -cnotin @('100644','100755')) { Add-SafetyFinding $path 'Links and submodules require separate source review'; continue }
        $pathFinding = Test-SafetyPath $path
        if ($pathFinding) { Add-SafetyFinding $path $pathFinding }
        # This is a byte protocol. Framework's default stdin StreamWriter can
        # inherit a UTF8 BOM from Console.InputEncoding on Windows CI runners.
        # Send the validated ASCII object ID and LF directly, without a BOM/CR.
        $request=[Text.Encoding]::ASCII.GetBytes($oid+"`n")
        $inputStream.Write($request,0,$request.Length); $inputStream.Flush()
        $header = Read-SafetyHeader $stream
        if ($header -notmatch '^([0-9a-f]{40,64}) blob ([0-9]+)$' -or $Matches[1] -cne $oid) { throw 'Git object identity or type mismatch.' }
        [long]$length = $Matches[2]; [long]$remaining = $length; $bytesRead += $length
        $prefix = New-Object byte[] ([int][Math]::Min(64,$length)); $prefixCount=0
        # Non-image source content over8 MiB requires maintainer review. Images
        # are streamed, not copied into the worktree or decoded into RAM.
        $body = New-Object IO.MemoryStream
        try {
            while ($remaining -gt 0) {
                $n = $stream.Read($buffer,0,[int][Math]::Min($buffer.Length,$remaining))
                if ($n -le 0) { throw 'Incomplete Git source object.' }
                if ($prefixCount -lt $prefix.Length) {
                    $take=[Math]::Min($prefix.Length-$prefixCount,$n)
                    [Array]::Copy($buffer,0,$prefix,$prefixCount,$take); $prefixCount+=$take
                }
                if ($length -le 8MB) { $body.Write($buffer,0,$n) }
                $remaining-=$n
            }
            if ($stream.ReadByte() -ne 10) { throw 'Invalid Git object terminator.' }
            $signature = Test-SafetySignature $prefix
            if ($signature) { Add-SafetyFinding $path $signature }
            $image = Test-PermittedImage $path $prefix
            if ($image) { $permittedImages++ }
            elseif ($length -gt 8MB) { Add-SafetyFinding $path 'Large non-image source blob requires review' }
            else {
                $data=$body.ToArray(); $text=$null
                try {
                    $decoder=New-Object Text.UTF8Encoding($false,$true)
                    if ($data.Length -ge 2 -and $path -match '(?i)\.(sln|vcproj|vcxproj|filters|xml|ps1|txt)$') {
                        if ($data[0] -eq 255 -and $data[1] -eq 254) { $decoder=New-Object Text.UnicodeEncoding($false,$true,$true) }
                        elseif ($data[0] -eq 254 -and $data[1] -eq 255) { $decoder=New-Object Text.UnicodeEncoding($true,$true,$true) }
                    }
                    $text=$decoder.GetString($data)
                } catch [Text.DecoderFallbackException] {
                    Add-SafetyFinding $path 'Invalid text encoding outside permitted source images'
                }
                if ($null -ne $text) {
                    if ($text -match '[\x00-\x08\x0e-\x1f\x7f]') { Add-SafetyFinding $path 'Unexpected binary/control payload outside permitted source images' }
                    if ($text -match '(?m)^-----BEGIN (?:[A-Z0-9]+ )?PRIVATE KEY-----\r?$') { Add-SafetyFinding $path 'Private-key marker; contents are not printed' }
                }
                if ($path -match '(?i)\.(png|gif|jpg|jpeg|bmp|ico|webp)$') { Add-SafetyFinding $path 'Image path or image signature is not permitted by source policy' }
            }
        } finally { $body.Dispose() }
    }
    # Close the raw pipe, not the unused text writer (which may emit its BOM).
    $inputStream.Close(); $batch.WaitForExit()
    $errors=$errorTask.GetAwaiter().GetResult()
    if ($batch.ExitCode -ne 0) { throw 'Git source-object reading failed.' }
} finally {
    # On a malformed stream, do not wait on an owned Git child blocked writing
    # to an unread pipe. This never targets another process or a VR runtime.
    if (-not $batch.HasExited) { $batch.Kill(); $batch.WaitForExit() }
    $batch.Dispose()
}
$result = [pscustomobject]@{
    Passed=($findings.Count -eq 0); FilesChecked=$checked; BlobBytesRead=$bytesRead
    PermittedImages=$permittedImages; Findings=$findings.ToArray(); StagedBlobsChecked=$true
    FilesModified=$false; HistoryChecked=$false; ReleaseReady=$false
    Limits='Preventive staged-path and binary-signature policy, not an exhaustive copyright, secret, personal-data or historical audit.'
}
if (-not $result.Passed -and -not $ReportOnly) {
    $messages=@($findings | ForEach-Object { '{0}: {1}' -f $_.Path,$_.Reason })
    throw ("Source-distribution check failed:`n" + ($messages -join "`n"))
}
$result
