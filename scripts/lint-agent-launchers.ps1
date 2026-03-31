param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = 'Stop'

$launcherPath = Join-Path $RepoRoot '.codex\launchers.md'
$readmePath = Join-Path $RepoRoot '.codex\README.md'
$agentsPath = Join-Path $RepoRoot 'AGENTS.md'

foreach ($path in @($launcherPath, $readmePath, $agentsPath)) {
    if (-not (Test-Path $path)) {
        throw "Missing required file: $path"
    }
}

$launcherText = Get-Content $launcherPath -Raw
$readmeText = Get-Content $readmePath -Raw
$agentsText = Get-Content $agentsPath -Raw

foreach ($needle in @(
    'bootstrap.json',
    'AGENT_CONTEXT.json',
    'WSL/bash',
    'Windows shells'
)) {
    if ($launcherText -notmatch [regex]::Escape($needle) -and $readmeText -notmatch [regex]::Escape($needle) -and $agentsText -notmatch [regex]::Escape($needle)) {
        throw "Expected launcher guidance to mention: $needle"
    }
}

Write-Host "Codex launcher lint passed."
