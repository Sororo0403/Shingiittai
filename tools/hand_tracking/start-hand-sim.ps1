$ErrorActionPreference = "Stop"

param(
    [ValidateSet("cross", "label-flip", "dropout", "noisy-cross")]
    [string]$Scenario = "cross",
    [switch]$ShowWindow
)

$script = Join-Path $PSScriptRoot "hand_udp_sender.py"
$venvPython = Join-Path $PSScriptRoot ".venv\Scripts\python.exe"

$args = @($script, "--simulate", "--simulate-scenario", $Scenario)
if ($ShowWindow) {
    $args += "--show-window"
}

if (Test-Path $venvPython) {
    & $venvPython @args
} else {
    & py -3.11 @args
}
