param(
    [Parameter(Mandatory=$true)][string]$TargetManifest,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Release',
    [string]$LauncherAssembly,
    [string]$ProjectTool,
    [string]$SourceManifest
)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tool = if ($ProjectTool) { [IO.Path]::GetFullPath($ProjectTool) } else { Join-Path $workspace 'Generated/Modules/x64/Debug/NexusProjectTool/NexusProjectTool.exe' }
$assembly = if ($LauncherAssembly) { [IO.Path]::GetFullPath($LauncherAssembly) } else { Join-Path $workspace "Project/Launcher/Standalone/bin/$Configuration/net10.0-windows/NexusLauncher.dll" }
$source = if ($SourceManifest) { [IO.Path]::GetFullPath($SourceManifest) } else { Join-Path $workspace 'NexusEngine.Debug.nxengine' }
$root = Join-Path $workspace ('Generated/StandaloneMigrationValidation/' + [guid]::NewGuid())
[IO.Directory]::CreateDirectory($root) | Out-Null
$game = Join-Path $root '日本語 Game'
& $tool create-template $game 'Migration UI' $source blank
if ($LASTEXITCODE -ne 0) { throw 'Could not create isolated project.' }
$file = Join-Path $game '日本語 Game.nxproject'
$original = [IO.File]::ReadAllText($file)
Add-Type -AssemblyName System.Windows.Forms
$loaded = [Reflection.Assembly]::LoadFrom($assembly)
$projectType = $loaded.GetType('NexusLauncher.ProjectEntry', $true)
$engineType = $loaded.GetType('NexusLauncher.Installed', $true)
$dialogType = $loaded.GetType('NexusLauncher.ProjectMigrationDialog', $true)
$sourceEngine = Get-Content -LiteralPath $source -Raw | ConvertFrom-Json
$project = $projectType.GetConstructors()[0].Invoke([object[]]@([string]$file,'Migration UI',[string]$sourceEngine.version,[string]$sourceEngine.configuration,[string]$sourceEngine.id,[string]$sourceEngine.association))
$target = Get-Content -LiteralPath $TargetManifest -Raw | ConvertFrom-Json
$engine = $engineType.GetConstructors()[0].Invoke([object[]]@([IO.Path]::GetFullPath($TargetManifest),[string]$target.id,[string]$target.version,[string]$target.configuration,[string]$target.association))
$engines = [Array]::CreateInstance($engineType, 1)
$engines.SetValue($engine, 0)
$dialog = $dialogType.GetConstructors()[0].Invoke([object[]]@($project,$engines,[string]$tool))
$flags = [Reflection.BindingFlags]'Instance,NonPublic'
function Field([string]$name) { return $dialogType.GetField($name,$flags).GetValue($dialog) }
try {
    if ((Field 'apply').Enabled -or (Field 'inspect').Enabled) { throw 'Migration is enabled without an explicit target/review.' }
    (Field 'target').SelectedIndex = 0
    if (!(Field 'inspect').Enabled) { throw 'Cannot review selected engine.' }
    $task = $dialogType.GetMethod('Prepare',$flags).Invoke($dialog,@())
    # WinFormsの継続処理を実行し、UIスレッドを同期待ちで塞がない。
    $deadline = [DateTime]::UtcNow.AddMinutes(2)
    while (!$task.IsCompleted) {
        [Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 20
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Review timed out.' }
    }
    $null = $task.GetAwaiter().GetResult()
    $dialogType.GetMethod('UpdateActions',$flags).Invoke($dialog,@())
    if ((Field 'apply').Enabled) { throw 'Apply enabled before acknowledgement.' }
    if (!(Field 'review').Text.Contains($target.version)) { throw 'Review does not show target version.' }
    (Field 'acknowledge').Checked = $true
    if (!(Field 'apply').Enabled) { throw 'Reviewed change cannot be applied.' }
    (Field 'target').SelectedIndex = -1
    if ((Field 'apply').Enabled -or (Field 'acknowledge').Checked -or (Field 'plan')) { throw 'Changed target retained a stale review.' }
    if ([IO.File]::ReadAllText($file) -cne $original) { throw 'Review modified the project.' }
    Write-Output "PASS: explicit target, real native compatibility review, acknowledgement gating, stale review invalidation, unchanged descriptor. Artifacts: $root"
}
finally { $dialog.Dispose() }
