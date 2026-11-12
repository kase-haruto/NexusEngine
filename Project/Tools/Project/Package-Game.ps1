param(
 [Parameter(Mandatory=$true)][string]$ProjectFile,
 [Parameter(Mandatory=$true)][string]$EngineManifest,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [string]$DotNetRoot=''
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Project-ModuleOrder.ps1')
. (Join-Path $PSScriptRoot 'Managed-Runtime.ps1')
# 読み取った定義とコピー対象を同じhashへ固定する。検証後のBuildで別世代へ入れ替わった入力は公開しない。
$pinned=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
# ファイル内容だけを識別し、DLLロードや実行による検証を行わない。
function Hash([string]$File) { return (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() }
# 同じ実体へ別の検証結果を後から上書きしない。Buildの世代混在を明示的な失敗にする。
function Pin-Hash([string]$File,[string]$Digest) {
 $file=[IO.Path]::GetFullPath($File)
 if($Digest -cnotmatch '\A[0-9a-f]{64}\z') { throw 'Invalid Package input hash.' }
 if($pinned.ContainsKey($file) -and $pinned[$file] -cne $Digest) { throw "Conflicting Package input hashes: $file" }
 $pinned[$file]=$Digest
}
function Read-PinnedJson([string]$File) {
 # JSONをparseした後で別の内容をhashしないよう、一回の読取結果を両方に使う。
 $bytes=[IO.File]::ReadAllBytes($File); $hasher=[Security.Cryptography.SHA256]::Create()
 try { $digest=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','').ToLowerInvariant() } finally { $hasher.Dispose() }
 Pin-Hash $File $digest
 return ([Text.UTF8Encoding]::new($false,$true).GetString($bytes).TrimStart([char]0xfeff) | ConvertFrom-Json)
}
$projectFilePath=[IO.Path]::GetFullPath($ProjectFile); $root=[IO.Path]::GetDirectoryName($projectFilePath)
$project=Read-PinnedJson $projectFilePath
$engine=Read-PinnedJson $EngineManifest
$engineRoot=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($EngineManifest))
$null=@(Get-NexusModuleOrder $project); Assert-NexusRuntimeModules $project
if($project.engine.id -cne $engine.id -or $project.engine.association -cne $engine.association -or ($project.engine.version -and $project.engine.version -cne $engine.version)) { throw 'Package Engine identity mismatch.' }
$targetsFile=Resolve-NexusProjectReference $root ($project.paths.config+'/BuildTargets.json')
$targets=Read-PinnedJson $targetsFile
$managedRuntime=Get-NexusManagedRuntime $targets
$modulePlan=@(Get-NexusModuleBuildPlan $project $targets $root $engine.configuration 'Standard' $engine)
function Resolve-In([string]$Base,[string]$Reference) {
 if([IO.Path]::IsPathRooted($Reference)) { throw 'Package reference must be relative.' }
 $path=[IO.Path]::GetFullPath((Join-Path $Base $Reference))
 if(-not $path.StartsWith($Base.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Package reference escapes its root.' }; return $path
}
function Project-Output([string]$Reference) { return Resolve-In $root $Reference.Replace('{configuration}',$engine.configuration) }
function Assert-X64([string]$File) {
 # PE headerを読むだけで、Native DLLをロード・実行しない。
 $stream=[IO.File]::OpenRead($File); $reader=[IO.BinaryReader]::new($stream)
 try {
  if($reader.ReadUInt16() -ne 0x5a4d) { throw 'Runtime dependency is not PE.' }
  $stream.Position=0x3c; $offset=$reader.ReadUInt32(); if($offset -gt $stream.Length-6) { throw 'Invalid PE offset.' }
  $stream.Position=$offset; if($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw 'Package requires Windows x64 native dependencies.' }
 } finally { $reader.Dispose(); $stream.Dispose() }
}
$runtime=Project-Output $targets.runtimeExecutable
$stamp=Read-PinnedJson ($runtime+'.nxbuild')
if($stamp.projectHash -cne $pinned[$projectFilePath]) { throw 'Project changed after Build.' }
if($stamp.targetsHash -cne $pinned[$targetsFile]) { throw 'BuildTargets changed or its hash is missing. Rebuild the Standard Runtime.' }
if(@($targets.moduleProjects | Where-Object { $_ }).Count -ne @($stamp.moduleFiles | Where-Object { $_ }).Count -or @($targets.runtimeDependencies | Where-Object { $_ }).Count -ne @($stamp.runtimeFiles | Where-Object { $_ }).Count) { throw 'Build stamp does not cover all declared dependencies.' }
if($stamp.schemaVersion -ne 1 -or $stamp.projectId -cne $project.projectId -or $stamp.engineId -cne $engine.id -or $stamp.engineVersion -cne $engine.version -or $stamp.association -cne $engine.association -or $stamp.buildId -cne $engine.buildId -or $stamp.configuration -cne $engine.configuration -or $stamp.hostKind -cne 'Runtime' -or ($stamp.PSObject.Properties['buildProfile'] -and $stamp.buildProfile -cne 'Standard') -or $stamp.sha256 -cne (Hash $runtime)) { throw 'Build and verify the Standard Runtime before packaging.' }
Pin-Hash $runtime $stamp.sha256
$bin=[IO.Path]::GetDirectoryName($runtime)
$deployment=Join-Path $bin 'Nexus.modules.json'
if(-not [IO.File]::Exists($deployment)) { throw 'Rebuild the deployment manifest before packaging.' }
$roster=Read-PinnedJson $deployment
if($stamp.deploymentHash -cne $pinned[$deployment]) { throw 'Rebuild the deployment manifest before packaging.' }
if($roster.projectId -cne $project.projectId) { throw 'Module deployment belongs to another Project.' }
if(@($roster.PSObject.Properties | Where-Object { $_.Name -cnotin @('schemaVersion','projectId','native','managedAssembly','staticNative','managedRuntime') }).Count) { throw 'Unsupported Module deployment field.' }
if((Get-NexusManagedRuntime $roster) -cne $managedRuntime) { throw 'Module deployment Runtime differs from BuildTargets.' }
foreach($entry in $roster.native) {
 if($entry.id -isnot [string] -or $entry.path -isnot [string] -or @($entry.PSObject.Properties | Where-Object { $_.Name -cnotin @('id','path') }).Count) { throw 'Invalid Native Module deployment entry.' }
}
foreach($id in $roster.staticNative) { if($id -isnot [string]) { throw 'Invalid static Native Module ID.' } }
foreach($entry in @($stamp.nativeModule)+@($stamp.moduleFiles)+@($stamp.managedFiles)+@($stamp.managedBootstrap)+@($stamp.gameScripts)+@($stamp.runtimeFiles)) {
 if($null -eq $entry) { continue }; $file=Resolve-In $root $entry.path
 if($entry.sha256 -cne (Hash $file)) { throw "Stale deployment: $($entry.path)" }
 Pin-Hash $file $entry.sha256
}
# stampとdeploymentの件数だけでなく、起動するModule IDと配置先も同じ値計画へ結び付ける。
$nativePlan=@($modulePlan | Where-Object { $_.language -ceq 'Cpp' -and $_.output })
$staticPlan=@($modulePlan | Where-Object { $_.language -ceq 'Cpp' -and -not $_.output })
if($roster.schemaVersion -ne 1 -or $roster.native -isnot [Array] -or ($roster.PSObject.Properties['staticNative'] -and $roster.staticNative -isnot [Array]) -or @($roster.native).Count -ne $nativePlan.Count -or @($roster.staticNative | Where-Object { $_ }).Count -ne $staticPlan.Count) { throw 'Module deployment differs from BuildTargets.' }
foreach($module in $nativePlan) {
 $records=@($roster.native | Where-Object { $_.id -ceq $module.id })
 if($records.Count -ne 1 -or $records[0].path -cne [IO.Path]::GetFileName($module.output) -or -not $pinned.ContainsKey($module.output)) { throw "Native deployment differs from BuildTargets: $($module.id)" }
}
foreach($module in $staticPlan) { if(@($roster.staticNative | Where-Object { $_ -ceq $module.id }).Count -ne 1) { throw 'Static Native deployment differs from Project.' } }
if($roster.managedAssembly -cne $(if($targets.gameScripts) { 'Managed/GameScripts.dll' } else { '' })) { throw 'Managed deployment differs from BuildTargets.' }
if($targets.nativeModule) {
 if(-not $stamp.nativeModule -or (Resolve-In $root $stamp.nativeModule.path) -ine (Project-Output $targets.nativeModule)) { throw 'Primary Native DLL stamp differs from BuildTargets.' }
}
if($targets.managedBootstrap) {
 $bootstrap=Project-Output $targets.managedBootstrap
 $required=@($bootstrap,[IO.Path]::ChangeExtension($bootstrap,'.runtimeconfig.json'),[IO.Path]::ChangeExtension($bootstrap,'.deps.json'),(Join-Path $bin 'nethost.dll'))
 if($managedRuntime -ceq 'Mono') { $required=@($bootstrap,[IO.Path]::ChangeExtension($bootstrap,'.deps.json'),(Join-Path $bin 'Mono/bin/mono-2.0-sgen.dll'),(Join-Path $bin 'Mono/lib/mono/4.5/mscorlib.dll'),(Join-Path $bin 'Mono/etc/mono/config')) }
 if($targets.gameScripts) {
  $game=Project-Output $targets.gameScripts
  $required+=@($game,[IO.Path]::ChangeExtension($game,'.deps.json'),[IO.Path]::ChangeExtension($game,'.metadata.json'),(Join-Path $bin 'Managed/Nexus.Managed.dll'),(Join-Path $bin 'Managed/GameScripts.modules.json'))
 }
 foreach($file in $required) { if(-not $pinned.ContainsKey($file)) { throw "Build stamp omits a Managed startup file: $file" } }
}
# 件数だけでは別Module / 別ファイルへの差替えを検出できないため、現在の値計画と照合する。
foreach($module in $modulePlan | Where-Object { -not $_.primary }) {
 $records=@($stamp.moduleFiles | Where-Object { $_.id -ceq $module.id })
 if($records.Count -ne 1 -or (Resolve-In $root $records[0].path) -ine $module.output) { throw "Module stamp differs from BuildTargets: $($module.id)" }
}
$runtimeFiles=@($targets.runtimeDependencies | Where-Object { $_ })
foreach($reference in $runtimeFiles) {
 $expected=Project-Output $reference
 $records=@($stamp.runtimeFiles | Where-Object { (Resolve-In $root $_.path) -ieq $expected })
 if($records.Count -ne 1) { throw 'Runtime dependency stamp differs from BuildTargets.' }
}
$destination=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\','/')
if(-not [IO.Path]::IsPathRooted($OutputDirectory)) { throw 'Package output must be absolute.' }
if(Test-Path -LiteralPath $destination) { throw 'Package requires a new output directory.' }
# Project内部やEngine内部へPackageを重ねない。コピーの自己再帰・Source破壊を防ぐ。
foreach($protected in @($root,$engineRoot)) { if($destination -ieq $protected -or $destination.StartsWith($protected.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Package output must be outside Project and Engine roots.' } }
$parent=[IO.Path]::GetDirectoryName($destination); [IO.Directory]::CreateDirectory($parent) | Out-Null
$staging=Join-Path $parent ('.nexus-game-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($staging) | Out-Null
$copied=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
$copiedCount=0; $copiedBytes=0L
function Copy-File([string]$Source,[string]$Relative) {
 if((Get-Item -Force -LiteralPath $Source).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Package cannot include reparse points.' }
 $target=Resolve-In $staging $Relative; $digest=Hash $Source
 if($pinned.ContainsKey([IO.Path]::GetFullPath($Source)) -and $pinned[[IO.Path]::GetFullPath($Source)] -cne $digest) { throw "Package input changed after validation: $Source" }
 if($copied.ContainsKey($target)) { if($copied[$target] -cne $digest) { throw "Package destination collision: $Relative" }; return }
 $script:copiedCount++; $script:copiedBytes+=(Get-Item -LiteralPath $Source).Length
 if($script:copiedCount -gt 100000 -or $script:copiedBytes -gt 20GB) { throw 'Package exceeds file/size limits.' }
 [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
 [IO.File]::Copy($Source,$target,$false)
 if((Hash $target) -cne $digest) { throw 'Package copy verification failed.' }; $copied[$target]=$digest
}
function Copy-Tree([string]$Source,[string]$Relative,[bool]$ManagedContent=$false) {
 $directory=Get-Item -Force -LiteralPath $Source
 if($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Package data root cannot be a reparse point.' }
 [IO.Directory]::CreateDirectory((Resolve-In $staging $Relative)) | Out-Null
 foreach($item in Get-ChildItem -Force -LiteralPath $Source) {
  if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Package cannot traverse reparse points.' }
  if($item.PSIsContainer) { Copy-Tree $item.FullName ($Relative+'/'+$item.Name) $ManagedContent }
  elseif(((-not $ManagedContent) -or $item.Extension -notin @('.cs','.cpp','.vcxproj','.csproj','.pdb','.lib','.obj')) -and $item.Name -ne '.gitkeep') {
   # Managed依存libraryもBuild stampで検証したものだけを配布し、Build後の混入を受け入れない。
   if($ManagedContent -and -not $pinned.ContainsKey($item.FullName)) { throw "Managed file is not covered by the Build stamp: $($item.FullName)" }
   Copy-File $item.FullName ($Relative+'/'+$item.Name)
  }
 }
}
try {
 Assert-X64 $runtime
 Copy-File $runtime 'Bin/GameRuntime.exe'
 Copy-File $deployment 'Bin/Nexus.modules.json'
 Copy-File $projectFilePath ([IO.Path]::GetFileName($projectFilePath))
 foreach($directory in @($project.paths.assets,$project.paths.levels,$project.paths.config)) {
  $path=Resolve-In $root $directory
  foreach($code in @($project.paths.source)+@($project.modules | ForEach-Object { $_.path })) { $codePath=Resolve-In $root $code; if($path -ieq $codePath -or $path.StartsWith($codePath+'\',[StringComparison]::OrdinalIgnoreCase) -or $codePath.StartsWith($path+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Game data overlaps Source.' } }
  Copy-Tree $path $directory
 }
 [IO.Directory]::CreateDirectory((Resolve-In $staging $project.paths.source)) | Out-Null
 foreach($module in $roster.native) { Copy-File (Resolve-In $bin $module.path) ('Bin/'+$module.path) }
 foreach($dependency in @($engine.runtimeFiles)) {
  $source=Resolve-In $engineRoot $dependency; $deployed=Join-Path $bin ([IO.Path]::GetFileName($source))
  $digest=Hash $source
  if($digest -cne (Hash $deployed)) { throw 'Engine runtime dependency mismatch.' }
  Pin-Hash $source $digest; Pin-Hash $deployed $digest
  Copy-File $deployed ('Bin/'+[IO.Path]::GetFileName($deployed))
 }
 foreach($dependency in $targets.runtimeDependencies) { $file=Project-Output $dependency; Copy-File $file ('Bin/'+[IO.Path]::GetFileName($file)) }
 $data=Resolve-In $engineRoot $engine.paths.data
 foreach($directory in @('Shaders','Resources')) { $source=Join-Path $data $directory; if(Test-Path -LiteralPath $source) { Copy-Tree $source ('EngineData/'+$directory) } }
 if(-not (Test-Path -LiteralPath (Join-Path $staging 'EngineData/Shaders'))) { throw 'Package Engine shaders missing.' }
 $privateRoot=''
 if($targets.managedBootstrap) {
  if($managedRuntime -ceq 'Mono') {
   if($DotNetRoot) { throw 'Mono packaging uses the deployed Mono Runtime; DotNetRoot applies only to CoreCLR.' }
   Assert-X64 (Join-Path $bin 'Mono/bin/mono-2.0-sgen.dll')
   Copy-Tree (Join-Path $bin 'Managed') 'Bin/Managed' $true
   Copy-Tree (Join-Path $bin 'Mono') 'Bin/Mono' $true
  } else {
  Assert-X64 (Join-Path $bin 'nethost.dll')
  Copy-Tree (Join-Path $bin 'Managed') 'Bin/Managed' $true; Copy-File (Join-Path $bin 'nethost.dll') 'Bin/nethost.dll'
  $configuration=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $bin 'Managed/Nexus.Managed.Bootstrap.runtimeconfig.json') | ConvertFrom-Json
  $framework=$configuration.runtimeOptions.framework
  if(-not $framework -or $framework.name -cne 'Microsoft.NETCore.App' -or $configuration.runtimeOptions.includedFrameworks) { throw 'Only framework-dependent Microsoft.NETCore.App component hosting is supported.' }
  if($DotNetRoot) {
   # component hostingを保ったまま、公式Runtimeのhost / shared配置だけを同梱する。
   $dotnet=[IO.Path]::GetFullPath($DotNetRoot); $required=[version]$framework.version
   $runtimes=@(Get-ChildItem -Directory -LiteralPath (Join-Path $dotnet 'shared/Microsoft.NETCore.App') | Where-Object { $version=$null; [version]::TryParse($_.Name,[ref]$version) -and $version.Major -eq $required.Major -and $version.Minor -eq $required.Minor -and $version -ge $required } | Sort-Object { [version]$_.Name } -Descending)
   if(-not $runtimes.Count) { throw 'Private .NET Runtime does not satisfy runtimeconfig.' }
   $fxr=@(Get-ChildItem -Directory -LiteralPath (Join-Path $dotnet 'host/fxr') | Where-Object { $version=$null; [version]::TryParse($_.Name,[ref]$version) -and $version.Major -eq $required.Major -and [IO.File]::Exists((Join-Path $_.FullName 'hostfxr.dll')) } | Sort-Object { [version]$_.Name } -Descending)[0]
   if(-not $fxr) { throw 'Private hostfxr missing.' }
   Assert-X64 (Join-Path $fxr.FullName 'hostfxr.dll'); Assert-X64 (Join-Path $runtimes[0].FullName 'coreclr.dll')
   Copy-Tree $runtimes[0].FullName ('DotNet/shared/Microsoft.NETCore.App/'+$runtimes[0].Name)
   Copy-Tree $fxr.FullName ('DotNet/host/fxr/'+$fxr.Name)
   foreach($notice in @('LICENSE.txt','ThirdPartyNotices.txt')) { if(Test-Path -LiteralPath (Join-Path $dotnet $notice)) { Copy-File (Join-Path $dotnet $notice) ('DotNet/'+$notice) } }
   $privateRoot='DotNet'
  }
  }
 } elseif($DotNetRoot) { throw 'Private .NET Runtime is only valid for a Managed Project.' }
 $sdk=[IO.Path]::GetFullPath((Join-Path $engineRoot $engine.paths.sdk))
 foreach($notice in @('ThirdParty/DotNetHosting/LICENSE.TXT','ThirdParty/DotNetHosting/UPSTREAM.md')) {
  $file=Join-Path $sdk $notice; if($targets.managedBootstrap -and $managedRuntime -cne 'Mono' -and [IO.File]::Exists($file)) { Copy-File $file ('Notices/DotNetHosting/'+[IO.Path]::GetFileName($file)) }
 }
 # 開発マシンの絶対EngineManifestやBuildログは配布の起動契約に使用しない。
 $inventory=@(); foreach($entry in $copied.GetEnumerator()) { $inventory+=@{path=$entry.Key.Substring($staging.Length+1).Replace('\','/');sha256=$entry.Value} }
 $manifest=@{schemaVersion=1;project=[IO.Path]::GetFileName($projectFilePath);engineData='EngineData';dotnetRoot=$privateRoot;files=@($inventory | Sort-Object path);configuration=$engine.configuration;engineId=$engine.id;engineVersion=$engine.version}
 $manifestText=$manifest | ConvertTo-Json -Depth 12
 if([Text.Encoding]::UTF8.GetByteCount($manifestText) -gt 1MB) { throw 'Package inventory exceeds the Runtime manifest limit.' }
 [IO.File]::WriteAllText((Join-Path $staging 'Bin/NexusGame.package.json'),$manifestText,[Text.UTF8Encoding]::new($false))
 # copy後に定義やstampが変わった場合も新しい配布を成功扱いしない。既存成果物は変更しない。
 foreach($inputFile in $pinned.Keys) { if((Hash $inputFile) -cne $pinned[$inputFile]) { throw "Package input changed before publication: $inputFile" } }
 # すべてのcopyとhash確認後だけ公開する。移動先が途中で作られた場合も上書きしない。
 [IO.Directory]::Move($staging,$destination)
 Write-Output ('Package created: '+(Join-Path $destination 'Bin/GameRuntime.exe'))
} catch {
 # 自分が新規生成したstaging以外は削除しない。検証済み絶対配置だけを対象にする。
 $checked=[IO.Path]::GetFullPath($staging)
 if([IO.Path]::GetDirectoryName($checked) -ieq $parent -and [IO.Path]::GetFileName($checked).StartsWith('.nexus-game-') -and [IO.Directory]::Exists($checked)) { Remove-Item -LiteralPath $checked -Recurse -Force }
 throw
}
