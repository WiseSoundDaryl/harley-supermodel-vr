#requires -Version 5.1
# Ordinary, non-diagnostic user-play options shared by portable launch routes.
function Get-HarleyVRRomSelection {
    param([string]$RomDirectory, [string]$GamesPath)
    $preferred = Join-Path $RomDirectory 'harleyb.zip'
    $candidates = @($preferred, (Join-Path $RomDirectory 'harley.zip')) |
        Where-Object { [IO.File]::Exists($_) }
    if (-not $candidates) {
        return [pscustomobject]@{ Path=$preferred; Issue='Add your own complete Export Revision B set to ROMs as harley.zip or harleyb.zip. Keep it zipped. Revision A (harleya) is a different set.' }
    }

    try {
        $database = [System.Xml.XmlDocument]::new()
        $database.XmlResolver = $null
        $reader = [System.Xml.XmlReader]::Create($GamesPath, [System.Xml.XmlReaderSettings]::new())
        try { $database.Load($reader) } finally { $reader.Dispose() }
        $expected = [Collections.Generic.HashSet[uint32]]::new()
        foreach ($chip in $database.SelectNodes("//game[@name='harley']/roms/region[not(@required='false')]/file")) {
            [void]$expected.Add([Convert]::ToUInt32($chip.GetAttribute('crc32'),16))
        }
        if (-not $expected.Count) { throw 'Missing Revision B definition.' }
    } catch {
        return [pscustomobject]@{ Path=$preferred; Issue='Cannot identify the game set. Extract Config\Games.xml from this HarleyVR release again.' }
    }

    # Supermodel uses ZIP directory CRCs to identify a set before reading the chips.
    # Modern .NET exposes Crc32 publicly; Windows PowerShell 5.1's .NET Framework
    # stores the same directory value in _crc32. Neither path opens chip streams.
    Add-Type -AssemblyName System.IO.Compression
    $entryType = [IO.Compression.ZipArchiveEntry]
    $crcMember = $entryType.GetProperty('Crc32')
    if (-not $crcMember) { $crcMember = $entryType.GetField('_crc32', 'Instance,NonPublic') }
    if (-not $crcMember) {
        return [pscustomobject]@{ Path=$preferred; Issue='This PowerShell runtime cannot verify the ZIP directory checksums. Please report the PowerShell and .NET versions for launcher support.' }
    }

    $problems = [Collections.Generic.List[string]]::new()
    foreach ($path in $candidates) {
        $name = [IO.Path]::GetFileName($path)
        try {
            $found = [Collections.Generic.HashSet[uint32]]::new()
            $stream = [IO.File]::OpenRead($path)
            try {
                $zip = [IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Read)
                try {
                    foreach ($entry in $zip.Entries) {
                        if ($entry.Length -gt 0) { [void]$found.Add($crcMember.GetValue($entry)) }
                    }
                } finally { $zip.Dispose() }
            } finally { $stream.Dispose() }
        } catch {
            $problems.Add("$name could not be read as a ZIP archive.")
            continue
        }
        $missing = [Collections.Generic.HashSet[uint32]]::new($expected)
        $missing.ExceptWith($found)
        $other = [Collections.Generic.HashSet[uint32]]::new($found)
        $other.ExceptWith($expected)
        $hasRevA = $false
        foreach ($chip in $database.SelectNodes("//game[@name='harleya']/roms/region/file")) {
            if ($other.Contains([Convert]::ToUInt32($chip.GetAttribute('crc32'),16))) { $hasRevA = $true; break }
        }
        if ($hasRevA) {
            $problems.Add("$name contains Revision A game data. Use a standalone Export Revision B archive.")
        } elseif ($missing.Count) {
            $problems.Add("$name is missing $($missing.Count) of $($expected.Count) required Revision B game files. Use the complete set.")
        } else {
            # A merged archive can make Supermodel choose a child or another
            # game ahead of the complete parent. Ignore unrelated extras like text.
            $hasOtherGame = $false
            if ($other.Count) {
                foreach ($chip in $database.SelectNodes("//game/roms/region/file[@crc32]")) {
                    if ($other.Contains([Convert]::ToUInt32($chip.GetAttribute('crc32'),16))) { $hasOtherGame = $true; break }
                }
            }
            if (-not $hasOtherGame) { return [pscustomobject]@{ Path=$path; Issue=$null } }
            $problems.Add("$name contains multiple games. Use a standalone Export Revision B archive.")
        }
    }
    return [pscustomobject]@{ Path=@($candidates)[0]; Issue=($problems -join ' ') }
}

function Get-HarleyVRLaunchOptions {
    param([ValidateSet('Meta','SteamVR')][string]$Runtime,[switch]$AfterStartup,
          [ValidateSet('Performance','Balanced','Quality')][string]$MetaRenderPreset='Balanced')
    $metaScale = @{ Performance='0.5'; Balanced='0.75'; Quality='1' }
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
        '-body-interaction=0', '-body-test=0', '-no-stereo-bike-hud',
        '-no-harley-free-ride', '-no-stereo-hide-hud',
        '-user-menu=1', '-user-menu-start=1', '-user-menu-restart=1', '-user-menu-calibrate=0'
    )
    foreach ($option in $options) {
        # The tested Rift S build predates the ride menu and Dream Ride startup selector.
        if ($Runtime -eq 'Meta' -and ($option -like '-user-menu-*' -or $option -in @('-user-menu=1','-no-harley-free-ride','-no-stereo-hide-hud'))) { continue }
        if ($Runtime -eq 'Meta' -and $option -ceq '-xr-scale=0.5') { "-xr-scale=$($metaScale[$MetaRenderPreset])" }
        elseif ($AfterStartup -and $option -ceq '-user-menu-start=1') { '-user-menu-start=0' }
        elseif ($AfterStartup -and $option -ceq '-user-menu-restart=1') { '-user-menu-restart=0' }
        else { $option }
    }
    if ($Runtime -eq 'SteamVR') {
        '-idle-skip'
        '-stereo-theater-depth=0'
        '-xr-ready-wake=1'
        '-xr-pre-prepare-present=1'
    }
}
