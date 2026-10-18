# Portable core/WASAPI x64 Release benchmark; NEVER registers a driver.
# Uses a separate build folder to preserve the Cubase-verified Debug DLL.
param([switch] $SkipTests)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $root 'build-benchmark'
& cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 '-DMSA_BUILD_ASIO_DLL=OFF' '-DMSA_BUILD_BENCHMARKS=ON' '-DBUILD_TESTING=ON'
if($LASTEXITCODE -ne 0) { throw 'Benchmark configure failed' }
& cmake --build $build --config Release --parallel
if($LASTEXITCODE -ne 0) { throw 'Benchmark Release build failed' }
if(-not $SkipTests) {
  & ctest --test-dir $build -C Release --output-on-failure
  if($LASTEXITCODE -ne 0) { throw 'Benchmark build unit tests failed' }
}
$dir = Join-Path $build 'Release'
$executables = @('modern_asio_bridge_bench.exe','modern_asio_native_float32_bench.exe','modern_asio_pcm_scan_bench.exe')
foreach($name in $executables) {
  $exe = Join-Path $dir $name
  if(-not (Test-Path $exe)) { throw "Benchmark executable missing: $exe" }
  Write-Host "Running $name (Release)"
  & $exe
  if($LASTEXITCODE -ne 0) { throw "Benchmark failed: $name" }
}
Write-Host 'Benchmark complete. No audio driver was registered or installed.'
