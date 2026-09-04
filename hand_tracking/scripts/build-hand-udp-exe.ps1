param(
    [string]$Platform = "x64",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$handTrackingRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $handTrackingRoot
$script = Join-Path $handTrackingRoot "src\hand_udp_sender.py"
$setupScript = Join-Path $PSScriptRoot "setup-hand-udp.ps1"
$generated = Join-Path $repoRoot "generated"
$projectName = "HandUdpSender"
$dist = Join-Path $generated "outputs\$Platform\$Configuration\$projectName"
$build = Join-Path $generated "intermediate\$Platform\$Configuration\$projectName\pyinstaller"
$venv = Join-Path $generated "intermediate\$projectName\.venv"
$venvPython = Join-Path $venv "Scripts\python.exe"

function Invoke-VenvPython {
    & $venvPython @args
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed with exit code ${LASTEXITCODE}: $($args -join ' ')"
    }
}

& $setupScript
if (-not (Test-Path -LiteralPath $venvPython)) {
    throw "Failed to prepare the hand tracking build environment."
}

Invoke-VenvPython -m PyInstaller `
    --noconfirm `
    --clean `
    --log-level ERROR `
    --onedir `
    --name hand_udp_sender `
    --distpath $dist `
    --workpath $build `
    --specpath $build `
    --exclude-module tkinter `
    --collect-all mediapipe `
    --collect-all cv2 `
    $script
