param(
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug',
    [string]$EngineManifest
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(-not $EngineManifest) { $EngineManifest = Join-Path $workspace "NexusEngine.$Configuration.nxengine" }
$EngineManifest = [IO.Path]::GetFullPath($EngineManifest)
$tool = Join-Path $workspace "Generated/Modules/x64/$Configuration/NexusProjectTool/NexusProjectTool.exe"
$root = Join-Path $workspace ('Generated/WorkflowValidation/' + [guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($root) | Out-Null
$registry = Join-Path $root 'Engines.json'
$utf8 = [Text.UTF8Encoding]::new($false)
function Invoke-Expected([string]$Executable, [string[]]$Arguments, [int]$Expected = 0) {
    $output = & $Executable @Arguments 2>&1 | Out-String
    if($LASTEXITCODE -ne $Expected) { throw "Exit $LASTEXITCODE (expected $Expected): $Executable $Arguments`n$output" }
    return $output
}
function Save-Json([string]$File, $Value) { [IO.File]::WriteAllText($File, ($Value | ConvertTo-Json -Depth 12), $utf8) }
$null = Invoke-Expected $tool @('engine-register',$EngineManifest,$registry)
$null = Invoke-Expected $tool @('engine-register',$EngineManifest,$registry)
$registered = Get-Content -LiteralPath $registry -Raw -Encoding UTF8 | ConvertFrom-Json
if($registered.engines.Count -ne 1) { throw 'Re-registering the same Engine duplicated it.' }
$listing = Invoke-Expected $tool @('template-list',$EngineManifest)
if($listing -notmatch 'blank' -or $listing -notmatch 'basic-3d') { throw 'Initial templates are missing.' }
$engine = Get-Content -LiteralPath $EngineManifest -Raw -Encoding UTF8 | ConvertFrom-Json
$dataRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($EngineManifest)) $engine.paths.data))
$generic = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($EngineManifest)) $engine.paths.runtime))
foreach($template in @('blank','basic-3d')) {
    $gameRoot = Join-Path $root ("日本語 Game $template")
    $null = Invoke-Expected $tool @('create-template',$gameRoot,"検証 $template",$EngineManifest,$template)
    $file = Join-Path $gameRoot (([IO.Path]::GetFileName($gameRoot)) + '.nxproject')
    $descriptorBytes = [IO.File]::ReadAllText($file)
    # Game固有の型を含むLevelは汎用Runtimeでは開けず、生成したGameホストで開けること。
    $level = Join-Path $gameRoot 'Levels/Startup.level'
    $levelText = [IO.File]::ReadAllText($level)
    if($template -eq 'blank') {
        if($levelText -match '(?m)^ENTITY ') { throw 'Blank template is not empty.' }
        # 型登録と実行経路の検証用Cameraはテスト側で追加し、空テンプレートの契約を変えない。
        $basicLevel = [IO.File]::ReadAllText((Join-Path $workspace 'Project/Templates/Basic3D/Startup.level'))
        $cameraFixture = [regex]::Match($basicLevel, '(?ms)^ENTITY .*?"Camera"\r?\n.*?^END_ENTITY\r?\n').Value
        if(-not $cameraFixture) { throw 'Camera fixture is missing.' }
        $levelText = $levelText.Replace('END_LEVEL', $cameraFixture + 'END_LEVEL')
    }
    $component = 'COMPONENTS 1' + "`n" + 'COMPONENT "Game.Health" object 2 "fields" object 1 "health" int 42 "type" string "Game.Health"'
    $position = $levelText.IndexOf('COMPONENTS 0')
    if($position -lt 0) { throw 'Template has no component insertion point.' }
    $levelText = $levelText.Remove($position,12).Insert($position,$component)
    [IO.File]::WriteAllText($level,$levelText,$utf8)
    $null = Invoke-Expected $generic @('--project',$file,'--engine',$dataRoot,'--check-game') 1
    $null = Invoke-Expected $tool @('check',$file,$registry,$Configuration)
    $localProps = Join-Path $gameRoot 'Nexus.Engine.local.props'
    $propsTime = [IO.File]::GetLastWriteTimeUtc($localProps)
    # 再ビルドでlocal propsとstampの置換も通す。Solutionや既存Gameを対象にしない。
    $null = Invoke-Expected $tool @('build-editor',$file,$registry,$Configuration)
    if([IO.File]::GetLastWriteTimeUtc($localProps) -ne $propsTime) { throw 'Unchanged local props were rewritten and invalidate incremental compilation.' }
    $null = Invoke-Expected $tool @('verify-editor',$file,$registry,$Configuration)
    $null = Invoke-Expected $tool @('verify-runtime',$file,$registry,$Configuration)
    $editor = Join-Path $gameRoot "Build/Bin/x64/$Configuration/GameEditor.exe"
    $null = Invoke-Expected $editor @('--game','--project',$file,'--engine',$dataRoot,'--check-game')
    if([IO.File]::ReadAllText($file) -cne $descriptorBytes) { throw 'Build modified the shared Project Descriptor.' }
    if([IO.File]::ReadAllText((Join-Path $gameRoot '.gitignore')) -notmatch '/Nexus.Engine.local.props') { throw 'Machine-specific binding is not ignored.' }
    $runtime = Join-Path $gameRoot "Build/Bin/x64/$Configuration/GameRuntime.exe"
    $stampFile = $runtime + '.nxbuild'
    $stampBytes = [IO.File]::ReadAllText($stampFile)
    try {
        $stamp = $stampBytes | ConvertFrom-Json
        $stamp.hostKind = 'Editor'; Save-Json $stampFile $stamp
        $null = Invoke-Expected $tool @('verify-runtime',$file,$registry,$Configuration) 1
        $stamp = $stampBytes | ConvertFrom-Json
        $stamp.sha256 = '0' * 64; Save-Json $stampFile $stamp
        $null = Invoke-Expected $tool @('verify-runtime',$file,$registry,$Configuration) 1
    } finally { [IO.File]::WriteAllText($stampFile,$stampBytes,$utf8) }
    try {
        $descriptor = $descriptorBytes | ConvertFrom-Json
        $descriptor.engine.version = '999.0.0-not-installed'; Save-Json $file $descriptor
        $before = [IO.File]::ReadAllText($file)
        $null = Invoke-Expected $tool @('verify-runtime',$file,$registry,$Configuration) 1
        if([IO.File]::ReadAllText($file) -cne $before) { throw 'Engine resolution changed the Project.' }
    } finally { [IO.File]::WriteAllText($file,$descriptorBytes,$utf8) }
    $null = Invoke-Expected $tool @('verify-runtime',$file,$registry,$Configuration)
}
Write-Output "PASS: Both templates, Unicode/spaces, typed Game/Editor reflection, repeat builds, exact Engine binding and stamp rejection. Artifacts: $root"
