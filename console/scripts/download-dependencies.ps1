[CmdletBinding()]
param([switch]$Silent)

$ErrorActionPreference = 'Stop'
$started = [DateTimeOffset]::UtcNow
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$manifestPath = Join-Path $repoRoot 'dependency-manifest.json'

function Write-Phase([string]$Message) {
    Write-Host ("[{0:HH:mm:ss}] {1}" -f [DateTime]::Now, $Message)
}

function Get-Sha256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

function Fail([string]$Dependency, [string]$Constraint, [string]$Source, [string]$Reason) {
    Write-Error "Dependency '$Dependency' ($Constraint) could not be obtained from '$Source': $Reason"
    exit 1
}

try {
    Write-Phase 'Reading the pinned dependency manifest.'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        Fail 'dependency manifest' 'schemaVersion 1' $manifestPath 'file is missing'
    }
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1) {
        Fail 'dependency manifest' 'schemaVersion 1' $manifestPath "unsupported schemaVersion $($manifest.schemaVersion)"
    }

    $node = @($manifest.dependencies | Where-Object id -eq 'node-win-x64')
    if ($node.Count -ne 1) {
        Fail 'Node.js' 'one exact node-win-x64 record' $manifestPath "found $($node.Count) records"
    }
    $node = $node[0]
    if ($node.sha256 -notmatch '^[0-9a-f]{64}$') {
        Fail 'Node.js' "version $($node.version)" $node.source 'manifest SHA-256 is malformed'
    }

    $toolchainRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'DingPBX\toolchains'
    $nodeRoot = Join-Path $toolchainRoot $node.archiveRoot
    $nodeExe = Join-Path $nodeRoot 'node.exe'
    $npmCmd = Join-Path $nodeRoot 'npm.cmd'
    $cacheRoot = Join-Path $toolchainRoot 'downloads'
    $archive = Join-Path $cacheRoot (Split-Path -Leaf $node.source)
    New-Item -ItemType Directory -Force -Path $toolchainRoot,$cacheRoot | Out-Null

    $usable = (Test-Path -LiteralPath $nodeExe -PathType Leaf) -and (Test-Path -LiteralPath $npmCmd -PathType Leaf)
    if ($usable) {
        $actual = (& $nodeExe --version).TrimStart('v')
        if ($actual -ne [string]$node.version) {
            Write-Phase "Cached Node.js reports $actual; pinned version is $($node.version), so it will be replaced."
            $usable = $false
        }
    }

    if (-not $usable) {
        Write-Phase "Obtaining Node.js $($node.version) from the canonical Node.js release service."
        $download = $true
        if (Test-Path -LiteralPath $archive -PathType Leaf) {
            $cachedHash = Get-Sha256 $archive
            $download = $cachedHash -ne $node.sha256
            if ($download) { Remove-Item -LiteralPath $archive -Force }
        }
        if ($download) {
            try { Invoke-WebRequest -UseBasicParsing -Uri $node.source -OutFile $archive }
            catch { Fail 'Node.js' "version $($node.version)" $node.source $_.Exception.Message }
        }
        $actualHash = Get-Sha256 $archive
        if ($actualHash -ne $node.sha256) {
            Fail 'Node.js' "SHA-256 $($node.sha256)" $node.source "received SHA-256 $actualHash"
        }
        $extractRoot = Join-Path $toolchainRoot ('.extract-' + [Guid]::NewGuid().ToString('N'))
        try {
            Expand-Archive -LiteralPath $archive -DestinationPath $extractRoot -Force
            $expanded = Join-Path $extractRoot $node.archiveRoot
            if (-not (Test-Path -LiteralPath (Join-Path $expanded 'node.exe') -PathType Leaf)) {
                Fail 'Node.js' "version $($node.version)" $node.source 'archive did not contain node.exe at the declared root'
            }
            if (Test-Path -LiteralPath $nodeRoot) { Remove-Item -LiteralPath $nodeRoot -Recurse -Force }
            Move-Item -LiteralPath $expanded -Destination $nodeRoot
        } finally {
            if (Test-Path -LiteralPath $extractRoot) { Remove-Item -LiteralPath $extractRoot -Recurse -Force }
        }
        Write-Phase "Installed Node.js to $nodeRoot."
    } else {
        Write-Phase "Reusing verified Node.js $($node.version) at $nodeRoot."
    }

    $env:PATH = "$nodeRoot;$env:PATH"
    $consoleRoot = Join-Path $repoRoot 'console'
    $packageJson = Join-Path $consoleRoot 'package.json'
    $lockfile = Join-Path $consoleRoot 'package-lock.json'
    if (-not (Test-Path -LiteralPath $packageJson -PathType Leaf)) {
        Fail 'console project dependencies' 'console/package.json' $packageJson 'manifest is missing'
    }
    if (-not (Test-Path -LiteralPath $lockfile -PathType Leaf)) {
        Fail 'console project dependencies' 'console/package-lock.json' $lockfile 'lockfile is missing; reproducible npm ci is required'
    }

    Write-Phase 'Installing exact console dependencies with npm ci.'
    Push-Location $consoleRoot
    try {
        & $npmCmd ci --no-audit --no-fund
        if ($LASTEXITCODE -ne 0) {
            Fail 'console project dependencies' 'versions in console/package-lock.json' 'https://registry.npmjs.org/' "npm ci exited $LASTEXITCODE"
        }
    } finally { Pop-Location }

    $elapsed = [DateTimeOffset]::UtcNow - $started
    Write-Phase ("Dependency bootstrap complete in {0:c}." -f $elapsed)
    exit 0
} catch {
    Write-Error "Dependency bootstrap failed after $(([DateTimeOffset]::UtcNow - $started).ToString('c')): $($_.Exception.Message)"
    exit 1
}
