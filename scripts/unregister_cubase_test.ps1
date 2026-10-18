# Unregister only the independent Modern Shared Audio experimental entry.
# Run in elevated 64-bit PowerShell after closing Cubase.
param(
    [ValidateSet('Debug','Release')][string] $Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Open PowerShell as Administrator.'
}
if (Get-Process -Name 'Cubase8','Cubase 8','Cubase' -ErrorAction SilentlyContinue) {
    throw 'Close Cubase before unregistering.'
}
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dll = Join-Path $root "build-win\$Configuration\ModernSharedASIO.dll"
$guid = '{6FA63F59-A581-4D9A-B8CB-73C3BD3A6D46}'
$asioKey = 'HKLM:\SOFTWARE\ASIO\Modern Shared Audio'
$clsidKey = "HKLM:\SOFTWARE\Classes\CLSID\$guid"
$inprocKey = "$clsidKey\InprocServer32"
$asioExists = Test-Path -LiteralPath $asioKey
$clsidExists = Test-Path -LiteralPath $clsidKey
if (-not $asioExists -and -not $clsidExists) {
    Write-Host 'Already unregistered; no registry changes.'
    exit 0
}
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Registered DLL is missing: $dll. Do not delete keys blindly."
}
if ($asioExists) {
    $registeredCLSID = (Get-Item -LiteralPath $asioKey).GetValue('CLSID')
    if (-not [string]::Equals([string]$registeredCLSID,$guid,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'ASIO CLSID mismatch. Refusing to alter unrelated registration.'
    }
}
if ($clsidExists) {
    if (-not (Test-Path -LiteralPath $inprocKey)) {
        throw 'InprocServer32 key missing. Manual inspection required.'
    }
    $server = (Get-Item -LiteralPath $inprocKey).GetValue('')
    if (-not [string]::Equals([string]$server,$dll,[StringComparison]::OrdinalIgnoreCase)) {
        throw "Registered COM DLL differs from test DLL ($server). Refusing to delete."
    }
}
$regsvr32 = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
& $regsvr32 /u /s $dll
if ($LASTEXITCODE -ne 0) { throw "Unregister failed: $LASTEXITCODE" }
if ((Test-Path -LiteralPath $asioKey) -or (Test-Path -LiteralPath $clsidKey)) {
    throw 'Experimental registry entries remain after unregister. Inspect before retrying.'
}
Write-Host 'UNREGISTERED: Modern Shared Audio only. Original Steinberg ASIO driver unchanged.'
