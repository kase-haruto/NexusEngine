param([ValidateSet('Debug','Develop','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$source=Join-Path $workspace 'Project/ThirdParty/JoltPhysics/Build'
$output=Join-Path $workspace 'Generated/ThirdParty/JoltPhysics-5.6.0'
$cmake=(Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if(-not $cmake) {
    $locator=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $cmake=@(& $locator -latest -find 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')[0]
}
if(-not $cmake -or -not (Test-Path -LiteralPath $cmake)) { throw 'CMake is required to build the pinned Jolt dependency.' }
# 複数のSource Projectが同じ依存を要求しても、configureとarchive生成を競合させない。
$joltHash=[Security.Cryptography.SHA256]::Create()
try { $joltBuildKey=[BitConverter]::ToString($joltHash.ComputeHash([Text.Encoding]::UTF8.GetBytes($output.ToUpperInvariant()))).Replace('-','') }
finally { $joltHash.Dispose() }
$joltMutex=[Threading.Mutex]::new($false,('Local\NexusJolt_'+$joltBuildKey))
$joltMutexHeld=$false
try {
    try { $joltMutexHeld=$joltMutex.WaitOne(60000) }
    catch [Threading.AbandonedMutexException] { $joltMutexHeld=$true }
    if(-not $joltMutexHeld) { throw 'Timed out waiting for the Jolt dependency build.' }
# SDK利用者は既存NexusRuntime.libだけをリンクする。Source EngineのBuild時だけ依存を構築する。
$joltBuildConfiguration='Debug'
if($Configuration -eq 'Release') { $joltBuildConfiguration='Release' }
& $cmake -S $source -B $output -G 'Visual Studio 18 2026' -A x64 `
    -DTARGET_UNIT_TESTS=OFF -DTARGET_HELLO_WORLD=OFF -DTARGET_PERFORMANCE_TEST=OFF -DTARGET_SAMPLES=OFF -DTARGET_VIEWER=OFF `
    -DINTERPROCEDURAL_OPTIMIZATION=OFF -DENABLE_ALL_WARNINGS=OFF -DENABLE_INSTALL=OFF -DENABLE_OBJECT_STREAM=OFF `
    -DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF -DPROFILER_IN_DEBUG_AND_RELEASE=OFF -DFLOATING_POINT_EXCEPTIONS_ENABLED=OFF `
    -DJPH_USE_DX12=OFF -DJPH_USE_VK=OFF -DJPH_USE_MTL=OFF -DJPH_USE_CPU_COMPUTE=OFF `
    -DUSE_AVX=OFF -DUSE_AVX2=OFF -DUSE_AVX512=OFF -DUSE_SSE4_1=OFF -DUSE_SSE4_2=OFF `
    -DUSE_LZCNT=OFF -DUSE_TZCNT=OFF -DUSE_F16C=OFF -DUSE_FMADD=OFF -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=ON
if($LASTEXITCODE -ne 0) { throw 'Jolt configure failed.' }
$buildArguments=@('--build',$output,'--config',$joltBuildConfiguration,'--target','Jolt','--parallel','2')
& $cmake @buildArguments
if($LASTEXITCODE -ne 0) { throw 'Jolt build failed.' }
} finally {
    if($joltMutexHeld) { $joltMutex.ReleaseMutex() }
    $joltMutex.Dispose()
}
