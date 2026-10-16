[CmdletBinding()]
param(
    [string]$Version = 'latest',
    [string]$Destination,
    [string]$ProjectDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = 'https://github.com/jfbilodeau/Nomad'
if ($env:OS -ne 'Windows_NT' -or [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
    throw 'This installer supports Windows x64 only.'
}
if ($Version -eq 'latest') {
    try {
        $release = Invoke-RestMethod 'https://api.github.com/repos/jfbilodeau/Nomad/releases/latest' -TimeoutSec 180
    } catch {
        throw "Cannot resolve the latest stable release. Specify -Version explicitly to install a prerelease. $($_.Exception.Message)"
    }
    if ($release.draft -or $release.prerelease) {
        throw 'GitHub did not return a stable published release.'
    }
    $Version = $release.tag_name
}
$Version = $Version -replace '^v', ''
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw 'Version must be latest or major.minor.patch (optionally prefixed with v).'
}
if (-not $Destination) {
    $nomadHome = if ($env:NOMAD_HOME) {
        $env:NOMAD_HOME
    } else {
        Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Nomad'
    }
    $Destination = Join-Path $nomadHome "sdks\$Version\windows-x64"
}
$Destination = [System.IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $Destination) {
    throw "Installation already exists: $Destination"
}
$wrapperNames = @('nomadw', 'nomadw.cmd', 'nomadw.ps1')
if ($ProjectDirectory) {
    $ProjectDirectory = [System.IO.Path]::GetFullPath($ProjectDirectory)
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectDirectory 'nomad.toml') -PathType Leaf)) {
        throw "Project directory does not contain nomad.toml: $ProjectDirectory"
    }
    foreach ($wrapperName in $wrapperNames) {
        $wrapperDestination = Join-Path $ProjectDirectory $wrapperName
        if (Test-Path -LiteralPath $wrapperDestination) {
            throw "Refusing to overwrite project wrapper: $wrapperDestination"
        }
    }
}
$parent = Split-Path -Parent $Destination
[System.IO.Directory]::CreateDirectory($parent) | Out-Null
$staging = Join-Path $parent ('.nomad-install-' + [guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($staging) | Out-Null
try {
    $name = "nomad-sdk-windows-x64-$Version.zip"
    $base = "$repository/releases/download/v$Version"
    $archive = Join-Path $staging $name
    $checksums = Join-Path $staging 'SHA256SUMS.txt'
    Invoke-WebRequest "$base/SHA256SUMS.txt" -OutFile $checksums -UseBasicParsing -TimeoutSec 180
    Invoke-WebRequest "$base/$name" -OutFile $archive -UseBasicParsing -TimeoutSec 180
    $entries = @(Get-Content -LiteralPath $checksums | Where-Object {
        $_ -match ('^[0-9a-fA-F]{64}  ' + [regex]::Escape($name) + '$')
    })
    if ($entries.Count -ne 1) {
        throw "Expected exactly one checksum for $name"
    }
    $expected = $entries[0].Substring(0, 64)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($archive)
    try {
        $actual = [System.BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '')
    } finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
    if ($actual -ne $expected) {
        throw "Checksum mismatch for $name"
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($entry in $zip.Entries) {
            $path = $entry.FullName
            $mode = ($entry.ExternalAttributes -shr 16) -band 0xF000
            if ($path -match '(^[/\\]|\\|:|(^|/)\.\.(/|$))' -or $mode -eq 0xA000) {
                throw "Unsafe archive entry: $path"
            }
        }
    } finally {
        $zip.Dispose()
    }
    $sdk = Join-Path $staging 'sdk'
    [System.IO.Compression.ZipFile]::ExtractToDirectory($archive, $sdk)
    foreach ($required in @('nomad.exe', 'nomad-lsp.exe', 'nomadc.exe', 'nomad-runtime.exe', 'templates', 'runtime\runtime.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sdk $required))) {
            throw "Missing SDK file: $required"
        }
    }
    $manifest = Get-Content -LiteralPath (Join-Path $sdk 'runtime\runtime.json') -Raw | ConvertFrom-Json
    if ($manifest.version -ne $Version -or $manifest.target -ne 'windows-x64') {
        throw 'SDK runtime version or target does not match the requested installation.'
    }
    [System.IO.Directory]::Move($sdk, $Destination)
    if ($ProjectDirectory) {
        $wrapperSource = Join-Path $Destination 'templates\projects\default'
        foreach ($wrapperName in $wrapperNames) {
            $source = Join-Path $wrapperSource $wrapperName
            if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                throw "Installed SDK does not contain project wrapper: $source"
            }
        }
        foreach ($wrapperName in $wrapperNames) {
            Copy-Item -LiteralPath (Join-Path $wrapperSource $wrapperName) `
                -Destination (Join-Path $ProjectDirectory $wrapperName)
        }
        Write-Host "Installed Nomad wrappers in $ProjectDirectory"
    }
    Write-Host "Installed Nomad $Version to $Destination"
    Write-Host "Add this directory to PATH, or run: & '$($Destination.Replace("'", "''"))\nomad.exe' --version"
} finally {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
