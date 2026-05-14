$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "hand_udp_sender.py"
$requirements = Join-Path $PSScriptRoot "requirements-hand-udp.txt"
$dist = Join-Path $PSScriptRoot "dist"
$build = Join-Path $PSScriptRoot "build"
$venv = Join-Path $PSScriptRoot ".venv"
$venvPython = Join-Path $venv "Scripts\python.exe"

function Invoke-BasePython {
    & $basePython @basePythonArgs @args
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed with exit code ${LASTEXITCODE}: $($args -join ' ')"
    }
}

function Invoke-VenvPython {
    & $venvPython @args
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed with exit code ${LASTEXITCODE}: $($args -join ' ')"
    }
}

function Test-Python311 {
    param(
        [string]$Command,
        [string[]]$CommandArgs = @()
    )

    $version = & $Command @CommandArgs -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
    return $LASTEXITCODE -eq 0 -and $version -eq "3.11"
}

$python = Get-Command python -ErrorAction SilentlyContinue
$py = Get-Command py -ErrorAction SilentlyContinue

if ($python -and (Test-Python311 $python.Source)) {
    $basePython = $python.Source
    $basePythonArgs = @()
} elseif ($py -and (Test-Python311 $py.Source @("-3.11"))) {
    $basePython = $py.Source
    $basePythonArgs = @("-3.11")
} elseif ($python) {
    $basePython = $python.Source
    $basePythonArgs = @()
} else {
    throw "Python 3.11 is required to build the Release hand tracking helper."
}

if (-not (Test-Path $venvPython)) {
    Invoke-BasePython -m venv $venv
}

Invoke-VenvPython -m pip install --upgrade pip
Invoke-VenvPython -m pip install -r $requirements
Invoke-VenvPython -m pip install pyinstaller

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
