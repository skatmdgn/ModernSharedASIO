# READ-ONLY ModernSharedASIO route/build/registration consistency report.
# Never sets registry keys, changes audio devices, or registers a driver.
param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDll=Join-Path $root "build-win\$Configuration\ModernSharedASIO.dll"
$clsid='{6FA63F59-A581-4D9A-B8CB-73C3BD3A6D46}'
$regPath="Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
Write-Host '=== ModernSharedASIO read-only output route diagnostic ==='
Write-Host "Expected latest build: $buildDll"
$registered=$null
try { $registered=(Get-Item -LiteralPath $regPath -ErrorAction Stop).GetValue('') }
catch { Write-Warning 'Modern Shared ASIO COM registration not found in HKLM 64-bit view.' }
Write-Host "Registered DLL: $(if($registered){$registered}else{'NOT FOUND'})"
$builtExists=Test-Path -LiteralPath $buildDll -PathType Leaf
$regExists=$registered -and (Test-Path -LiteralPath $registered -PathType Leaf)
Write-Host "Expected built DLL exists: $builtExists"
Write-Host "Registered DLL exists: $([bool]$regExists)"
if($registered -and $builtExists) {
    $actual=[IO.Path]::GetFullPath([string]$registered)
    $expected=[IO.Path]::GetFullPath([string]$buildDll)
    $samePath=[string]::Equals($actual,$expected,[StringComparison]::OrdinalIgnoreCase)
    Write-Host "Registered path matches current Debug/Release build: $samePath"
    if(-not $samePath -and $regExists) {
        $a=(Get-FileHash -LiteralPath $buildDll -Algorithm SHA256).Hash
        $b=(Get-FileHash -LiteralPath $registered -Algorithm SHA256).Hash
        Write-Host "Registered DLL binary equals current built DLL: $([string]::Equals($a,$b,[StringComparison]::OrdinalIgnoreCase))"
    }
    if(-not $samePath) { Write-Warning 'Build script does not install the DLL. Cubase may still load the registered older DLL.' }
}
$saved=$null
try { $saved=(Get-ItemProperty -Path 'HKCU:\Software\ModernSharedASIO' -Name OutputDeviceId -ErrorAction Stop).OutputDeviceId }
catch { }
Write-Host "Specific headset/output preference saved: $([bool](-not [string]::IsNullOrEmpty($saved)))"
Write-Host '(Device ID intentionally hidden from diagnostic output.)'
try {
    $cubases=@(Get-Process -ErrorAction Stop | Where-Object { $_.ProcessName -match '^(Cubase|Cubase8|Cubase 8)' })
    foreach($p in $cubases) {
        Write-Host "Active Cubase PID: $($p.Id) ($($p.ProcessName))"
        try {
            $mods=@($p.Modules | Where-Object { $_.ModuleName -ieq 'ModernSharedASIO.dll' })
            foreach($m in $mods) { Write-Host "Loaded Cubase DLL: $($m.FileName)" }
            if($mods.Count -eq 0) { Write-Host 'ModernSharedASIO.dll not visible in this process module list.' }
        } catch { Write-Warning 'Could not query Cubase loaded modules; process architecture/permissions may restrict inspection.' }
    }
} catch { Write-Warning 'Could not enumerate running processes.' }
$log=Join-Path $env:TEMP 'ModernSharedASIO-debug.log'
if(Test-Path -LiteralPath $log) {
    $last=Get-Content -LiteralPath $log | Where-Object { $_ -match '^ASIO_STOP ' } | Select-Object -Last 1
    if($last) {
        $fallback=[regex]::Match($last,'(?:^|\s)output_device_fallback=(\d+)')
        if($fallback.Success) { Write-Host "Last completed ASIO session used unavailable-device fallback: $($fallback.Groups[1].Value)" }
        else { Write-Warning 'Last ASIO_STOP log has no output_device_fallback field; it may be from an older driver build.' }
        foreach($name in @('host_nonzero_left','underrun_frames','stream_errors','last_hr')) {
            $match=[regex]::Match($last,"(?:^|\s)$name=([^\s]+)")
            if($match.Success) { Write-Host "Last ASIO_STOP $name = $($match.Groups[1].Value)" }
        }
    }
}
Write-Host '=== This command made no changes. Close/reopen Cubase after changing output preferences. ==='
