[CmdletBinding()]
param([switch]$Silent)

$ErrorActionPreference = 'Stop'
$started = [DateTimeOffset]::UtcNow
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$bootstrap = Join-Path $repoRoot 'download-dependencies.bat'
$node = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'dependency-manifest.json') | ConvertFrom-Json | Select-Object -ExpandProperty dependencies | Where-Object id -eq 'node-win-x64'
$nodeRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) ("DingPBX\toolchains\{0}" -f $node.archiveRoot)
$npm = Join-Path $nodeRoot 'npm.cmd'
$output = Join-Path $repoRoot 'console\dist\squirrel-windows\squirrel-windows'

function Phase([string]$Message) { Write-Host ("[{0:HH:mm:ss}] {1}" -f [DateTime]::Now, $Message) }

try {
    Phase 'Bootstrapping all packaging dependencies.'
    $bootstrapArgs = @()
    if ($Silent) { $bootstrapArgs += '/s' }
    & $bootstrap @bootstrapArgs
    if ($LASTEXITCODE -ne 0) { throw "download-dependencies.bat exited $LASTEXITCODE" }
    $env:PATH = "$nodeRoot;$env:PATH"
    $env:CSC_IDENTITY_AUTO_DISCOVERY = 'false'
    $env:CSC_LINK = ''
    $env:CSC_KEY_PASSWORD = ''

    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Recurse -Force }
    Phase 'Building the unsigned Squirrel.Windows installer through the project packaging script.'
    Push-Location (Join-Path $repoRoot 'console')
    try {
        & $npm run package:squirrel
        if ($LASTEXITCODE -ne 0) { throw "npm run package:squirrel exited $LASTEXITCODE" }
    } finally { Pop-Location }

    $setup = @(Get-ChildItem -LiteralPath $output -File -Filter '*Setup.exe')
    $releases = @(Get-ChildItem -LiteralPath $output -File -Filter 'RELEASES')
    $full = @(Get-ChildItem -LiteralPath $output -File -Filter '*-full.nupkg')
    if ($setup.Count -ne 1) { throw "expected exactly one Setup.exe under $output; found $($setup.Count)" }
    if ($releases.Count -ne 1) { throw "expected exactly one RELEASES under $output; found $($releases.Count)" }
    if ($full.Count -lt 1) { throw "expected at least one full .nupkg under $output; found none" }
    $releaseText = Get-Content -Raw -LiteralPath $releases[0].FullName
    foreach ($package in $full) {
        if ($releaseText -notmatch [regex]::Escape($package.Name)) { throw "RELEASES does not reference $($package.Name)" }
    }
    $signature = Get-AuthenticodeSignature -FilePath $setup[0].FullName
    if ($signature.Status -ne 'NotSigned') { throw "code-signing policy violation: Setup.exe status is $($signature.Status)" }

    Phase 'Installer verification complete. Artifacts are intentionally unsigned.'
    Get-ChildItem -LiteralPath $output -File | Sort-Object Name | ForEach-Object {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
        Write-Host ("{0}  {1} bytes  sha256:{2}" -f $_.FullName, $_.Length, $hash)
    }
    Phase ("Installer build complete in {0:c}." -f ([DateTimeOffset]::UtcNow - $started))
    exit 0
} catch {
    Write-Error "Installer build failed after $(([DateTimeOffset]::UtcNow - $started).ToString('c')): $($_.Exception.Message)"
    exit 1
}
