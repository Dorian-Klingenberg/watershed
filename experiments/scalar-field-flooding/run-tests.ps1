param(
    [switch]$Build
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$testExe = Join-Path $repoRoot "build\vs2026-debug\experiments\scalar-field-flooding\Debug\scalar_field_flooding_tests.exe"

if ($Build)
{
    Push-Location $repoRoot
    try
    {
        cmake --build --preset build-vs2026-debug-scalar-field-flooding-tests
    }
    finally
    {
        Pop-Location
    }
}

if (-not (Test-Path $testExe))
{
    throw "Test executable not found at '$testExe'. Run this script with -Build first."
}

& $testExe
