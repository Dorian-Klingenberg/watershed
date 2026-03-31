param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = 'Stop'

$bootstrapPath = Join-Path $RepoRoot '.codex\bootstrap.json'
if (-not (Test-Path $bootstrapPath)) {
    throw "Missing bootstrap file: $bootstrapPath"
}

$bootstrap = Get-Content $bootstrapPath -Raw | ConvertFrom-Json

if ($bootstrap.schema -ne 'thegame.codex_bootstrap.v1') {
    throw "Unexpected bootstrap schema: $($bootstrap.schema)"
}

$requiredFiles = @(
    'AGENTS.md',
    'projects/grannys-house-trials/AGENT_CONTEXT.json',
    'projects/grannys-house-trials/STATUS.md',
    'projects/grannys-house-trials/README.md'
)

foreach ($relativePath in $requiredFiles) {
    $fullPath = Join-Path $RepoRoot $relativePath
    if (-not (Test-Path $fullPath)) {
        throw "Bootstrap references missing file: $relativePath"
    }
}

if ($bootstrap.load_policy.mode -ne 'narrow') {
    throw "Bootstrap load policy should remain narrow."
}

if ($bootstrap.focused_context_files.Count -gt 8) {
    throw "Bootstrap is too broad; keep focused_context_files small."
}

Write-Host "Codex bootstrap lint passed."
