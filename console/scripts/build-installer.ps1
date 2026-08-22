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

function Get-Sha256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try { return ([System.BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Test-UnsignedPortableExecutable([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "$Path is not a PE file" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "$Path has an invalid PE signature" }
        $optionalHeader = $peOffset + 24
        $stream.Position = $optionalHeader
        $magic = $reader.ReadUInt16()
        $dataDirectory = if ($magic -eq 0x10B) { $optionalHeader + 96 } elseif ($magic -eq 0x20B) { $optionalHeader + 112 } else { throw "$Path has an unsupported PE optional-header format" }
        $stream.Position = $dataDirectory + (4 * 8)
        $certificateOffset = $reader.ReadUInt32()
        $certificateSize = $reader.ReadUInt32()
        return $certificateOffset -eq 0 -and $certificateSize -eq 0
    } finally { $reader.Dispose(); $stream.Dispose() }
}

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
    if (-not (Test-UnsignedPortableExecutable $setup[0].FullName)) { throw 'code-signing policy violation: Setup.exe contains an Authenticode certificate table' }

    Phase 'Installer verification complete. Artifacts are intentionally unsigned.'
    Get-ChildItem -LiteralPath $output -File | Sort-Object Name | ForEach-Object {
        $hash = Get-Sha256 $_.FullName
        Write-Host ("{0}  {1} bytes  sha256:{2}" -f $_.FullName, $_.Length, $hash)
    }
    Phase ("Installer build complete in {0:c}." -f ([DateTimeOffset]::UtcNow - $started))
    exit 0
} catch {
    Write-Error "Installer build failed after $(([DateTimeOffset]::UtcNow - $started).ToString('c')): $($_.Exception.Message)"
    exit 1
}
