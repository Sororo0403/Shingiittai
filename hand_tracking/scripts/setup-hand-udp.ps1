$ErrorActionPreference = "Stop"

$handTrackingRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $handTrackingRoot
$requirements = Join-Path $handTrackingRoot "requirements\hand-udp.txt"
$environmentRoot = Join-Path $repoRoot "generated\intermediate\HandUdpSender"
$venv = Join-Path $environmentRoot ".venv"
$venvPython = Join-Path $venv "Scripts\python.exe"
$environmentMarker = Join-Path $venv ".hand-udp-environment"
$environmentSchemaVersion = "1"
$pipVersion = "26.2.1"

function Get-PythonVersion {
    param(
        [string]$Command,
        [string[]]$CommandArgs = @()
    )

    $version = & $Command @CommandArgs -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}')"
    if ($LASTEXITCODE -ne 0) {
        return $null
    }

    return "$version".Trim()
}

function Invoke-BasePython {
    & $basePython @basePythonArgs @args
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed with exit code ${LASTEXITCODE}: $($args -join ' ')"
    }
}

function Invoke-VenvPython {
    & $venvPython @args
    if ($LASTEXITCODE -ne 0) {
        throw "Hand tracking virtual environment command failed with exit code ${LASTEXITCODE}: $($args -join ' ')"
    }
}

function Remove-HandTrackingVenv {
    $resolvedEnvironmentRoot = [System.IO.Path]::GetFullPath($environmentRoot).TrimEnd('\', '/')
    $resolvedVenv = [System.IO.Path]::GetFullPath($venv)
    $requiredPrefix = $resolvedEnvironmentRoot + [System.IO.Path]::DirectorySeparatorChar

    if (-not $resolvedVenv.StartsWith($requiredPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $resolvedVenv) -ne ".venv") {
        throw "Refusing to remove unexpected virtual environment path: '$resolvedVenv'."
    }

    if (Test-Path -LiteralPath $resolvedVenv) {
        Write-Host "Recreating the project-local hand tracking environment because its inputs changed."
        Remove-Item -LiteralPath $resolvedVenv -Recurse -Force
    }
}

$py = Get-Command py -ErrorAction SilentlyContinue
$python = Get-Command python -ErrorAction SilentlyContinue
$basePython = $null
$basePythonArgs = @()
$basePythonVersion = $null

if ($py) {
    $candidateVersion = Get-PythonVersion $py.Source @("-3.11")
    if ($candidateVersion -and $candidateVersion.StartsWith("3.11.")) {
        $basePython = $py.Source
        $basePythonArgs = @("-3.11")
        $basePythonVersion = $candidateVersion
    }
}

if (-not $basePython -and $python) {
    $candidateVersion = Get-PythonVersion $python.Source
    if ($candidateVersion -and $candidateVersion.StartsWith("3.11.")) {
        $basePython = $python.Source
        $basePythonVersion = $candidateVersion
    }
}

if (-not $basePython) {
    throw "Python 3.11 is required. Install Python 3.11 and confirm 'py -3.11 --version' works."
}

$requirementsHash = (Get-FileHash -LiteralPath $requirements -Algorithm SHA256).Hash.ToLowerInvariant()
$expectedEnvironmentMarker = @(
    "schema=$environmentSchemaVersion",
    "python=$basePythonVersion",
    "pip=$pipVersion",
    "requirements=$requirementsHash"
) -join "`n"

$environmentIsCurrent = $false
if ((Test-Path -LiteralPath $venvPython) -and (Test-Path -LiteralPath $environmentMarker)) {
    $venvPythonVersion = Get-PythonVersion $venvPython
    $actualEnvironmentMarker = Get-Content -LiteralPath $environmentMarker -Raw
    $environmentIsCurrent = (
        $venvPythonVersion -eq $basePythonVersion -and
        $actualEnvironmentMarker -eq $expectedEnvironmentMarker
    )
}

if ($environmentIsCurrent) {
    Write-Host "Hand tracking Python environment is up to date."
    return
}

Remove-HandTrackingVenv
Invoke-BasePython -m venv $venv
Invoke-VenvPython -m pip install --disable-pip-version-check "pip==$pipVersion"
Invoke-VenvPython -m pip install --disable-pip-version-check -r $requirements
Invoke-VenvPython -c "import cv2, mediapipe, PyInstaller"

Set-Content -LiteralPath $environmentMarker -Value $expectedEnvironmentMarker -Encoding ASCII -NoNewline
Write-Host "Prepared the project-local hand tracking environment."
