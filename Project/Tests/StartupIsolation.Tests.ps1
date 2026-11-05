param([string]$Executable = (Join-Path $PSScriptRoot '../../Generated/Modules/x64/Debug/SampleGameRuntime/SampleGameRuntime.exe'))
$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Generated/Phase1Validation/Startup'))
$levelSource = Join-Path $engineRoot 'Resources/Assets/Levels/RuntimeDemoLevel.json'
$Executable = [IO.Path]::GetFullPath($Executable)
# 同じ相対設定を別Rootへ配置し、CWD・他Gameの資源から独立していることを確認する。
foreach($name in @('日本語 Game A', 'Moved Game B')) {
    $gameRoot = Join-Path $outputRoot $name
    foreach($directory in @('Config', 'Assets', 'Levels')) { [IO.Directory]::CreateDirectory((Join-Path $gameRoot $directory)) | Out-Null }
    $settings = 'name "Independent"' + "`n" + 'assetPath "Assets"' + "`n" + 'startupLevel "Levels/Startup.level"' + "`n"
    [IO.File]::WriteAllText((Join-Path $gameRoot 'Config/Project.settings'), $settings, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath $levelSource -Destination (Join-Path $gameRoot 'Levels/Startup.level')
    & $Executable --project $gameRoot --engine $engineRoot --check-game
    if($LASTEXITCODE -ne 0) { throw "Game startup failed: $name" }
}
& $Executable --project (Join-Path $outputRoot 'Missing Game') --engine $engineRoot --check-game 2>&1 | Out-String | Out-Null
if($LASTEXITCODE -ne 1) { throw 'Invalid project was not rejected.' }
& $Executable --project (Join-Path $outputRoot 'Moved Game B') --engine (Join-Path $outputRoot 'Missing Engine') --check-game 2>&1 | Out-String | Out-Null
if($LASTEXITCODE -ne 1) { throw 'Explicit missing engine was not rejected.' }
& $Executable --project (Join-Path $outputRoot 'Moved Game B') --engine $engineRoot --level '../Outside.level' --check-game 2>&1 | Out-String | Out-Null
if($LASTEXITCODE -ne 1) { throw 'Level path outside project was not rejected.' }
Write-Output 'PASS: Unicode/spaced project roots, relocated project settings, custom Assets/Levels paths and invalid root/path rejection.'
