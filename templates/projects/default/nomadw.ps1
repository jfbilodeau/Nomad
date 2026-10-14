$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Stop-NomadWrapper([string]$Message) {
    throw "nomadw: $Message"
}

function Write-NomadWrapperStatus([string]$Message) {
    Write-Host "nomadw: $Message"
}

$projectFile = Join-Path $PSScriptRoot 'nomad.toml'
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
    Stop-NomadWrapper "Missing project configuration: $projectFile"
}

$section = ''
$version = $null
foreach ($line in Get-Content -LiteralPath $projectFile) {
    $trimmed = $line.Trim()
    if (-not $trimmed -or $trimmed.StartsWith('#')) {
        continue
    }
    if ($trimmed -match '^\[([^\]]+)\]\s*(?:#.*)?$') {
        $section = $Matches[1]
        continue
    }
    if ($section -eq 'nomad' -and $trimmed -match '^version') {
        if ($trimmed -notmatch '^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"\s*(?:#.*)?$') {
            Stop-NomadWrapper 'The [nomad].version value must use the canonical major.minor.patch form.'
        }
        if ($version) {
            Stop-NomadWrapper 'The project defines [nomad].version more than once.'
        }
        $version = $Matches[1]
    }
}
if (-not $version) {
    Stop-NomadWrapper 'The project does not define [nomad].version.'
}

if ($env:OS -ne 'Windows_NT' -or
    [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
    Stop-NomadWrapper 'This wrapper currently supports Windows x64 only.'
}
$target = 'windows-x64'

$downloadPolicy = if ([string]::IsNullOrEmpty($env:NOMAD_DOWNLOAD_POLICY)) {
    'prompt'
} else {
    $env:NOMAD_DOWNLOAD_POLICY
}
if ($downloadPolicy -cnotin @('prompt', 'allow', 'never')) {
    Stop-NomadWrapper 'NOMAD_DOWNLOAD_POLICY must be prompt, allow, or never.'
}

if ($env:NOMAD_HOME) {
    $nomadHome = $env:NOMAD_HOME
} else {
    $nomadHome = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Nomad'
}
$sdk = Join-Path $nomadHome "sdks\$version\$target"
$legacySdk = Join-Path $HOME ".nomad\sdks\$version\$target"
if (-not $env:NOMAD_HOME -and
    -not (Test-Path -LiteralPath (Join-Path $sdk 'nomad.exe') -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $legacySdk 'nomad.exe') -PathType Leaf)) {
    $sdk = $legacySdk
}

function Test-NomadSdk([string]$Directory) {
    foreach ($required in @('nomad.exe', 'nomadc.exe', 'nomad-runtime.exe', 'templates', 'runtime\runtime.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Directory $required))) {
            return $false
        }
    }
    try {
        $manifest = Get-Content -LiteralPath (Join-Path $Directory 'runtime\runtime.json') -Raw |
            ConvertFrom-Json
    } catch {
        return $false
    }
    return $manifest.version -eq $version -and $manifest.target -eq $target
}

if (-not (Test-NomadSdk $sdk)) {
    if ($downloadPolicy -eq 'never') {
        Stop-NomadWrapper "Nomad SDK $version for $target is not installed and NOMAD_DOWNLOAD_POLICY=never."
    }

    if ($downloadPolicy -eq 'prompt') {
        $response = Read-Host "Nomad SDK $version for $target is not installed. Download and install it? [y/N]"
        if (-not $response -or $response.Trim().ToLowerInvariant() -notin @('y', 'yes')) {
            Stop-NomadWrapper 'SDK download was not approved.'
        }
    }

    $parent = Split-Path -Parent $sdk
    [System.IO.Directory]::CreateDirectory($parent) | Out-Null
    $lock = "$sdk.install-lock"
    try {
        New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null
    } catch {
        if (Test-NomadSdk $sdk) {
            & (Join-Path $sdk 'nomad.exe') @args
            exit $LASTEXITCODE
        }
        Stop-NomadWrapper "Another process is installing Nomad SDK $version for $target."
    }

    $staging = Join-Path $parent ('.nomad-install-' + [guid]::NewGuid().ToString('N'))
    [System.IO.Directory]::CreateDirectory($staging) | Out-Null
    try {
        $name = "nomad-sdk-$target-$version.zip"
        $releaseRoot = if ($env:NOMAD_RELEASE_BASE_URL) {
            $env:NOMAD_RELEASE_BASE_URL.TrimEnd('/')
        } else {
            'https://github.com/jfbilodeau/Nomad/releases/download'
        }
        $base = "$releaseRoot/v$version"
        $archive = Join-Path $staging $name
        $checksums = Join-Path $staging 'SHA256SUMS.txt'
        Write-NomadWrapperStatus "Downloading Nomad SDK $version for $target..."
        try {
            Invoke-WebRequest "$base/SHA256SUMS.txt" -OutFile $checksums -UseBasicParsing -TimeoutSec 180
        } catch {
            Stop-NomadWrapper "Failed to download release checksums from $base/SHA256SUMS.txt. Confirm that Nomad SDK $version for $target is published and that the URL is reachable."
        }
        try {
            Invoke-WebRequest "$base/$name" -OutFile $archive -UseBasicParsing -TimeoutSec 180
        } catch {
            Stop-NomadWrapper "Failed to download Nomad SDK archive from $base/$name. Confirm that Nomad SDK $version for $target is published and that the URL is reachable."
        }

        Write-NomadWrapperStatus "Verifying Nomad SDK $version for $target..."
        $entries = @(Get-Content -LiteralPath $checksums | Where-Object {
            $_ -match ('^[0-9a-fA-F]{64}  ' + [regex]::Escape($name) + '$')
        })
        if ($entries.Count -ne 1) {
            Stop-NomadWrapper "Expected exactly one checksum for $name."
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
            Stop-NomadWrapper "Checksum mismatch for $name."
        }

        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
        try {
            foreach ($entry in $zip.Entries) {
                $path = $entry.FullName
                $mode = ($entry.ExternalAttributes -shr 16) -band 0xF000
                if ($path -match '(^[/\\]|\\|:|(^|/)\.\.(/|$))' -or $mode -eq 0xA000) {
                    Stop-NomadWrapper "Unsafe archive entry: $path"
                }
            }
        } finally {
            $zip.Dispose()
        }

        $extracted = Join-Path $staging 'sdk'
        [System.IO.Compression.ZipFile]::ExtractToDirectory($archive, $extracted)
        if (-not (Test-NomadSdk $extracted)) {
            Stop-NomadWrapper "Downloaded SDK manifest or required files do not match $version for $target."
        }
        if (Test-Path -LiteralPath $sdk) {
            Stop-NomadWrapper "Invalid SDK installation already exists: $sdk"
        }
        [System.IO.Directory]::Move($extracted, $sdk)
        Write-NomadWrapperStatus "Installed Nomad SDK $version for $target in $sdk."
    } finally {
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $lock -Force -ErrorAction SilentlyContinue
    }
}

& (Join-Path $sdk 'nomad.exe') @args
exit $LASTEXITCODE
