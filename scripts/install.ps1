[CmdletBinding()]
param(
    [string]$Version = 'latest',
    [string]$Destination
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
    $Destination = Join-Path $HOME ".nomad\sdks\$Version\windows-x64"
}
$Destination = [System.IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $Destination) {
    throw "Installation already exists: $Destination"
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
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
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
    foreach ($required in @('nomad.exe', 'nomadc.exe', 'nomad-runtime.exe', 'templates', 'runtime\runtime.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sdk $required))) {
            throw "Missing SDK file: $required"
        }
    }
    $manifest = Get-Content -LiteralPath (Join-Path $sdk 'runtime\runtime.json') -Raw | ConvertFrom-Json
    if ($manifest.version -ne $Version -or $manifest.target -ne 'windows-x64') {
        throw 'SDK runtime version or target does not match the requested installation.'
    }
    [System.IO.Directory]::Move($sdk, $Destination)
    Write-Host "Installed Nomad $Version to $Destination"
    Write-Host "Add this directory to PATH, or run: & '$($Destination.Replace("'", "''"))\nomad.exe' --version"
} finally {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
