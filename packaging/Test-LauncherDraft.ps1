#requires -Version 5.1
# Source-only maintainer check. This never launches the emulator or loads OpenXR.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ReferenceManifest)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$launcher = Join-Path $root 'Play-HarleyVR.ps1'
$checks = 0
function Assert-That([bool]$Value, [string]$Message) {
    if (-not $Value) { throw $Message }
    $script:checks++
}
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($launcher, [ref]$tokens, [ref]$errors)
Assert-That ($errors.Count -eq 0) 'Launcher has PowerShell syntax errors.'
$assignments = @($ast.FindAll({
    param($node)
    $node -is [Management.Automation.Language.AssignmentStatementAst] -and
    $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and
    $node.Left.VariablePath.UserPath -eq 'options'
}, $true))
Assert-That ($assignments.Count -eq 1) 'Expected one literal regular-game options list.'
$literalNodes = @($assignments[0].Right.FindAll({ param($node) $true }, $true))
$allowedNodes = @('ArrayExpressionAst','StatementBlockAst','PipelineAst',
    'CommandExpressionAst','ArrayLiteralAst','StringConstantExpressionAst')
Assert-That (@($literalNodes | Where-Object { $allowedNodes -notcontains $_.GetType().Name }).Count -eq 0) 'Options must contain only literal strings.'
$options = @($literalNodes | Where-Object { $_ -is [Management.Automation.Language.StringConstantExpressionAst] } | ForEach-Object { $_.Value })
$records = Get-Content -LiteralPath $ReferenceManifest -Raw | ConvertFrom-Json
$reference = @($records | Where-Object { $_.Mode -eq 'Regular' })
Assert-That ($reference.Count -eq 1) 'Expected the recorded regular-game reference.'
$referenceArgs = [regex]::Replace($reference[0].Result.Arguments, '^"[^"]+" ', '')
Assert-That (($options -join ' ') -ceq $referenceArgs) 'Regular game options differ from the selected main build.'
foreach ($flag in @('-body-interaction=0', '-body-test=0', '-no-stereo-bike-hud',
    '-xr-scene-interpolation=1', '-xr-late-worker-present=1', '-xr-array-swapchain=0',
    '-sound-volume=100', '-music-volume=100')) {
    Assert-That (@($options | Where-Object { $_ -ceq $flag }).Count -eq 1) "Missing/duplicate main option: $flag"
}
Assert-That (-not (Test-Path -LiteralPath (Join-Path $root 'release-ready.json'))) 'This check is for the blocked source scaffold only.'
Assert-That (-not (Test-Path -LiteralPath (Join-Path $root 'supermodel.exe'))) 'No emulator belongs in this source-only scaffold.'
$before = @{}
Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {
    $before[$_.FullName] = (Get-FileHash -LiteralPath $_.FullName).Hash
}
$preflight = & $launcher -PreflightOnly
$metaPreflight = & $launcher -Runtime Meta -PreflightOnly
Assert-That ($preflight.Passed -eq $false) 'Incomplete source scaffold must not pass readiness.'
Assert-That ($preflight.GameLaunched -eq $false) 'Preflight must not launch a game.'
Assert-That ($preflight.RuntimeLoaded -eq $false) 'Preflight must not load a runtime.'
Assert-That ($preflight.AswQueriedOrChanged -eq $false -and $metaPreflight.AswQueriedOrChanged -eq $false) 'Preflights must not query or change Meta ASW.'
Assert-That (-not $metaPreflight.Passed -and -not $metaPreflight.GameLaunched -and -not $metaPreflight.RuntimeLoaded) 'Meta source scaffold must stay blocked and read-only.'
Assert-That (-not $preflight.SystemDefaultRuntimeChanged -and -not $metaPreflight.SystemDefaultRuntimeChanged) 'Runtime selection must not change the system default.'
Assert-That ($preflight.HeadsetTrackingVerified -eq $false) 'Runtime process presence is not headset verification.'
Assert-That (@($preflight.Issues | Where-Object { $_ -like '*source/package draft*' }).Count -eq 1) 'Missing release marker must be explained.'
Assert-That (@($preflight.Issues | Where-Object { $_ -like '*ROMs\harleyb.zip*' }).Count -eq 1) 'Missing user ROM must be explained.'
Assert-That ($preflight.Arguments -ceq ('"' + (Join-Path $root 'ROMs\harleyb.zip') + '" ' + $referenceArgs)) 'ROM argument must be package-relative and quoted.'
Assert-That ($metaPreflight.Arguments -ceq $preflight.Arguments.Replace('-xr-scale=0.5','-xr-scale=0.75')) 'Meta must change only the tested render scale.'
Assert-That (-not (Test-Path -LiteralPath (Join-Path $root 'Saves'))) 'Preflight must not create Saves.'
Assert-That (-not (Test-Path -LiteralPath (Join-Path $root 'NVRAM'))) 'Preflight must not create NVRAM.'
Assert-That (-not (Test-Path -LiteralPath (Join-Path $root 'Diagnostics'))) 'Preflight must not create diagnostics or recovery journals.'
$after = @(Get-ChildItem -LiteralPath $root -Recurse -File)
Assert-That ($after.Count -eq $before.Count) 'Preflight must not create/remove package files.'
foreach ($file in $after) {
    Assert-That ($before[$file.FullName] -eq (Get-FileHash -LiteralPath $file.FullName).Hash) 'Preflight modified a package file.'
}
$result = [pscustomobject]@{
    Passed=$true; Checks=$checks; PowerShellVersion=$PSVersionTable.PSVersion.ToString();
    OptionsExact=$true; GameLaunched=$false; RuntimeLoaded=$false;
    Preflight=$preflight; MetaPreflight=$metaPreflight;
    Scope='Source scaffold and read-only preflight only. No packaged-binary, clean-profile, headset or public-release validation.'
}
$result | ConvertTo-Json -Depth 8
