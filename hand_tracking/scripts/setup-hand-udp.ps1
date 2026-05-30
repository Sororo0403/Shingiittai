$ErrorActionPreference = "Stop"

$handTrackingRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $handTrackingRoot
$requirements = Join-Path $handTrackingRoot "requirements\hand-udp.txt"
$venv = Join-Path $repoRoot "generated\intermediate\HandUdpSender\.venv"
$venvPython = Join-Path $venv "Scripts\python.exe"

function Test-Python311 {
    param(
        [string]$Command,
        [string[]]$CommandArgs = @()
    )

    $version = & $Command @CommandArgs -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
    return $LASTEXITCODE -eq 0 -and $version -eq "3.11"
}

function Invoke-Python311 {
    param(
        [string[]]$CommandArgs
    )

    $python = Get-Command python -ErrorAction SilentlyContinue
    $py = Get-Command py -ErrorAction SilentlyContinue

    if ($py -and (Test-Python311 $py.Source @("-3.11"))) {
        & $py.Source -3.11 @CommandArgs
    } elseif ($python -and (Test-Python311 $python.Source)) {
        & $python.Source @CommandArgs
    } else {
        throw "Python 3.11 is required. Install Python 3.11 and confirm 'py -3.11 --version' works."
    }
}

if (-not (Test-Path $venvPython)) {
    Invoke-Python311 @("-m", "venv", $venv)
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create hand tracking virtual environment."
    }
}

if (-not (Test-Python311 $venvPython)) {
    throw "The hand tracking virtual environment must use Python 3.11. Delete '$venv' and run this script again with Python 3.11 installed."
}

& $venvPython -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) {
    throw "Failed to upgrade pip in hand tracking virtual environment."
}

& $venvPython -m pip install -r $requirements
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install hand tracking dependencies."
}
