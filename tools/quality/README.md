# C++ quality checks

The repository has one entry point for four complementary checks:

| Step | Purpose | Gate |
|---|---|---|
| Cppcheck | Bugs, undefined behavior, portability risks | Warning/performance/portability findings |
| Lizard | Function complexity | Cyclomatic complexity (CCN) > 15 |
| clang-tidy | C++ quality and readability | Analyzer, bugprone, performance, readability checks |
| Test | Behavior regression | Build and run `EngineTests.exe` |

Third-party code under `engine/externals` is excluded from project-owned source scans.

## Run

From the repository root:

```powershell
./tools/quality/Invoke-QualityChecks.ps1
```

Run one or more steps while iterating:

```powershell
./tools/quality/Invoke-QualityChecks.ps1 -Step Cppcheck,Complexity
./tools/quality/Invoke-QualityChecks.ps1 -Step ClangTidy -Configuration Release
./tools/quality/Invoke-QualityChecks.ps1 -Step Test
```

The script locates Visual Studio through `vswhere` and also checks common install
locations for the analysis tools. Required local tools are:

- Visual Studio with Desktop development with C++ and C++ Clang tools for Windows
- Cppcheck (`winget install Cppcheck.Cppcheck`)
- Lizard (`py -m pip install lizard`)

Generated analyzer state and binaries are written below `generated/`, which is
already ignored by Git.
