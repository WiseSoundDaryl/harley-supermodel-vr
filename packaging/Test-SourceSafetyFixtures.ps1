#requires -Version 5.1
<#
.SYNOPSIS
Runs source-distribution checks against a disposable synthetic Git index.
.DESCRIPTION
Fixtures are inert text, tiny image headers and recognizable non-executable
binary signatures. No game data, player package, runtime or private manifest is
needed. The checker must leave every fixture byte unchanged. Only the unique
directory created by this test is removed, after checking its marker and paths.
#>
[CmdletBinding()]
param([string]$TemporaryRoot = [IO.Path]::GetTempPath())
$ErrorActionPreference='Stop'
$checker=Join-Path $PSScriptRoot 'Test-SourceSafety.ps1'
$ignore=Join-Path (Split-Path -Parent $PSScriptRoot) '.gitignore'
$git=(Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$parent=Get-Item -LiteralPath $TemporaryRoot -Force
if (-not $parent.PSIsContainer -or ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'TemporaryRoot must be an ordinary existing directory.' }
$token=[Guid]::NewGuid().ToString('N')
$fixture=Join-Path $parent.FullName ('HarleyVR-source-safety-'+$token)
$marker=Join-Path $fixture '.fixture-owner'
$checks=0; $cases=New-Object 'System.Collections.Generic.List[string]'
$created=$false
$originalInputEncoding=[Console]::InputEncoding
function Assert-Safety([bool]$Condition,[string]$Message) {
    $script:checks++
    if (-not $Condition) { throw ('Source-safety fixture failed: '+$Message) }
}
function Quote-SafetyArgument([string]$Value) {
    return '"'+($Value -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'
}
function Invoke-FixtureGit([string[]]$Arguments,[string]$InputText=$null,[int[]]$Expected=@(0)) {
    $si=New-Object Diagnostics.ProcessStartInfo
    $si.FileName=$git; $si.WorkingDirectory=$fixture
    $si.Arguments=(@($Arguments | ForEach-Object {Quote-SafetyArgument $_}) -join ' ')
    $si.UseShellExecute=$false; $si.CreateNoWindow=$true
    $si.RedirectStandardInput=$true; $si.RedirectStandardOutput=$true; $si.RedirectStandardError=$true
    $si.EnvironmentVariables['GIT_CONFIG_NOSYSTEM']='1'
    $si.EnvironmentVariables['GIT_CONFIG_GLOBAL']=Join-Path $fixture '.empty-global-config'
    $si.EnvironmentVariables['GIT_TERMINAL_PROMPT']='0'
    $p=New-Object Diagnostics.Process; $p.StartInfo=$si
    try {
        if ($si.PSObject.Properties['StandardInputEncoding']) {
            $si.StandardInputEncoding=New-Object Text.UTF8Encoding($false)
            $started=$p.Start()
        } else {
            $savedInputEncoding=[Console]::InputEncoding
            try {
                [Console]::InputEncoding=New-Object Text.UTF8Encoding($false)
                $started=$p.Start()
            } finally { [Console]::InputEncoding=$savedInputEncoding }
        }
        if (-not $started) { throw 'Could not start Git for synthetic fixtures.' }
        $out=$p.StandardOutput.ReadToEndAsync(); $err=$p.StandardError.ReadToEndAsync()
        # Git stdin is a byte protocol, independent of Console.InputEncoding.
        # Avoid the Framework writer's optional BOM even when sending no input.
        $inputPipe=$p.StandardInput.BaseStream
        if ($null -ne $InputText) {
            $inputBytes=[Text.Encoding]::UTF8.GetBytes($InputText)
            $inputPipe.Write($inputBytes,0,$inputBytes.Length)
        }
        $inputPipe.Close(); $p.WaitForExit()
        $result=$out.GetAwaiter().GetResult(); $errors=$err.GetAwaiter().GetResult()
        if ($p.ExitCode -notin $Expected) { throw ('Synthetic Git operation failed: '+($Arguments -join ' ')) }
        return $result
    } finally { $p.Dispose() }
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
function Write-TextFixture([string]$Relative,[string]$Text) { Write-Fixture $Relative ([Text.Encoding]::UTF8.GetBytes($Text)) }
function Stage-Fixture([string]$Relative) { $null=Invoke-FixtureGit @('-c','core.attributesFile=.empty-global-config','add','-f','--',$Relative) }
function Remove-Fixture([string]$Relative) {
    $null=Invoke-FixtureGit @('update-index','--force-remove','--',$Relative)
    $path=Fixture-Path $Relative
    if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path }
}
function Snapshot-Fixture {
    $result=New-Object 'System.Collections.Generic.List[string]'
    foreach ($item in (Get-ChildItem -LiteralPath $fixture -Recurse -Force -File | Sort-Object FullName)) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unexpected fixture link.' }
        $result.Add($item.FullName.Substring($fixture.Length)+':'+(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash)
    }
    return ($result -join "`n")
}
function Check-Fixture([string]$Name,[bool]$Expected,[string]$Reason='') {
    $before=Snapshot-Fixture
    $result=& $checker -Root $fixture -ReportOnly
    Assert-Safety ($result.Passed -eq $Expected) ($Name+': result')
    Assert-Safety ($result.StagedBlobsChecked -and -not $result.FilesModified -and -not $result.HistoryChecked) ($Name+': scope')
    Assert-Safety ((Snapshot-Fixture) -ceq $before) ($Name+': read-only bytes')
    if ($Reason) { Assert-Safety (@($result.Findings | Where-Object {$_.Reason -like $Reason}).Count -gt 0) ($Name+': expected finding') }
    $cases.Add($Name)
    return $result
}
try {
    # Reproduce the Windows CI encoding that formerly prefixed Git object IDs
    # with a BOM. Every default fixture run covers it, then restores the caller.
    [Console]::InputEncoding=New-Object Text.UTF8Encoding($true)
    Assert-Safety ([Console]::InputEncoding.WebName -ceq 'utf-8' -and [Console]::InputEncoding.GetPreamble().Length -eq 3) 'UTF8-BOM input regression environment'
    $null=New-Item -ItemType Directory -Path $fixture
    $created=$true
    [IO.File]::WriteAllText($marker,$token)
    # Exercise the actual production Start helper's failure path without a
    # replacement implementation or any executable fixture. This child scope
    # uses an absent path and cannot start Git, an emulator or a runtime.
    $tokens=$null; $parseErrors=$null
    $syntax=[Management.Automation.Language.Parser]::ParseFile($checker,[ref]$tokens,[ref]$parseErrors)
    $startFunction=@($syntax.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'New-SafetyGit'},$true))
    Assert-Safety ($parseErrors.Count -eq 0 -and $startFunction.Count -eq 1) 'actual Git start helper is available'
    $failedStart=& {
        param($Definition,$Directory)
        $git=Join-Path $Directory 'intentionally-absent-tool.exe'; $checkedRoot=$Directory
        . ([scriptblock]::Create($Definition))
        $caught=$false
        try { $null=New-SafetyGit 'cat-file --batch' $true } catch { $caught=$true }
        [pscustomobject]@{Caught=$caught;Restored=([Console]::InputEncoding.WebName -ceq 'utf-8' -and [Console]::InputEncoding.GetPreamble().Length -eq 3)}
    } $startFunction[0].Extent.Text $fixture
    Assert-Safety $failedStart.Caught 'owned Git start failure is surfaced'
    Assert-Safety $failedStart.Restored 'owned Git start failure restores UTF8-BOM caller encoding'
    $cases.Add('owned Git start failure restores caller encoding')
    [IO.File]::WriteAllText((Join-Path $fixture '.empty-global-config'),'')
    $null=New-Item -ItemType Directory -Path (Join-Path $fixture '.empty-hooks')
    $null=Invoke-FixtureGit @('init','--quiet','--template=')
    $null=Invoke-FixtureGit @('config','core.autocrlf','false')
    # Prove explicit ignore patterns, even on a case-insensitive Windows disk.
    $null=Invoke-FixtureGit @('config','core.ignorecase','false')
    $null=Invoke-FixtureGit @('config','core.fsmonitor','false')
    $null=Invoke-FixtureGit @('config','core.hooksPath','.empty-hooks')
    $null=Invoke-FixtureGit @('config','core.attributesFile','.empty-global-config')
    Copy-Item -LiteralPath $ignore -Destination (Join-Path $fixture '.gitignore')
    Stage-Fixture '.gitignore'
    $good=@{
        'Src/ROMSet.cpp'='// Reads harleyb.zip from ROMs; no game data is embedded here.'
        'Src/Pro1000/SyntheticSCSIROM.cpp'='// Emulator-generated synthetic diagnostic instructions.'
        'Makefiles/Rules.inc'='objects = example.o'
        'Config/Games.xml'='<games><game name="synthetic" /></games>'
        'Config/Music.xml'='<music />'
        'Docs/Example with spaces.md'='Add your own ROM archive to ROMs/. Never commit player saves.'
        'Docs/reports/compact-result.json'='{"synthetic":true,"passed":true}'
        'packaging/validation-result.json'='{"SyntheticOnly":true}'
        'packaging/templates/Supermodel.ini.example'="[Global]`nSoundVolume=100"
        'packaging/templates/.env.example'='SYNTHETIC_OPTION=example-only'
        'Docs/Images/example.svg'='<svg xmlns="http://www.w3.org/2000/svg" />'
    }
    foreach ($item in $good.GetEnumerator()) { Write-TextFixture $item.Key $item.Value; Stage-Fixture $item.Key }
    # A real tiny PNG, plus an inert BMP header sufficient for signature policy.
    $png=[Convert]::FromBase64String('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j3ioAAAAASUVORK5CYII=')
    Write-Fixture 'Docs/Images/synthetic.png' $png; Stage-Fixture 'Docs/Images/synthetic.png'
    $bmp=New-Object byte[] 58; $bmp[0]=66; $bmp[1]=77
    [Array]::Copy([BitConverter]::GetBytes([int]58),0,$bmp,2,4)
    Write-Fixture 'Assets/p1crosshair.bmp' $bmp; Stage-Fixture 'Assets/p1crosshair.bmp'
    $null=Check-Fixture 'legitimate source, docs, templates and permitted images' $true

    $prohibited=@(
        'ROMs/synthetic.zip','roms/synthetic.zip','rOmS/synthetic.bin','nested/Saves/synthetic.st0',
        'Saves/progress.txt','saves/progress.txt','NVRAM/settings.txt','nvram/settings.txt',
        'Diagnostics/recovery.json','runs/private/result.json','reports/live-play/game.json',
        'captures/frame.png','profiles/data.json','pgo/data.txt','dist/package.txt',
        'supermodel.exe','nested/Runtime.DLL','release.ZIP','raw.7z','archive.TAR','code.o',
        'code.OBJ','libsample.a','code.PDB','profile.GCDA','profile.PROFRAW','trace.ETL',
        'game.st3.bak','data.NV','firmware.ROM','progress.sav','progress.STATE.bak','Config/Supermodel.ini','Config/Personal.json',
        'Config/private.cfg','meta-asw-pending.json','META-ASW-RECOVERY.JSON','release-ready.json','steamvr.vrsettings',
        '.env','.env.production','id_rsa','credentials/id_ed25519',
        'nested/game-trace.csv','native.log','capture.mp4'
    )
    foreach ($relative in $prohibited) {
        Write-TextFixture $relative 'Synthetic prohibited-name fixture; not real game or executable data.'
        Stage-Fixture $relative
        $null=Check-Fixture ('prohibited path: '+$relative) $false
        Remove-Fixture $relative
    }
    $signatures=@(
        @{Name='ZIP';Bytes=[byte[]]@(80,75,3,4,0,0,0,0);Reason='Archive signature*'},
        @{Name='gzip';Bytes=[byte[]]@(31,139,8,0,0,0,0,0);Reason='Archive signature*'},
        @{Name='7zip';Bytes=[byte[]]@(55,122,188,175,39,28,0,0);Reason='Archive signature*'},
        @{Name='RAR';Bytes=[byte[]]@(82,97,114,33,26,7,0,0);Reason='Archive signature*'},
        @{Name='ar';Bytes=[Text.Encoding]::ASCII.GetBytes("!<arch>`n");Reason='Archive signature*'},
        @{Name='PE';Bytes=[byte[]]@(77,90,0,0,0,0,0,0);Reason='Executable/object signature*'},
        @{Name='ELF';Bytes=[byte[]]@(127,69,76,70,0,0,0,0);Reason='Executable/object signature*'},
        @{Name='MachO';Bytes=[byte[]]@(207,250,237,254,0,0,0,0);Reason='Executable/object signature*'},
        @{Name='LLVM';Bytes=[byte[]]@(66,67,192,222,0,0,0,0);Reason='Executable/object signature*'},
        @{Name='unknown binary';Bytes=[byte[]]@(65,0,66,0,67,0);Reason='Unexpected binary/control payload*'},
        @{Name='non-NUL invalid UTF8';Bytes=[byte[]]@(255,128,129,130);Reason='Invalid text encoding*'},
        @{Name='non-NUL control bytes';Bytes=[byte[]]@(65,1,66,2);Reason='Unexpected binary/control payload*'}
    )
    foreach ($sample in $signatures) {
        Write-Fixture 'Docs/renamed payload.txt' $sample.Bytes; Stage-Fixture 'Docs/renamed payload.txt'
        $null=Check-Fixture ('renamed content: '+$sample.Name) $false $sample.Reason
        Remove-Fixture 'Docs/renamed payload.txt'
    }
    Write-Fixture 'Docs/Images/disguised.png' ([byte[]]@(80,75,3,4,0,0))
    Stage-Fixture 'Docs/Images/disguised.png'
    $null=Check-Fixture 'archive disguised as permitted image' $false 'Archive signature*'
    Remove-Fixture 'Docs/Images/disguised.png'
    Write-Fixture 'private-image.png' $png; Stage-Fixture 'private-image.png'
    $null=Check-Fixture 'image outside permitted locations' $false 'Image path or image signature*'
    Remove-Fixture 'private-image.png'
    Write-TextFixture 'Docs/synthetic-key.txt' ('-----BEGIN '+'PRIVATE KEY-----'+"`nSYNTHETIC-NOT-A-KEY`n"+'-----END '+'PRIVATE KEY-----')
    Stage-Fixture 'Docs/synthetic-key.txt'
    $null=Check-Fixture 'synthetic private-key marker' $false 'Private-key marker*'
    Remove-Fixture 'Docs/synthetic-key.txt'
    Write-Fixture 'Docs/utf16.txt' ([byte[]]@(255,254)+[Text.Encoding]::Unicode.GetBytes('Synthetic UTF16 document.'))
    Stage-Fixture 'Docs/utf16.txt'
    $null=Check-Fixture 'valid BOM UTF16 text is permitted' $true
    Remove-Fixture 'Docs/utf16.txt'
    Write-Fixture 'Docs/utf16.txt' ([byte[]]@(255,254,0,216))
    Stage-Fixture 'Docs/utf16.txt'
    $null=Check-Fixture 'malformed BOM UTF16 is rejected' $false 'Invalid text encoding*'
    Remove-Fixture 'Docs/utf16.txt'

    Write-Fixture 'Docs/staged.txt' ([byte[]]@(80,75,3,4,0,0)); Stage-Fixture 'Docs/staged.txt'
    Write-TextFixture 'Docs/staged.txt' 'The worktree is harmless; the staged blob is still prohibited.'
    $null=Check-Fixture 'prohibited staged bytes behind harmless worktree' $false 'Archive signature*'
    $badBlob=(Invoke-FixtureGit @('rev-parse',':Docs/staged.txt')).Trim()
    $goodBlob=(Invoke-FixtureGit @('hash-object','-w','--no-filters','--','Docs/staged.txt')).Trim()
    # This disposable fixture has no public/project history. Replacement refs
    # model a local substitution; the checker must still inspect indexed bytes.
    $null=Invoke-FixtureGit @('replace',$badBlob,$goodBlob)
    $null=Check-Fixture 'replacement refs cannot hide prohibited indexed bytes' $false 'Archive signature*'
    $null=Invoke-FixtureGit @('replace','-d',$badBlob)
    $caught=$false
    try { $null=& $checker -Root $fixture } catch { $caught=$_.Exception.Message -like 'Source-distribution check failed:*' }
    Assert-Safety $caught 'default invocation fails closed'
    Stage-Fixture 'Docs/staged.txt'
    Write-Fixture 'Docs/staged.txt' ([byte[]]@(80,75,3,4,0,0))
    $null=Check-Fixture 'unstaged bytes do not replace checked index bytes' $true
    Remove-Fixture 'Docs/staged.txt'

    Write-TextFixture 'Docs/link-target.txt' 'Synthetic symbolic-link target text.'
    $blob=(Invoke-FixtureGit @('hash-object','-w','--no-filters','--','Docs/link-target.txt')).Trim()
    $null=Invoke-FixtureGit @('update-index','--add','--cacheinfo',('120000,'+$blob+',Docs/synthetic-link'))
    $null=Check-Fixture 'index symlink is rejected without following it' $false 'Links and submodules*'
    Remove-Fixture 'Docs/synthetic-link'; Remove-Fixture 'Docs/link-target.txt'
    $null=Invoke-FixtureGit @('update-index','--add','--cacheinfo','160000,1111111111111111111111111111111111111111,synthetic-submodule')
    $null=Check-Fixture 'unreviewed submodule is rejected' $false 'Links and submodules*'
    Remove-Fixture 'synthetic-submodule'
    $null=Invoke-FixtureGit @('update-index','--index-info') ("100644 "+$blob+" 1`tconflict.cpp`n")
    $null=Check-Fixture 'unresolved Git index is rejected' $false 'Unresolved index entry'
    Remove-Fixture 'conflict.cpp'

    $ignored=@('ROMs/example.zip','roms/example.zip','rOmS/example.zip','Saves/example.st0','saves/example.st0',
        'NVRAM/example.nv','nvram/example.nv','supermodel.EXE','nested/runtime.DLL','distribution.ZIP',
        'runs/private/source.txt','reports/private/trace.csv','captures/private.png','profiles/private.json',
        'profile.GCDA','profile.profraw','Config/Supermodel.ini','Diagnostics/meta-asw-pending.json',
        'profile.OBJ','trace.LOG','progress.SAV','.env','.env.production','id_rsa','meta-asw-recovery.json')
    foreach ($relative in $ignored) {
        $answer=Invoke-FixtureGit @('check-ignore','--no-index','--',$relative) $null @(0,1)
        Assert-Safety ($answer.Trim() -ceq $relative) ('ignore rule: '+$relative)
    }
    foreach ($relative in @('Src/ROMSet.cpp','Config/Games.xml','Config/Music.xml','Docs/Images/synthetic.png','Docs/reports/compact-result.json','packaging/templates/Supermodel.ini.example','packaging/templates/.env.example')) {
        $answer=Invoke-FixtureGit @('check-ignore','--no-index','--',$relative) $null @(0,1)
        Assert-Safety ([string]::IsNullOrEmpty($answer)) ('source remains addable: '+$relative)
    }
    $null=Check-Fixture 'clean index after every negative case' $true
    $result=[pscustomobject]@{Passed=$true;Checks=$checks;Cases=$cases.Count;CaseNames=$cases.ToArray();SyntheticOnly=$true;FixtureCleanup=$false;UTF8BomInputCovered=$true;ConsoleInputEncodingRestored=$false;GameDataUsed=$false;GameOrRuntimeLaunched=$false;HistoryRewritten=$false;ReleaseReady=$false}
} finally {
  try {
    if ($created -and (Test-Path -LiteralPath $fixture)) {
        $resolved=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $fixture).Path)
        $expected=[IO.Path]::GetFullPath((Join-Path $parent.FullName ('HarleyVR-source-safety-'+$token)))
        if ($resolved -cne $expected -or [IO.File]::ReadAllText($marker) -cne $token) { throw 'Fixture ownership check failed; nothing removed.' }
        $items=@(Get-Item -LiteralPath $fixture -Force)+@(Get-ChildItem -LiteralPath $fixture -Recurse -Force)
        if (@($items | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count) { throw 'Fixture cleanup refused a linked path.' }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
  } finally { [Console]::InputEncoding=$originalInputEncoding }
}
$result.FixtureCleanup=$true
$result.ConsoleInputEncodingRestored=([Console]::InputEncoding.CodePage -eq $originalInputEncoding.CodePage -and
    [Convert]::ToBase64String([Console]::InputEncoding.GetPreamble()) -ceq [Convert]::ToBase64String($originalInputEncoding.GetPreamble()))
if (-not $result.ConsoleInputEncodingRestored) { throw 'The caller input encoding was not restored.' }
$result
