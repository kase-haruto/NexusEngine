param([ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug',[string]$EngineManifest,
    [ValidateSet('blank','basic-3d')][string]$Template = 'basic-3d')
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tool = Join-Path $workspace "Generated/Modules/x64/$Configuration/NexusProjectTool/NexusProjectTool.exe"
$manifest = if($EngineManifest) { [IO.Path]::GetFullPath($EngineManifest) } else { Join-Path $workspace "NexusEngine.$Configuration.nxengine" }
$root = Join-Path $workspace ('Generated/ProjectGpuValidation/' + [guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($root) | Out-Null
$registry = Join-Path $root 'Engines.json'; $gameRoot = Join-Path $root '日本語 GPU Game'
& $tool engine-register $manifest $registry
if($LASTEXITCODE -ne 0) { throw 'Cannot register selected Engine for GPU validation.' }
& $tool create-template $gameRoot 'GPU Game' $manifest $Template
if($LASTEXITCODE -ne 0) { throw 'Cannot create GPU validation Game.' }
$file = Join-Path $gameRoot '日本語 GPU Game.nxproject'
$levelFile = Join-Path $gameRoot 'Levels/Startup.level'
$level = [IO.File]::ReadAllText($levelFile)
if($Template -eq 'blank') {
    if($level -match '(?m)^ENTITY ') { throw 'Blank template is not empty.' }
    # GPU経路の型付きEntityはfixtureとして明示追加する。
    $basicLevel = [IO.File]::ReadAllText((Join-Path $workspace 'Project/Templates/Basic3D/Startup.level'))
    $cameraFixture = [regex]::Match($basicLevel, '(?ms)^ENTITY .*?"Camera"\r?\n.*?^END_ENTITY\r?\n').Value
    if(-not $cameraFixture) { throw 'Camera fixture is missing.' }
    $level = $level.Replace('END_LEVEL', $cameraFixture + 'END_LEVEL')
}
$position = $level.IndexOf('COMPONENTS 0'); if($position -lt 0) { throw 'Missing GPU Level component insertion point.' }
$component = 'COMPONENTS 1' + "`n" + 'COMPONENT "Game.Health" object 2 "fields" object 1 "health" int 42 "type" string "Game.Health"'
[IO.File]::WriteAllText($levelFile,$level.Remove($position,12).Insert($position,$component),[Text.UTF8Encoding]::new($false))
Write-Output "Building typed GPU hosts in $gameRoot"
& $tool build-editor $file $registry $Configuration
if($LASTEXITCODE -ne 0) { throw 'Cannot build typed GPU hosts.' }
$engine = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$dataRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($manifest)) $engine.paths.data))
$foreign = Join-Path $root 'Foreign cwd'; [IO.Directory]::CreateDirectory($foreign) | Out-Null
foreach($hostName in @('GameRuntime','GameEditor')) {
    $exe = Join-Path $gameRoot "Build/Bin/x64/$Configuration/$hostName.exe"
    $start = [Diagnostics.ProcessStartInfo]::new($exe)
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $foreign; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $start.Environment['LOCALAPPDATA'] = Join-Path $root 'User'
    foreach($argument in @('--project',$file,'--engine',$dataRoot,'--hidden','--frames','3')) { $start.ArgumentList.Add($argument) }
    if($hostName -eq 'GameEditor') { $start.ArgumentList.Add('--engine-manifest'); $start.ArgumentList.Add($manifest) }
    Write-Output "Running $hostName GPU initialization and 3 frames..."
    $child = [Diagnostics.Process]::Start($start)
    $outputTask = $child.StandardOutput.ReadToEndAsync(); $errorTask = $child.StandardError.ReadToEndAsync()
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(90)
        while(-not $child.WaitForExit(1000)) { if([DateTime]::UtcNow -gt $deadline) { $child.Kill($true); throw "GPU host timeout: $hostName" } }
        $outputText = $outputTask.GetAwaiter().GetResult(); $errorText = $errorTask.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $root ($hostName + '.log')),$outputText + $errorText,[Text.UTF8Encoding]::new($false))
        if($child.ExitCode -ne 0) { throw "$hostName exited $($child.ExitCode): $errorText. Logs: $root" }
        if($outputText -notmatch 'PASS: completed 3 GPU frames and normal shutdown.' -or $outputText -notmatch 'GraphicsSystem initialized' -or $outputText -notmatch 'NexusEngine shutdown completed') { throw "Missing GPU lifecycle evidence: $hostName. Logs: $root" }
    } finally { $child.Dispose() }
}
Write-Output "PASS: typed Runtime and Editor loaded Game.Health from a Unicode project, initialized GPU, completed 3 frames and shut down normally from a foreign CWD. Logs: $root"
