param(
 [Parameter(Mandatory=$true)][string]$ProjectFile,
 [Parameter(Mandatory=$true)][string]$EngineManifest,
 [Parameter(Mandatory=$true)][string]$ModuleId,
 [Parameter(Mandatory=$true)][ValidateSet('Cpp','CSharp')][string]$Language,
 [string]$Dependencies=''
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Project-ModuleOrder.ps1')
$root=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($ProjectFile))
$lock=[IO.File]::Open((Join-Path $root '.nexus.modules.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
# 読み取った時点の値を保持し、計画後に他の編集内容を上書きしない。
$before=@{}
function Read-Original([string]$File) {
 if(-not $before.ContainsKey($File)) { $before[$File]=$(if([IO.File]::Exists($File)) { [IO.File]::ReadAllBytes($File) } else { $null }) }
 if($null -eq $before[$File]) { return $null }
 return [Text.Encoding]::UTF8.GetString($before[$File]).TrimStart([char]0xfeff)
}
try {
$project=(Read-Original ([IO.Path]::GetFullPath($ProjectFile))) | ConvertFrom-Json
$engine=Get-Content -Raw -Encoding UTF8 -LiteralPath $EngineManifest | ConvertFrom-Json
$engineRoot=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($EngineManifest))
$templates=[IO.Path]::GetFullPath((Join-Path $engineRoot $engine.paths.templates))
if($ModuleId -cnotmatch '\A[A-Za-z][A-Za-z0-9_]{0,63}\z' -or $ModuleId -imatch '\A(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])\z' -or @($project.modules | Where-Object { $_.id -ieq $ModuleId }).Count) { throw 'Choose a unique ASCII identifier for the new Module.' }
if($project.engine.id -cne $engine.id -or $project.engine.association -cne $engine.association -or ($project.engine.version -and $project.engine.version -cne $engine.version)) { throw 'Engine association / version mismatch.' }
function Within([string]$Relative) {
 if([IO.Path]::IsPathRooted($Relative)) { throw 'Module path must be relative.' }
 $full=[IO.Path]::GetFullPath((Join-Path $root $Relative))
 if(-not $full.StartsWith($root.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Module path escapes Project.' }
 $cursor=$full; while($cursor -and $cursor.Length -ge $root.Length) { if((Test-Path -LiteralPath $cursor) -and ((Get-Item -Force -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Module transaction cannot traverse a reparse point.' }; $cursor=[IO.Path]::GetDirectoryName($cursor) }
 return $full
}
function Xml([string]$Value) { return [Security.SecurityElement]::Escape($Value) }
# 条件付きの参照を常時接続の根拠にしない。任意のMSBuild式を生成処理内で実行・推測しない。
function Is-Unconditional([System.Xml.XmlNode]$Node) {
 for($cursor=$Node;$null -ne $cursor;$cursor=$cursor.ParentNode) {
  if($cursor -is [System.Xml.XmlElement] -and -not [string]::IsNullOrWhiteSpace($cursor.GetAttribute('Condition'))) { return $false }
 }
 return $true
}
$source=$project.paths.source+'/'+$ModuleId
$moduleRoot=Within $source
if(Test-Path -LiteralPath $moduleRoot) { throw 'New Module directory must not exist.' }
$config=Within ($project.paths.config+'/BuildTargets.json')
if(-not [IO.File]::Exists($config)) { throw 'Generate a Project host before adding Modules.' }
$targets=(Read-Original $config) | ConvertFrom-Json
$oldManaged=@($project.modules | Where-Object { $_.language -ceq 'CSharp' })
if($Language -ceq 'CSharp' -and -not $oldManaged.Count) { $targets | Add-Member -Force NoteProperty managedRuntime 'Mono' }
$oldNative=@($project.modules | Where-Object { $_.language -ceq 'Cpp' })
$dependenciesList=@($Dependencies.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if(-not $dependenciesList.Count -and $Language -ceq 'CSharp') { $dependenciesList=@($oldNative | ForEach-Object { $_.id }) }
$descriptor=[pscustomobject]@{id=$ModuleId;name=$ModuleId;language=$Language;path=$source;type='Game';dependencies=$dependenciesList}
$project | Add-Member -Force NoteProperty modules @(@($project.modules | Where-Object { $_ })+$descriptor)
$null=@(Get-NexusModuleOrder $project); Assert-NexusRuntimeModules $project
$writes=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
function Plan([string]$Relative,[string]$Text,[bool]$NewOnly=$false) {
 $file=Within $Relative; $null=Read-Original $file; if($NewOnly -and $null -ne $before[$file]) { throw "Generated file already exists: $Relative" }; $writes[$file]=$Text
}
function NativeProject([string]$Id,[string]$Directory) {
 $text=[IO.File]::ReadAllText((Join-Path $templates 'NativeModule/GameModule.vcxproj.in'))
 $text=$text.Replace('@MODULE_GUID@',[guid]::NewGuid().ToString()).Replace('@SOURCE@\Game',(Xml $Directory.Replace('/','\'))).Replace('<TargetName>Game</TargetName>','<TargetName>'+ $Id +'</TargetName>')
 return $text
}
function ManagedProject([string]$Id,[string]$Directory,[string[]]$References) {
 $framework=if($targets.managedRuntime -ceq 'Mono') { 'net48' } else { 'net10.0' }
 $refs=''; foreach($reference in $References) { $refs+='<ProjectReference Include="'+(Xml $reference)+'" />' }
 return '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>'+$framework+'</TargetFramework><LangVersion>latest</LangVersion><AssemblyName>'+(Xml $Id)+'</AssemblyName><AllowUnsafeBlocks>true</AllowUnsafeBlocks><Nullable>enable</Nullable><ImplicitUsings>enable</ImplicitUsings><PlatformTarget>x64</PlatformTarget><EnableDynamicLoading>true</EnableDynamicLoading><EnableDefaultCompileItems>false</EnableDefaultCompileItems><Configurations>Debug;Develop;Release</Configurations><SelfContained>false</SelfContained><NexusManagedApiPath Condition="&apos;$(NexusManagedApiPath)&apos;==&apos;&apos;">$(MSBuildThisFileDirectory)Build/Bin/x64/$(Configuration)/Managed/Nexus.Managed.dll</NexusManagedApiPath></PropertyGroup><ItemGroup><Reference Include="Nexus.Managed"><HintPath>$(NexusManagedApiPath)</HintPath><Private>false</Private></Reference><Compile Include="$(MSBuildThisFileDirectory)'+(Xml $Directory)+'/**/*.cs" />'+$refs+'</ItemGroup><ItemGroup Condition="&apos;$(TargetFramework)&apos;==&apos;net48&apos;"><PackageReference Include="System.Memory" Version="4.6.3" /></ItemGroup></Project>'
}
if($Language -ceq 'Cpp') {
 foreach($file in @('CrowdSimulationSystem.h','CrowdSimulationSystem.cpp','CrowdBinding.h','NativeModuleState.h','NativePatchHooks.cpp','NativeMover.h','NativeMover.cpp','GameComponents.h','Main.cpp')) {
  $text=[IO.File]::ReadAllText((Join-Path $templates ('NativeModule/'+$file+'.in')))
  $text=$text.Replace('@PROJECT_GUID@',$project.projectId).Replace('{"Game", 4}','{"'+$ModuleId+'", '+$ModuleId.Length+'}')
  $text=$text.Replace('#define NEXUS_GAME_COMPONENT_MODULE_ID "Game"','#define NEXUS_GAME_COMPONENT_MODULE_ID "'+$ModuleId+'"')
  Plan ($source+'/'+$file) $text $true
 }
 $projectName=$ModuleId+'.vcxproj'
 $output='Build/Bin/x64/{configuration}/'+$ModuleId+'.dll'
 if(-not $oldNative.Count) {
  # 初めてのNative Moduleは既存単一DLL契約のGame.dllを使う。
  Plan 'GameModule.vcxproj' ((NativeProject 'Game' $source)) $true
  $targets | Add-Member -Force NoteProperty nativeModule 'Build/Bin/x64/{configuration}/Game.dll'
 } else {
  Plan $projectName (NativeProject $ModuleId $source) $true
  $targets | Add-Member -Force NoteProperty moduleProjects @(@($targets.moduleProjects | Where-Object { $_ })+[pscustomobject]@{id=$ModuleId;project=$projectName;output=$output})
 }
 if($oldManaged.Count) {
  $facade=[IO.File]::ReadAllText((Join-Path $templates 'Hybrid/CrowdSimulation.cs.in')).Replace('"NativeCombat"','"'+$ModuleId+'"').Replace('class CrowdSimulation','class '+$ModuleId+'CrowdSimulation')
  Plan ($oldManaged[0].path+'/'+$ModuleId+'CrowdSimulation.cs') $facade $true
 }
} else {
 $text=[IO.File]::ReadAllText((Join-Path $templates 'ManagedGameplay/PlayerController.cs.in')).Replace('namespace Game;','namespace NexusGameScripts.M'+(Get-NexusModuleCodeIdentifier $ModuleId)+';').Replace('"game.player"','"'+$ModuleId+'.behaviours.PlayerController"')
 Plan ($source+'/PlayerController.cs') $text $true
 $references=@(); foreach($dependency in $dependenciesList) {
  $item=@($project.modules | Where-Object { $_.id -ceq $dependency })[0]
  if($item.language -ceq 'CSharp') { $mapping=@($targets.moduleProjects | Where-Object { $_.id -ceq $dependency }); $references+= $(if($mapping.Count) { $mapping[0].project } else { 'GameScripts.csproj' }) }
 }
 if(-not $oldManaged.Count) { Plan 'GameScripts.csproj' (ManagedProject 'GameScripts' $source $references) $true }
 else { $name=$ModuleId+'.csproj'; Plan $name (ManagedProject $ModuleId $source $references) $true; $targets | Add-Member -Force NoteProperty moduleProjects @(@($targets.moduleProjects | Where-Object { $_ })+[pscustomobject]@{id=$ModuleId;project=$name;output=('Build/Bin/x64/{configuration}/Managed/'+$ModuleId+'.dll')}) }
 foreach($native in $oldNative) {
  if(($targets.nativeModule -and $native.id -ceq $oldNative[0].id) -or @($targets.moduleProjects | Where-Object { $_.id -ceq $native.id }).Count) {
   $facade=[IO.File]::ReadAllText((Join-Path $templates 'Hybrid/CrowdSimulation.cs.in')).Replace('"NativeCombat"','"'+$native.id+'"').Replace('class CrowdSimulation','class '+(Get-NexusModuleCodeIdentifier $native.id)+'CrowdSimulation')
   Plan ($source+'/'+$native.id+'CrowdSimulation.cs') $facade $true
  }
 }
}
# 既存Bootstrap-onlyをGameplayへ拡張する場合も、Sourceや既存IDを保持する。
if($Language -ceq 'CSharp' -or $oldManaged.Count) {
 if(-not $targets.gameScripts) {
  $targets | Add-Member -Force NoteProperty gameScripts 'Build/Bin/x64/{configuration}/Managed/GameScripts.dll'
  if($oldManaged.Count -and -not [IO.File]::Exists((Within 'GameScripts.csproj'))) { Plan 'GameScripts.csproj' (ManagedProject 'GameScripts' $oldManaged[0].path @()) $true }
 }
 $targets | Add-Member -Force NoteProperty managedBootstrap 'Build/Bin/x64/{configuration}/Managed/Nexus.Managed.Bootstrap.dll'
 $existing=Read-Original (Within 'ManagedBootstrap.vcxproj')
 if($null -ne $existing) {
  # ユーザーのBuild設定を残し、Bootstrap-onlyからGameplayへの必要箇所だけを変更する。
  [xml]$utility=$existing; $ns=$utility.DocumentElement.NamespaceURI
  foreach($import in $utility.SelectNodes('//*[local-name()="Import"]')) { if($import.Project -like '*Nexus.ManagedBootstrap.targets') { $import.SetAttribute('Project','$(NexusEngineRoot)\Build\Nexus.ManagedGameplay.targets') } }
  $group=$utility.CreateElement('PropertyGroup',$ns)
  foreach($entry in @(@('NexusGameScriptsProject','$(ProjectDir)GameScripts.csproj'),@('NexusDeferredModuleDeployment','true'))) { $node=$utility.CreateElement($entry[0],$ns); $node.InnerText=$entry[1]; [void]$group.AppendChild($node) }
  [void]$utility.DocumentElement.PrependChild($group); Plan 'ManagedBootstrap.vcxproj' $utility.OuterXml
 } else {
  $utility=[IO.File]::ReadAllText((Join-Path $templates 'Hybrid/ManagedBootstrap.vcxproj.in')).Replace('@MODULE_GUID@',[guid]::NewGuid().ToString())
  # 通常Host側がNativeをBuildし、Utilityは安定BootstrapとManagedだけを担当する。
  $utility=$utility -replace '<ItemGroup><ProjectReference[^\r\n]+</ItemGroup>',''
  Plan 'ManagedBootstrap.vcxproj' $utility $true
 }
}
foreach($hostName in @($targets.runtimeProject,$targets.editorProject)) {
 $file=Within $hostName; [xml]$xml=Read-Original $file; $ns=$xml.DocumentElement.NamespaceURI
 $imports=@($xml.SelectNodes('//*[local-name()="Import"]') | Where-Object { (Is-Unconditional $_) -and $_.GetAttribute('Project').Replace('/','\') -ieq '$(NexusEngineRoot)\Build\Nexus.GameDeployment.targets' })
 if(-not $imports.Count) { $node=$xml.CreateElement('Import',$ns); $node.SetAttribute('Project','$(NexusEngineRoot)\Build\Nexus.GameDeployment.targets'); [void]$xml.DocumentElement.AppendChild($node) }
 $group=$xml.CreateElement('ItemGroup',$ns)
 foreach($reference in @('GameModule.vcxproj','ManagedBootstrap.vcxproj')) {
  $alreadyReferenced=$false
  foreach($existingReference in $xml.SelectNodes('//*[local-name()="ProjectReference"]')) {
   if(-not (Is-Unconditional $existingReference)) { continue }
   $include=$existingReference.GetAttribute('Include').Replace('$(ProjectDir)',$root+'\').Replace('$(MSBuildThisFileDirectory)',$root+'\')
   if($include.Contains('$(')) { continue } # 任意のMSBuild式を推測して別Projectと同一視しない。
   $resolved=if([IO.Path]::IsPathRooted($include)) { [IO.Path]::GetFullPath($include) } else { [IO.Path]::GetFullPath((Join-Path $root $include)) }
   if($resolved -ieq (Within $reference)) { $alreadyReferenced=$true; break }
  }
  if(($writes.ContainsKey((Within $reference)) -or [IO.File]::Exists((Within $reference))) -and -not $alreadyReferenced) {
   $node=$xml.CreateElement('ProjectReference',$ns); $node.SetAttribute('Include','$(ProjectDir)'+$reference); $link=$xml.CreateElement('LinkLibraryDependencies',$ns); $link.InnerText='false'; [void]$node.AppendChild($link); [void]$group.AppendChild($node)
  }
 }
 [void]$xml.DocumentElement.AppendChild($group); Plan $hostName $xml.OuterXml
}
$hostSource=Within ($project.paths.source+'/Host/Main.cpp')
if(-not [IO.File]::Exists($hostSource)) { $hostSource=Within ($project.paths.source+'/Game/Main.cpp') } # 旧静的Gameのentry pointを保持する。
if(-not [IO.File]::Exists($hostSource)) { throw 'Cannot safely identify the existing Host source; no files changed.' }
$hostText=Read-Original $hostSource
# コメント中の単語や別名ProjectReferenceだけで「接続済み」と判定しない。
if(-not [regex]::IsMatch($hostText,'(?m)^\s*#\s*include\s*[<"]Core/Hosting/ProjectHost\.inl[>"]')) { throw 'Host source does not use the generated ProjectHost entry point. Integrate ModuleDeployment explicitly; no files changed.' }
if(-not [regex]::IsMatch($hostText,'(?m)^\s*#\s*define\s+NEXUS_PROJECT_MODULE_MANIFEST\b')) { Plan ($hostSource.Substring($root.Length+1)) ('#define NEXUS_PROJECT_MODULE_MANIFEST 1'+"`r`n"+$hostText) }
$propsFile=Within 'Directory.Build.props'
[xml]$props=if([IO.File]::Exists($propsFile)) { Read-Original $propsFile } else { '<Project />' }
if(-not $props.SelectSingleNode('//*[local-name()="AssemblySearchPath_UseCandidateAssemblyFiles"]')) {
 # Runtime配布物を参照Assemblyの候補に含めず、net48 / Mono BCLの重複を防ぐ。
 $group=$props.CreateElement('PropertyGroup',$props.DocumentElement.NamespaceURI)
 $group.SetAttribute('Condition',"'`$(MSBuildProjectExtension)'=='.csproj'")
 $node=$props.CreateElement('AssemblySearchPath_UseCandidateAssemblyFiles',$props.DocumentElement.NamespaceURI)
 $node.InnerText='false'; [void]$group.AppendChild($node); [void]$props.DocumentElement.AppendChild($group)
}
if(-not $props.SelectSingleNode('//*[local-name()="NexusManagedRuntime"]')) {
 $group=$props.CreateElement('PropertyGroup',$props.DocumentElement.NamespaceURI)
 $node=$props.CreateElement('NexusManagedRuntime',$props.DocumentElement.NamespaceURI)
 $node.SetAttribute('Condition',"'`$(NexusManagedRuntime)'==''")
 $node.InnerText=if($targets.managedRuntime -ceq 'Mono') { 'Mono' } else { 'CoreCLR' }
 [void]$group.AppendChild($node); [void]$props.DocumentElement.AppendChild($group)
}
if(-not $props.SelectSingleNode('//*[@Label="NexusManagedIntermediate"]')) {
 $group=$props.CreateElement('PropertyGroup',$props.DocumentElement.NamespaceURI); $group.SetAttribute('Label','NexusManagedIntermediate'); $group.SetAttribute('Condition',"'`$(MSBuildProjectExtension)'=='.csproj' and '`$(NexusManagedIntermediateRoot)'!=''")
 $node=$props.CreateElement('BaseIntermediateOutputPath',$props.DocumentElement.NamespaceURI); $node.InnerText='$(NexusManagedIntermediateRoot)$(MSBuildProjectName)/'; [void]$group.AppendChild($node); [void]$props.DocumentElement.AppendChild($group)
 $node=$props.CreateElement('MSBuildProjectExtensionsPath',$props.DocumentElement.NamespaceURI); $node.InnerText='$(BaseIntermediateOutputPath)'; [void]$group.AppendChild($node)
}
Plan 'Directory.Build.props' $props.OuterXml
$solution=Within 'Game.slnx'
if([IO.File]::Exists($solution)) {
 [xml]$xml=Read-Original $solution
 $names=@($writes.Keys | Where-Object { [IO.Path]::GetDirectoryName($_) -ieq $root -and [IO.Path]::GetExtension($_) -in @('.csproj','.vcxproj') } | ForEach-Object { [IO.Path]::GetFileName($_) })
 if([IO.File]::Exists((Within 'GameScripts.csproj'))) { $names+='GameScripts.csproj' }
 foreach($name in $names | Sort-Object -Unique) {
  if($xml.SelectSingleNode('//Project[@Path="'+$name+'"]')) { continue }
  $node=$xml.CreateElement('Project'); $node.SetAttribute('Path',$name)
  # Scriptと追加DLLはHostのcoordinatorが生成する。Solutionからも独立Buildすると同じ出力へ競合する。
  if([IO.Path]::GetExtension($name) -ieq '.csproj' -or $name -in @($targets.moduleProjects | ForEach-Object { $_.project })) { $build=$xml.CreateElement('Build'); $build.SetAttribute('Project','false'); [void]$node.AppendChild($build) }
  [void]$xml.DocumentElement.AppendChild($node)
 }
 Plan 'Game.slnx' $xml.OuterXml
}
Plan ($project.paths.config+'/BuildTargets.json') ($targets | ConvertTo-Json -Depth 20)
# 追加言語がHosting DLLや既存Moduleを上書きする計画は、一つもcommitする前に拒否する。
$null=@(Get-NexusModuleBuildPlan $project $targets $root $engine.configuration 'Standard' $engine)
# Project定義を最後にcommitする。rollbackは自分が書いた内容のままの場合だけ行う。
$writes[[IO.Path]::GetFullPath($ProjectFile)]=$project | ConvertTo-Json -Depth 20
$committed=[Collections.Generic.List[string]]::new(); $written=@{}
$newDirectories=[Collections.Generic.List[string]]::new()
try {
 $files=@($writes.Keys | Where-Object { $_ -ine [IO.Path]::GetFullPath($ProjectFile) })+@([IO.Path]::GetFullPath($ProjectFile))
 foreach($file in $files) {
  $null=Within ($file.Substring($root.Length+1)) # commit直前にも親のreparse pointを再検査する。
  if([IO.File]::Exists($file)) { if($null -eq $before[$file] -or [Convert]::ToBase64String([IO.File]::ReadAllBytes($file)) -cne [Convert]::ToBase64String($before[$file])) { throw "Concurrent modification: $file" } }
  elseif($null -ne $before[$file]) { throw "Concurrent removal: $file" }
  $directory=[IO.Path]::GetDirectoryName($file); $missing=@(); while(-not [IO.Directory]::Exists($directory)) { $missing+=,$directory; $directory=[IO.Path]::GetDirectoryName($directory) }
  for($index=$missing.Count-1;$index -ge 0;$index--) { [IO.Directory]::CreateDirectory($missing[$index]) | Out-Null; $newDirectories.Add($missing[$index]) }
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes($writes[$file]); $temporary=$file+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
  # Windows PowerShellの空文字backupを避け、既存ファイルだけを原子的に置換する。
  try { [IO.File]::WriteAllBytes($temporary,$bytes); if([IO.File]::Exists($file)) { [IO.File]::Replace($temporary,$file,[System.Management.Automation.Language.NullString]::Value) } else { [IO.File]::Move($temporary,$file) } }
  finally { if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
  $written[$file]=[Convert]::ToBase64String($bytes); $committed.Add($file)
 }
} catch {
 for($index=$committed.Count-1;$index -ge 0;$index--) { $file=$committed[$index]; if([IO.File]::Exists($file) -and [Convert]::ToBase64String([IO.File]::ReadAllBytes($file)) -ceq $written[$file]) { if($null -ne $before[$file]) { [IO.File]::WriteAllBytes($file,$before[$file]) } else { [IO.File]::Delete($file) } } }
 for($index=$newDirectories.Count-1;$index -ge 0;$index--) { $directory=$newDirectories[$index]; if([IO.Directory]::Exists($directory) -and [IO.Directory]::GetFileSystemEntries($directory).Length -eq 0) { [IO.Directory]::Delete($directory,$false) } }
 throw
}
Write-Output "Module added: $ModuleId ($Language). Build and restart the Host; Level / Asset data was preserved."
} finally { $lock.Dispose() }
