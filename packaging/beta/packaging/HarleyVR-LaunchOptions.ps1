#requires -Version 5.1
# Ordinary, non-diagnostic user-play options shared by portable launch routes.
function Get-HarleyVRLaunchOptions {
    param([ValidateSet('Meta','SteamVR')][string]$Runtime,[switch]$AfterStartup)
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
        if ($Runtime -eq 'Meta' -and $option -ceq '-xr-scale=0.5') { '-xr-scale=0.75' }
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
