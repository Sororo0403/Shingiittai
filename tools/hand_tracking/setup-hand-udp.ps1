$ErrorActionPreference = "Stop"

$requirements = Join-Path $PSScriptRoot "requirements-hand-udp.txt"
$venv = Join-Path $PSScriptRoot ".venv"
$venvPython = Join-Path $venv "Scripts\python.exe"

if (-not (Test-Path $venvPython)) {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        & $py.Source -3.11 -m venv $venv
    } else {
        & (Get-Command python -ErrorAction Stop).Source -m venv $venv
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create hand tracking virtual environment."
    }
}

& $venvPython -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) {
    throw "Failed to upgrade pip in hand tracking virtual environment."
}

& $venvPython -m pip install -r $requirements
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install hand tracking dependencies."
}
