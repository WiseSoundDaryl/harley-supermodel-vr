#requires -Version 5.1
# Token-based successor to the retained E840 preview supervisor. Dot-source only.
# No shell evaluation, runtime loading or settings changes occur in this file.
function ConvertTo-HarleyVRCommandLine {
    param([Parameter(Mandatory=$true)][string[]]$Tokens)
    $quoted = foreach ($token in $Tokens) {
        if ($null -eq $token -or $token.IndexOf([char]0) -ge 0) { throw 'Invalid game argument.' }
        $text = New-Object Text.StringBuilder
        $null = $text.Append('"'); $slashes = 0
        foreach ($character in $token.ToCharArray()) {
            if ($character -eq [char]92) { $slashes++; continue }
            if ($character -eq [char]34) {
                $null = $text.Append(('\' * ($slashes * 2 + 1))); $null = $text.Append('"')
            } else {
                $null = $text.Append(('\' * $slashes)); $null = $text.Append($character)
            }
            $slashes = 0
        }
        $null = $text.Append(('\' * ($slashes * 2))); $null = $text.Append('"')
        $text.ToString()
    }
    return $quoted -join ' '
}

function Get-HarleyVRRestartTokens {
    param([Parameter(Mandatory=$true)][string[]]$Tokens,[Parameter(Mandatory=$true)][int]$Code)
    if ($Code -lt 75 -or $Code -gt 80) { throw 'Unsupported startup restart request.' }
    $calibrate = $Code -in @(76,79,80)
    $mode = $(if ($Code -in @(77,79)) { 'dream' } elseif ($Code -in @(78,80)) { 'regular' } else { 'unchanged' })
    $result = New-Object 'System.Collections.Generic.List[string]'
    foreach ($token in $Tokens) {
        # Exact tokens only. Never replace text inside a ROM path or music option.
        if ($calibrate -and $token -cmatch '^-user-menu-calibrate=[01]$') { continue }
        if ($mode -ne 'unchanged' -and $token -cin @('-harley-free-ride','-no-harley-free-ride','-stereo-hide-hud','-no-stereo-hide-hud')) { continue }
        $result.Add($token)
    }
    if ($mode -eq 'dream') { $result.Add('-harley-free-ride'); $result.Add('-stereo-hide-hud') }
    if ($mode -eq 'regular') { $result.Add('-no-harley-free-ride'); $result.Add('-no-stereo-hide-hud') }
    if ($calibrate) { $result.Add('-user-menu-calibrate=1') }
    return $result.ToArray()
}

function Invoke-HarleyVRModeSession {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [Parameter(Mandatory=$true)][string[]]$RestartArguments,
        [Parameter(Mandatory=$true)][scriptblock]$ProcessFactory,
        [scriptblock]$OnGameStarted = { param($Game) },
        [scriptblock]$OnGameExited = { param($Game) }
    )
    if ($Arguments.Count -lt 2 -or $RestartArguments.Count -lt 2 -or $Arguments[0] -cne $RestartArguments[0] -or
        @($Arguments | Where-Object { $_ -ceq '-user-menu-restart=1' }).Count -ne 1 -or
        @($RestartArguments | Where-Object { $_ -ceq '-user-menu-restart=0' }).Count -ne 1 -or
        @($RestartArguments | Where-Object { $_ -ceq '-user-menu-start=0' }).Count -ne 1) {
        throw 'The prepared startup restart arguments are incomplete.'
    }
    $next = $RestartArguments; $restartCode = 0
    for ($attempt = 0; $attempt -lt 2; $attempt++) {
        $game = $null; $started = $false
        try {
            [string[]]$tokens = $(if ($attempt -eq 0) { $Arguments } else { $next })
            $game = & $ProcessFactory $tokens
            if (-not $game.Start()) { throw 'The game process did not start.' }
            $started = $true
            & $OnGameStarted $game
            $game.WaitForExit()
            if ($game.ExitCode -ge 75 -and $game.ExitCode -le 80) {
                if ($attempt -ne 0) { throw 'The game requested another startup restart. It was not restarted repeatedly.' }
                $restartCode = $game.ExitCode
                $next = @(Get-HarleyVRRestartTokens $RestartArguments $restartCode)
                continue
            }
            if ($game.ExitCode -ne 0) { throw "Game exited with code $($game.ExitCode). See its session report and Supermodel.log." }
            return [pscustomobject]@{
                ExitCode=0;StartupRestarted=($attempt -eq 1);GameProcessesStarted=($attempt + 1);RestartRequestCode=$restartCode;
                SeatCalibrationRequested=($restartCode -in @(76,79,80));
                RequestedMode=$(if($restartCode -in @(77,79)){'dream'}elseif($restartCode -in @(78,80)){'regular'}else{'unchanged'})
            }
        } finally {
            if ($null -ne $game) {
                try {
                    # A journal or retention failure still waits for OUR child.
                    # The caller keeps the shared ASW guard over both children.
                    if ($started -and -not $game.HasExited) { $game.WaitForExit() }
                    if ($started -and $game.HasExited) { & $OnGameExited $game }
                } finally { $game.Dispose() }
            }
        }
    }
    throw 'The session ended unexpectedly.'
}
