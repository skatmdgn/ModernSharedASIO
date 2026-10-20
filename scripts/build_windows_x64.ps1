# Manual Windows x64 validation ONLY. No registration, installation or Cubase use.
param(
  [Parameter(Mandatory=$true)][string] $AsioSdkDir,
  [ValidateSet('Debug','Release')][string] $Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not (Test-Path (Join-Path $AsioSdkDir 'common\iasiodrv.h'))) { throw 'Official ASIO SDK headers not found' }
$build = Join-Path $root 'build-win'
& cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 '-DMSA_BUILD_ASIO_DLL=ON' "-DASIO_SDK_DIR=$AsioSdkDir"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
& cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Windows build failed' }
& ctest --test-dir $build -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Core or COM preflight tests failed' }
Write-Host 'Build and registry-free preflight completed; DLL remains experimental.'
$dll=Join-Path $build "$Configuration\ModernSharedASIO.dll"
$reg='Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{6FA63F59-A581-4D9A-B8CB-73C3BD3A6D46}\InprocServer32'
try {
    $installed=(Get-Item -LiteralPath $reg -ErrorAction Stop).GetValue('')
    if(-not [string]::Equals([IO.Path]::GetFullPath([string]$installed),[IO.Path]::GetFullPath([string]$dll),[StringComparison]::OrdinalIgnoreCase)) {
        Write-Warning "Cubase is registered to load a different DLL: $installed"
        Write-Warning 'Build and tests do not install or register the new DLL. Run scripts\diagnose_output_route.ps1 for details.'
    } else {
        Write-Host 'Registered COM DLL path matches built DLL path. Restart Cubase after build.'
    }
} catch { Write-Warning 'ModernSharedASIO registration could not be verified.' }
Write-Host 'DO NOT register or use in Cubase before manual Windows/audio tests.'
