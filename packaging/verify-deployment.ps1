<#
.SYNOPSIS
    Verifies a canonical deployment directory before it is packaged.

.DESCRIPTION
    Checks that the deployment produced by `cmake --install` is self-contained
    and free of development artifacts, so the same tree can be turned into the
    installer and the portable ZIP with confidence. Every check is reported;
    the script exits non-zero if any required check fails.

.PARAMETER DeploymentDir
    Path to the canonical deployment directory (the folder containing the
    application executable).

.PARAMETER ExeName
    Base name of the main executable, without the .exe extension.
    Defaults to the exeName field of packaging/product.json.

.PARAMETER RequireOpenSsl
    When set, missing OpenSSL runtime DLLs are treated as a failure. Defaults to
    true because the application uses secure OPC UA connections.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $DeploymentDir,
    [string] $ExeName,
    [bool]   $RequireOpenSsl = $true
)

$ErrorActionPreference = 'Stop'

if (-not $ExeName) {
    $productJson = Join-Path $PSScriptRoot 'product.json'
    if (Test-Path $productJson) {
        $ExeName = (Get-Content $productJson -Raw | ConvertFrom-Json).exeName
    }
}
if (-not $ExeName) { $ExeName = 'appOpcUaManager' }

$errors = New-Object System.Collections.Generic.List[string]
$checks = New-Object System.Collections.Generic.List[string]

function Test-Item {
    param([string] $RelPath, [string] $Label)
    $full = Join-Path $DeploymentDir $RelPath
    if (Test-Path $full) {
        $checks.Add("OK   : $Label ($RelPath)")
    } else {
        $errors.Add("FAIL : $Label is missing ($RelPath)")
    }
}

# Qt plugins live under plugins/ when windeployqt writes a qt.conf, or flat at
# the deployment root otherwise. Resolve a plugin path against both.
$pluginRoots = @($DeploymentDir, (Join-Path $DeploymentDir 'plugins')) |
    Where-Object { Test-Path $_ }

function Resolve-Plugin {
    param([string] $RelPath)
    foreach ($root in $pluginRoots) {
        $candidate = Join-Path $root $RelPath
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Test-Plugin {
    param([string] $RelPath, [string] $Label)
    if (Resolve-Plugin $RelPath) {
        $checks.Add("OK   : $Label ($RelPath)")
    } else {
        $errors.Add("FAIL : $Label is missing ($RelPath)")
    }
}

if (-not (Test-Path $DeploymentDir)) {
    Write-Error "Deployment directory not found: $DeploymentDir"
    exit 2
}

# 1. Main executable.
Test-Item "$ExeName.exe" 'Main executable'

# 2. No debug Qt / backend DLLs (a release deployment must not ship debug runtime).
$debugDlls = Get-ChildItem -Path $DeploymentDir -Recurse -File -Filter '*d.dll' |
    Where-Object { $_.Name -match '^(Qt6.*d\.dll|open62541_backendd\.dll)$' }
if ($debugDlls) {
    foreach ($d in $debugDlls) { $errors.Add("FAIL : debug DLL present ($($d.Name))") }
} else {
    $checks.Add('OK   : no debug Qt/backend DLLs')
}

# 3. Required Qt plugins (under plugins/ or flat at the root).
Test-Plugin 'platforms/qwindows.dll'       'Windows platform plugin'
Test-Plugin 'sqldrivers/qsqlite.dll'       'SQLite driver plugin'
Test-Plugin 'opcua/open62541_backend.dll'  'OPC UA open62541 backend plugin'

$imageFormatsDir = Resolve-Plugin 'imageformats'
$imageFormats = @()
if ($imageFormatsDir) {
    $imageFormats = Get-ChildItem -Path $imageFormatsDir -File -Filter '*.dll'
}
if ($imageFormats.Count -gt 0) {
    $checks.Add("OK   : image format plugins ($($imageFormats.Count) file(s))")
} else {
    $errors.Add('FAIL : no image format plugins under (plugins/)imageformats/')
}

# A TLS backend must exist; on Windows Qt 6.11 windeployqt ships the schannel
# backend rather than the OpenSSL one, which is fine (OPC UA security uses the
# OpenSSL DLLs directly via open62541, not Qt's TLS plugin).
$tlsDir = Resolve-Plugin 'tls'
$tlsBackends = @()
if ($tlsDir) { $tlsBackends = Get-ChildItem -Path $tlsDir -File -Filter '*.dll' }
if ($tlsBackends.Count -gt 0) {
    $checks.Add("OK   : TLS backend plugin(s) ($($tlsBackends.Count) file(s))")
} else {
    $errors.Add('FAIL : no TLS backend under (plugins/)tls/')
}

# 4. OpenSSL runtime DLLs.
$sslPresent    = Get-ChildItem -Path $DeploymentDir -File -Filter 'libssl-3*.dll'    -ErrorAction SilentlyContinue
$cryptoPresent = Get-ChildItem -Path $DeploymentDir -File -Filter 'libcrypto-3*.dll' -ErrorAction SilentlyContinue
if ($sslPresent -and $cryptoPresent) {
    $checks.Add('OK   : OpenSSL 3 runtime DLLs present')
} elseif ($RequireOpenSsl) {
    $errors.Add('FAIL : OpenSSL 3 runtime DLLs (libssl-3*/libcrypto-3*) missing')
} else {
    $checks.Add('WARN : OpenSSL 3 runtime DLLs missing (not required)')
}

# 5. Seed data and licensing materials.
Test-Item 'db/opcua_nodes.db'       'Seed node database'
Test-Item 'pki'                     'PKI skeleton'
Test-Item 'LICENSE'                 'Project license'
Test-Item 'THIRD_PARTY_NOTICES.md'  'Third-party notices'
$licensesDir = Join-Path $DeploymentDir 'licenses'
if ((Test-Path $licensesDir) -and (Get-ChildItem $licensesDir -File)) {
    $checks.Add('OK   : bundled license texts (licenses/)')
} else {
    $errors.Add('FAIL : licenses/ directory missing or empty')
}

# 6. No development artifacts leaking into the deployment.
$devArtifacts = Get-ChildItem -Path $DeploymentDir -Recurse -File |
    Where-Object { $_.Extension -in '.obj', '.ilk', '.pdb', '.exp', '.lib' -or
                   $_.Name -in 'CMakeCache.txt', 'build.ninja' }
if ($devArtifacts) {
    foreach ($a in $devArtifacts) { $errors.Add("FAIL : development artifact present ($($a.Name))") }
} else {
    $checks.Add('OK   : no development artifacts')
}

Write-Host ''
Write-Host "Deployment verification: $DeploymentDir"
foreach ($c in $checks) { Write-Host "  $c" }
if ($errors.Count -gt 0) {
    Write-Host ''
    foreach ($e in $errors) { Write-Host "  $e" -ForegroundColor Red }
    Write-Host ''
    Write-Error "Deployment verification FAILED with $($errors.Count) error(s)."
    exit 1
}

Write-Host ''
Write-Host 'Deployment verification passed.' -ForegroundColor Green
exit 0
