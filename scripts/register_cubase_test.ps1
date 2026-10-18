# Register the isolated x64 experimental driver for Cubase testing.
# Run in elevated 64-bit PowerShell after closing Cubase.
param(
    [ValidateSet('Debug','Release')][string] $Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) {
    throw '64-bit PowerShell is required; 32-bit registry view is not supported.'
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Open PowerShell as Administrator before registration.'
}
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dll = Join-Path $root "build-win\$Configuration\ModernSharedASIO.dll"
$preflight = Join-Path $root "build-win\$Configuration\modern_asio_preflight.exe"
$guid = '{6FA63F59-A581-4D9A-B8CB-73C3BD3A6D46}'
$asioKey = 'HKLM:\SOFTWARE\ASIO\Modern Shared Audio'
$clsidKey = "HKLM:\SOFTWARE\Classes\CLSID\$guid"
$inprocKey = "$clsidKey\InprocServer32"
if (-not (Test-Path -LiteralPath $dll)) { throw "DLL not found: $dll" }
if (-not (Test-Path -LiteralPath $preflight)) { throw "Preflight not found: $preflight" }
if (Test-Path -LiteralPath $asioKey) {
    throw "ASIO entry already exists: $asioKey. Refusing to overwrite it."
}
if (Test-Path -LiteralPath $clsidKey) {
    throw "CLSID already exists: $clsidKey. Refusing to overwrite it."
}
if (Get-Process -Name 'Cubase8','Cubase 8','Cubase' -ErrorAction SilentlyContinue) {
    throw 'Please close Cubase before registering a new ASIO driver.'
}

Write-Host 'Running registry-free COM preflight...'
& $preflight $dll
if ($LASTEXITCODE -ne 0) { throw "COM preflight failed (exit $LASTEXITCODE)." }

# Preserve a read-only snapshot of existing ASIO registrations.
# Do NOT import this snapshot wholesale to restore: it may overwrite other changes.
$backupFolder = Join-Path $root 'build-win\registry-backup'
New-Item -ItemType Directory -Force -Path $backupFolder | Out-Null
if (Test-Path -LiteralPath 'HKLM:\SOFTWARE\ASIO') {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $backupFile = Join-Path $backupFolder "asio_before_modernsharedasio_$stamp.reg"
    & (Join-Path $env:SystemRoot 'System32\reg.exe') export 'HKLM\SOFTWARE\ASIO' $backupFile /y
    if ($LASTEXITCODE -ne 0) { throw "Registry backup failed (exit $LASTEXITCODE)." }
    Write-Host "Existing ASIO registrations backed up to: $backupFile"
}

$regsvr32 = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
Write-Host 'Registering new independent Modern Shared Audio CLSID...'
& $regsvr32 /s $dll
if ($LASTEXITCODE -ne 0) {
    throw "regsvr32 failed (exit $LASTEXITCODE); inspect experimental keys."
}
$server = (Get-Item -LiteralPath $inprocKey).GetValue('')
$registeredCLSID = (Get-Item -LiteralPath $asioKey).GetValue('CLSID')
if (-not [string]::Equals([string]$server,$dll,[StringComparison]::OrdinalIgnoreCase) -or
    -not [string]::Equals([string]$registeredCLSID,$guid,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Registry verification failed. Do not open Cubase until keys are inspected.'
}
Write-Host 'REGISTERED: Modern Shared Audio (x64), separate from the original Steinberg driver.'
Write-Host "Module: $dll"
Write-Host 'To revert: close Cubase and run scripts\unregister_cubase_test.ps1 with the same configuration.'
