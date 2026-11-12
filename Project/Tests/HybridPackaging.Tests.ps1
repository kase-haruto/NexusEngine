# ユーザー実行用。通常ファイルのfixtureをPackageへ渡すだけで、Build / dotnet / DLL / Gameは起動しない。
$ErrorActionPreference='Stop'
$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=Join-Path ([IO.Path]::GetDirectoryName($source)) ('Generated/Phase8Validation/Packaging/'+[guid]::NewGuid().ToString('N'))
$tool=Join-Path $source 'Tools/Project/Package-Game.ps1'
function Put([string]$File,[string]$Text) {
 [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($File)) | Out-Null
 [IO.File]::WriteAllText($File,$Text,[Text.UTF8Encoding]::new($false))
}
function Json([string]$File,[object]$Value) { Put $File (ConvertTo-Json -InputObject $Value -Depth 20) }
function Digest([string]$File) { return (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() }
function Require([bool]$Value,[string]$Message) { if(-not $Value) { throw $Message } }
function Inert-PE([string]$File) {
 # Assert-X64が読むheaderだけを持ち、コードsectionもentry pointもない。実行用binaryではない。
 [byte[]]$bytes=New-Object byte[] 256
 $bytes[0]=0x4d; $bytes[1]=0x5a; $bytes[0x3c]=0x80; $bytes[0x80]=0x50; $bytes[0x81]=0x45; $bytes[0x84]=0x64; $bytes[0x85]=0x86
 [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($File)) | Out-Null
 [IO.File]::WriteAllBytes($File,$bytes)
}
function Record([object]$Fixture,[string]$Relative) { return @{path=$Relative;sha256=(Digest (Join-Path $Fixture.game $Relative))} }
function Write-Stamp([object]$Fixture) {
 $stamp=@{schemaVersion=1;projectId=$Fixture.id;engineId='fixture';engineVersion='0.1.0';association='development';buildId='package-fixture';configuration='Release';hostKind='Runtime';projectHash=(Digest $Fixture.project);targetsHash=(Digest (Join-Path $Fixture.game 'Config/BuildTargets.json'));sha256=(Digest $Fixture.runtime);deploymentHash=(Digest (Join-Path $Fixture.bin 'Nexus.modules.json'));moduleFiles=@();runtimeFiles=@((Record $Fixture 'Dependencies/GameSupport.dll'))}
 if($Fixture.native) { $stamp.nativeModule=Record $Fixture 'Build/Bin/x64/Release/Game.dll' }
 if($Fixture.managed) {
  $stamp.managedFiles=@(Get-ChildItem -File -Recurse -LiteralPath (Join-Path $Fixture.bin 'Managed') | ForEach-Object { Record $Fixture $_.FullName.Substring($Fixture.game.Length+1).Replace('\','/') })
  $stamp.managedBootstrap=@((Record $Fixture 'Build/Bin/x64/Release/nethost.dll'))
  foreach($name in @('Nexus.Managed.Bootstrap.dll','Nexus.Managed.Bootstrap.runtimeconfig.json','Nexus.Managed.Bootstrap.deps.json','Nexus.Managed.dll')) { $stamp.managedBootstrap+=Record $Fixture ('Build/Bin/x64/Release/Managed/'+$name) }
  $stamp.gameScripts=@(); foreach($name in @('GameScripts.dll','GameScripts.deps.json','GameScripts.metadata.json')) { $stamp.gameScripts+=Record $Fixture ('Build/Bin/x64/Release/Managed/'+$name) }
 }
 Json ($Fixture.runtime+'.nxbuild') $stamp
}
function New-Fixture([string]$Name,[string]$Kind='Hybrid') {
 $directory=Join-Path $artifacts $Name; $game=Join-Path $directory 'Game 日本語'; $engine=Join-Path $directory 'Engine'
 $fixture=[pscustomobject]@{game=$game;engine=$engine;project=(Join-Path $game 'Game.nxproject');manifest=(Join-Path $engine 'Engine.nxengine');bin=(Join-Path $game 'Build/Bin/x64/Release');runtime=(Join-Path $game 'Build/Bin/x64/Release/GameRuntime.exe');output=(Join-Path $directory 'Package');id=[guid]::NewGuid().ToString();native=($Kind -ne 'CSharp');managed=($Kind -ne 'Cpp')}
 $modules=@()
 if($fixture.native) { $modules+=@{id='Native';name='Native';language='Cpp';type='Game';path='Source/Native';dependencies=@()} }
 if($fixture.managed) { $modules+=@{id='Managed';name='Managed';language='CSharp';type='Game';path='Source/Managed';dependencies=@()} }
 Json $fixture.project @{schemaVersion=1;projectId=$fixture.id;name='PackageFixture';projectVersion='0.1.0';engine=@{id='fixture';association='development';version='0.1.0'};paths=@{assets='Assets';levels='Levels';config='Config';source='Source'};startupLevel='Levels/Main.level';modules=$modules}
 Json $fixture.manifest @{schemaVersion=1;id='fixture';association='development';version='0.1.0';buildId='package-fixture';configuration='Release';paths=@{sdk='SDK';data='Data'};runtimeFiles=@('Bin/EngineSupport.dll')}
 Put (Join-Path $engine 'Bin/EngineSupport.dll') 'inert dependency bytes'; Put (Join-Path $fixture.bin 'EngineSupport.dll') 'inert dependency bytes'
 Put (Join-Path $engine 'Data/Shaders/Fixture.hlsl') '// inert shader fixture'
 Put (Join-Path $game 'Assets/Keep.asset') 'asset bytes'; Put (Join-Path $game 'Levels/Main.level') 'level bytes'
 Put (Join-Path $game 'Source/Keep.cs') '// must not be packaged'
 Put (Join-Path $game 'Dependencies/GameSupport.dll') 'inert game dependency bytes'
 $targets=@{schemaVersion=1;runtimeProject='GameRuntime.vcxproj';editorProject='GameEditor.vcxproj';runtimeExecutable='Build/Bin/x64/{configuration}/GameRuntime.exe';editorExecutable='Build/Bin/x64/{configuration}/GameEditor.exe';moduleProjects=@();runtimeDependencies=@('Dependencies/GameSupport.dll')}
 $native=@()
 if($fixture.native) { $targets.nativeModule='Build/Bin/x64/{configuration}/Game.dll'; Put (Join-Path $fixture.bin 'Game.dll') 'inert Native module bytes'; $native+=@{id='Native';path='Game.dll'} }
 if($fixture.managed) {
  $targets.managedBootstrap='Build/Bin/x64/{configuration}/Managed/Nexus.Managed.Bootstrap.dll'; $targets.gameScripts='Build/Bin/x64/{configuration}/Managed/GameScripts.dll'
  foreach($name in @('Nexus.Managed.Bootstrap.dll','Nexus.Managed.Bootstrap.deps.json','Nexus.Managed.dll','GameScripts.dll','GameScripts.deps.json','GameScripts.metadata.json')) { Put (Join-Path $fixture.bin ('Managed/'+$name)) ('inert '+$name+' fixture') }
  Json (Join-Path $fixture.bin 'Managed/Nexus.Managed.Bootstrap.runtimeconfig.json') @{runtimeOptions=@{framework=@{name='Microsoft.NETCore.App';version='10.0.0'}}}
  Json (Join-Path $fixture.bin 'Managed/GameScripts.modules.json') @('GameScripts.dll')
  Inert-PE (Join-Path $fixture.bin 'nethost.dll')
 }
 Json (Join-Path $game 'Config/BuildTargets.json') $targets
 Json (Join-Path $fixture.bin 'Nexus.modules.json') @{schemaVersion=1;projectId=$fixture.id;native=$native;staticNative=@();managedAssembly=$(if($fixture.managed) { 'Managed/GameScripts.dll' } else { '' })}
 Inert-PE $fixture.runtime; Write-Stamp $fixture
 return $fixture
}
function Package([object]$Fixture) { & $tool -ProjectFile $Fixture.project -EngineManifest $Fixture.manifest -OutputDirectory $Fixture.output | Out-Null }
function Reject([object]$Fixture,[string]$Message) {
 $rejected=$false
 try { Package $Fixture } catch { if($_.Exception.Message -notlike ('*'+$Message+'*')) { throw }; $rejected=$true }
 Require $rejected ('Invalid Package input was accepted: '+$Message)
 Require (-not (Test-Path -LiteralPath $Fixture.output)) 'Failed Package published an output directory.'
 Require (@(Get-ChildItem -Directory -Force -LiteralPath ([IO.Path]::GetDirectoryName($Fixture.output)) | Where-Object { $_.Name.StartsWith('.nexus-game-') }).Count -eq 0) 'Failed Package left its staging directory.'
}
foreach($kind in @('Cpp','CSharp','Hybrid')) {
 $fixture=New-Fixture $kind $kind; $before=Digest $fixture.project
 Package $fixture
 $manifest=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $fixture.output 'Bin/NexusGame.package.json') | ConvertFrom-Json
 foreach($entry in $manifest.files) { Require ((Digest (Join-Path $fixture.output $entry.path)) -ceq $entry.sha256) 'Published inventory differs from copied bytes.' }
 Require ((Digest $fixture.project) -ceq $before) 'Package modified the source Project.'
 Require ([IO.File]::ReadAllText((Join-Path $fixture.output 'Assets/Keep.asset')) -ceq 'asset bytes') 'Shared Asset bytes changed.'
 Require (-not (Test-Path -LiteralPath (Join-Path $fixture.output 'Source/Keep.cs'))) 'Package included game source.'
 Require ((Test-Path -LiteralPath (Join-Path $fixture.output 'Bin/Game.dll')) -eq $fixture.native) 'Native Module presence differs from Project.'
 Require ((Test-Path -LiteralPath (Join-Path $fixture.output 'Bin/Managed/GameScripts.dll')) -eq $fixture.managed) 'Managed Module presence differs from Project.'
}
$fixture=New-Fixture 'UnstampedManaged'
Put (Join-Path $fixture.bin 'Managed/Unstamped.dll') 'not part of the verified Build'
Reject $fixture 'not covered by the Build stamp'
$fixture=New-Fixture 'WrongNativeRoster'
$roster=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $fixture.bin 'Nexus.modules.json') | ConvertFrom-Json
$roster.native[0].path='Wrong.dll'; Json (Join-Path $fixture.bin 'Nexus.modules.json') $roster
Write-Stamp $fixture
Reject $fixture 'Native deployment differs from BuildTargets'
$fixture=New-Fixture 'MissingManagedStamp'
$stamp=Get-Content -Raw -Encoding UTF8 -LiteralPath ($fixture.runtime+'.nxbuild') | ConvertFrom-Json
foreach($field in @('managedFiles','managedBootstrap')) { $stamp.$field=@($stamp.$field | Where-Object { -not $_.path.EndsWith('Nexus.Managed.Bootstrap.runtimeconfig.json') }) }
Json ($fixture.runtime+'.nxbuild') $stamp
Reject $fixture 'omits a Managed startup file'
$fixture=New-Fixture 'ChangedNative'
Put (Join-Path $fixture.bin 'Game.dll') 'changed after Build'
Reject $fixture 'Stale deployment'
$fixture=New-Fixture 'ForeignAssociation'
$stamp=Get-Content -Raw -Encoding UTF8 -LiteralPath ($fixture.runtime+'.nxbuild') | ConvertFrom-Json
$stamp.association='installed'; Json ($fixture.runtime+'.nxbuild') $stamp
Reject $fixture 'Build and verify the Standard Runtime'
Write-Output ('PASS: Cpp / CSharp / Hybrid Package bytes and invalid input rejection. Artifacts: '+$artifacts)
