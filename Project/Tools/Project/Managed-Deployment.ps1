# 同一AssemblyLoadContextに配置するファイルの衝突だけを検査する。Assemblyはロードしない。
function Initialize-NexusManagedStage([string]$Source,[string]$Stage,[Collections.Generic.Dictionary[string,string]]$Files,[string]$OwnedAssembly) {
 if($OwnedAssembly -ieq 'GameScripts.dll' -or $OwnedAssembly -imatch '\ANexus\.Managed' -or [IO.Path]::GetFileName($OwnedAssembly) -cne $OwnedAssembly) { throw 'Additional Module cannot replace a primary / stable Assembly.' }
 [IO.Directory]::CreateDirectory($Stage) | Out-Null
 $owned=@(@('.dll','.pdb','.deps.json','.runtimeconfig.json','.xml') | ForEach-Object { [IO.Path]::ChangeExtension($OwnedAssembly,$_) })
 # ProjectReferenceのGetTargetPathは共通出力先を返す。既存rootを配置し、別のPDB出力先で再Buildしない。
 foreach($relative in $Files.Keys) {
  if($owned -icontains $relative) { continue }
  $file=Resolve-NexusProjectReference $Source $relative; $target=Resolve-NexusProjectReference $Stage $relative
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null; [IO.File]::Copy($file,$target,$false)
 }
 # 当該Moduleの出力は更新可能だが、他Module / 依存libraryの内容差は引き続き拒否する。
 foreach($file in $owned) { [void]$Files.Remove($file) }
}
function New-NexusManagedFileMap([string]$Directory,[string]$Primary='GameScripts.dll') {
 $map=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
 $names=@('Nexus.Managed.dll','Nexus.Managed.Bootstrap.dll',$Primary)
 # Mono profileでも安定APIのNuGet依存を同じ世代へcopyするため、Bootstrapのdepsも集約する。
 foreach($depsName in @([IO.Path]::ChangeExtension($Primary,'.deps.json'),'Nexus.Managed.Bootstrap.deps.json','Nexus.Managed.deps.json')) {
 $deps=Join-Path $Directory $depsName
 if([IO.File]::Exists($deps)) {
  $model=[IO.File]::ReadAllText($deps) | ConvertFrom-Json
  foreach($target in $model.targets.PSObject.Properties) { foreach($library in $target.Value.PSObject.Properties) {
   foreach($kind in @('runtime','native','resources','runtimeTargets')) { foreach($entry in $library.Value.$kind.PSObject.Properties) {
    $name=$entry.Name.Replace('/',[IO.Path]::DirectorySeparatorChar)
    $candidate=Resolve-NexusProjectReference $Directory $name
    if([IO.File]::Exists($candidate)) { $names+=$name }
    elseif([IO.File]::Exists((Join-Path $Directory ([IO.Path]::GetFileName($name))))) { $names+=[IO.Path]::GetFileName($name) }
   } }
  } }
 }
 }
 foreach($name in $names) { $file=Resolve-NexusProjectReference $Directory $name; if([IO.File]::Exists($file)) { $map[$file.Substring([IO.Path]::GetFullPath($Directory).TrimEnd('\','/').Length+1)]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash } }
 # 空のDictionaryもPowerShellのpipelineで消失させない。
 return ,$map
}
function Merge-NexusManagedDeployment([string]$Source,[string]$Destination,[Collections.Generic.Dictionary[string,string]]$Files) {
 $sourceRoot=[IO.Path]::GetFullPath($Source).TrimEnd('\','/')
 $entries=@(Get-ChildItem -Force -LiteralPath $sourceRoot -Recurse)
 $incoming=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($entry in $entries) {
  if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Managed deployment cannot traverse reparse points.' }
  if($entry.PSIsContainer) { continue }
  $relative=$entry.FullName.Substring($sourceRoot.Length+1); $hash=(Get-FileHash -LiteralPath $entry.FullName -Algorithm SHA256).Hash
  if($Files.ContainsKey($relative) -and $Files[$relative] -cne $hash) { throw "Managed dependency collision: $relative. Align package / ProjectReference versions before rebuilding." }
  $incoming.Add($relative,$hash)
 }
 # 検査をすべて終えてからコピーする。衝突したModuleで正常な配置を部分上書きしない。
 foreach($entry in $entries) {
  if($entry.PSIsContainer) { continue }; $relative=$entry.FullName.Substring($sourceRoot.Length+1)
  $sourceFile=Resolve-NexusProjectReference $sourceRoot $relative; $target=Resolve-NexusProjectReference $Destination $relative
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
  $hash=$incoming[$relative]
  if(-not [IO.File]::Exists($target) -or (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -cne $hash) {
   # 検査後に入力が変わっても既存DLLを上書きしない。同一Directoryの検証済みcopyだけを公開する。
   $temporary=$target+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
   try {
    [IO.File]::Copy($sourceFile,$temporary,$false)
    if((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -cne $hash) { throw 'Managed deployment input changed after validation.' }
    # Windows PowerShellによる$nullの空文字化を避け、検証済みの既存配置を原子的に更新する。
    if([IO.File]::Exists($target)) { [IO.File]::Replace($temporary,$target,[System.Management.Automation.Language.NullString]::Value) } else { [IO.File]::Move($temporary,$target) }
   } finally { if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
  }
  $Files[$relative]=$hash
 }
}
