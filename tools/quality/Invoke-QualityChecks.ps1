[CmdletBinding()]
param(
    [ValidateSet("All", "Cppcheck", "Complexity", "ClangTidy", "Test")]
    [string[]]$Step = @("All"),

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Resolve-Executable {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [string[]]$Candidates = @()
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }

    throw "'$Name' was not found. See tools/quality/README.md for setup instructions."
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory)] [string]$Executable,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Arguments
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $Executable"
    }
}

function Invoke-MSBuildCommand {
    param(
        [Parameter(Mandatory)] [string]$Executable,
        [Parameter(Mandatory)] [string[]]$Arguments
    )

    # Some embedded Windows terminals expose both PATH and Path. MSBuild's .NET
    # tool launcher rejects that duplicate environment key, so let cmd normalize
    # the child environment before MSBuild starts compiler processes.
    $quotedExecutable = '"' + $Executable.Replace('"', '""') + '"'
    $quotedArguments = $Arguments | ForEach-Object {
        '"' + $_.Replace('"', '""') + '"'
    }
    $commandLine = "set Path=& $quotedExecutable $($quotedArguments -join ' ')"
    & $env:ComSpec /d /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE."
    }
}

function Write-Step {
    param([Parameter(Mandatory)] [string]$Message)
    Write-Host "`n== $Message ==" -ForegroundColor Cyan
}

function Invoke-Cppcheck {
    Write-Step "Cppcheck: bugs and dangerous code"
    $cppcheck = Resolve-Executable -Name "cppcheck" -Candidates @(
        "$env:ProgramFiles\Cppcheck\cppcheck.exe"
    )
    $buildDirectory = Join-Path $repositoryRoot "generated\analysis\cppcheck"
    New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null

    $arguments = @(
        "--enable=warning,performance,portability",
        "--check-level=exhaustive",
        "--error-exitcode=1",
        "--inline-suppr",
        "--language=c++",
        "--std=c++20",
        "--platform=win64",
        "--suppress=missingIncludeSystem",
        "--suppress=unmatchedSuppression",
        "--cppcheck-build-dir=$buildDirectory",
        "-I", (Join-Path $repositoryRoot "app\include"),
        "-I", (Join-Path $repositoryRoot "engine\include"),
        (Join-Path $repositoryRoot "app\src"),
        (Join-Path $repositoryRoot "app\include"),
        (Join-Path $repositoryRoot "engine\src"),
        (Join-Path $repositoryRoot "engine\include"),
        (Join-Path $repositoryRoot "engine\public")
    )
    Invoke-CheckedCommand -Executable $cppcheck -Arguments $arguments
}

function Invoke-Complexity {
    Write-Step "Lizard: function complexity"
    $lizard = Resolve-Executable -Name "lizard" -Candidates @(
        (Join-Path $env:USERPROFILE "miniconda3\Scripts\lizard.exe"),
        (Join-Path $env:APPDATA "Python\Python313\Scripts\lizard.exe"),
        (Join-Path $env:APPDATA "Python\Python312\Scripts\lizard.exe")
    )
    $arguments = @(
        "-l", "cpp",
        "-C", "15",
        "--warning-msvs",
        "-i", "0",
        "-t", "1",
        (Join-Path $repositoryRoot "app\src"),
        (Join-Path $repositoryRoot "app\include"),
        (Join-Path $repositoryRoot "engine\src"),
        (Join-Path $repositoryRoot "engine\include"),
        (Join-Path $repositoryRoot "engine\public")
    )
    Invoke-CheckedCommand -Executable $lizard -Arguments $arguments
}

function Resolve-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($installation) {
            $candidate = Join-Path $installation "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }
    return Resolve-Executable -Name "MSBuild"
}

function Invoke-ClangTidy {
    Write-Step "clang-tidy: C++ quality and readability"
    $msbuild = Resolve-MSBuild
    $clangTidy = Resolve-Executable -Name "clang-tidy" -Candidates @(
        "$env:ProgramFiles\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe",
        "$env:ProgramFiles\LLVM\bin\clang-tidy.exe"
    )
    $clangTidyDirectory = Split-Path -Parent $clangTidy

    foreach ($project in @("engine\Engine.vcxproj", "app\App.vcxproj")) {
        $projectPath = Join-Path $repositoryRoot $project
        $commonArguments = @(
            $projectPath,
            "/nodeReuse:false",
            "/nologo",
            "/v:minimal",
            "/p:Configuration=$Configuration",
            "/p:Platform=x64",
            "/p:BuildProjectReferences=false",
            "/p:BundleHandTrackingHelper=false"
        )
        Invoke-MSBuildCommand -Executable $msbuild -Arguments @(
            $commonArguments + "/t:Clean"
        )

        # Compile every owned translation unit without linking the application.
        # This keeps static analysis independent of optional runtime libraries.
        $analysisArguments = @(
            $commonArguments +
            "/t:ClCompile",
            "/p:RunCodeAnalysis=true",
            "/p:EnableProjectClangTidy=true",
            "/p:ClangTidyToolPath=$clangTidyDirectory"
        )
        Invoke-MSBuildCommand -Executable $msbuild -Arguments $analysisArguments
    }
}

function Invoke-Tests {
    Write-Step "Tests: build and behavior regression"
    $msbuild = Resolve-MSBuild
    $testProject = Join-Path $repositoryRoot "tests\EngineTests\EngineTests.vcxproj"
    $arguments = @(
        $testProject,
        "/t:Build",
        "/m",
        "/nodeReuse:false",
        "/nologo",
        "/v:minimal",
        "/p:Configuration=$Configuration",
        "/p:Platform=x64"
    )
    Invoke-MSBuildCommand -Executable $msbuild -Arguments $arguments

    $testExecutable = Join-Path $repositoryRoot "generated\outputs\x64\$Configuration\EngineTests\EngineTests.exe"
    if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
        throw "Test executable was not produced: $testExecutable"
    }
    Invoke-CheckedCommand -Executable $testExecutable -Arguments @()
}

$requestedSteps = if ($Step -contains "All") {
    @("Cppcheck", "Complexity", "ClangTidy", "Test")
} else {
    $Step
}

$failures = [System.Collections.Generic.List[string]]::new()
foreach ($requestedStep in $requestedSteps) {
    try {
        switch ($requestedStep) {
            "Cppcheck" { Invoke-Cppcheck }
            "Complexity" { Invoke-Complexity }
            "ClangTidy" { Invoke-ClangTidy }
            "Test" { Invoke-Tests }
        }
    } catch {
        $failures.Add("${requestedStep}: $($_.Exception.Message)")
        Write-Error $_.Exception.Message -ErrorAction Continue
    }
}

if ($failures.Count -gt 0) {
    Write-Host "`nQuality checks failed:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

Write-Host "`nAll requested quality checks passed." -ForegroundColor Green
