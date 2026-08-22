[CmdletBinding()]
param([switch]$Silent)

$ErrorActionPreference = 'Stop'
$started = [DateTimeOffset]::UtcNow
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$bootstrap = Join-Path $repoRoot 'download-dependencies.bat'
$node = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'dependency-manifest.json') | ConvertFrom-Json | Select-Object -ExpandProperty dependencies | Where-Object id -eq 'node-win-x64'
$nodeRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) ("DingPBX\toolchains\{0}" -f $node.archiveRoot)
$npm = Join-Path $nodeRoot 'npm.cmd'

function Phase([string]$Message) { Write-Host ("[{0:HH:mm:ss}] {1}" -f [DateTime]::Now, $Message) }

try {
    Phase 'Bootstrapping all build dependencies.'
    $bootstrapArgs = @()
    if ($Silent) { $bootstrapArgs += '/s' }
    & $bootstrap @bootstrapArgs
    if ($LASTEXITCODE -ne 0) { throw "download-dependencies.bat exited $LASTEXITCODE" }
    $env:PATH = "$nodeRoot;$env:PATH"
    $env:CSC_IDENTITY_AUTO_DISCOVERY = 'false'
    $env:CSC_LINK = ''
    $env:CSC_KEY_PASSWORD = ''

    Phase 'Building the real console artifact through the project build script.'
    Push-Location (Join-Path $repoRoot 'console')
    try {
        & $npm run build
        if ($LASTEXITCODE -ne 0) { throw "npm run build exited $LASTEXITCODE" }
    } finally { Pop-Location }

    Phase ("Build complete in {0:c}." -f ([DateTimeOffset]::UtcNow - $started))
    if (-not $Silent) {
        $answer = Read-Host 'Run the console now? [y/N]'
        if ($answer -match '^(?i:y|yes)$') {
            Push-Location (Join-Path $repoRoot 'console')
            try { & $npm start; if ($LASTEXITCODE -ne 0) { throw "npm start exited $LASTEXITCODE" } }
            finally { Pop-Location }
        }
    }
    exit 0
} catch {
    Write-Error "Build failed after $(([DateTimeOffset]::UtcNow - $started).ToString('c')): $($_.Exception.Message)"
    exit 1
}
