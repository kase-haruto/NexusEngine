param([Parameter(Mandatory=$true)][string]$ProjectFile,[Parameter(Mandatory=$true)][string]$EngineManifest,[Parameter(Mandatory=$true)][string]$ModuleId,[Parameter(Mandatory=$true)][string]$Name,
 [ValidateSet('Behaviour','System')][string]$Kind='Behaviour')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Project-ModuleOrder.ps1')
$root=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($ProjectFile))
# Module追加と同じlockを、Project読取からsource公開まで保持する。宣言と生成先の食い違いを防ぐ。
$lock=[IO.File]::Open((Join-Path $root '.nexus.modules.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
$project=Get-Content -Raw -Encoding UTF8 -LiteralPath $ProjectFile | ConvertFrom-Json
$engine=Get-Content -Raw -Encoding UTF8 -LiteralPath $EngineManifest | ConvertFrom-Json; $templates=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($EngineManifest))) $engine.paths.templates
if($project.engine.id -cne $engine.id -or $project.engine.association -cne $engine.association -or ($project.engine.version -and $project.engine.version -cne $engine.version)) { throw 'Engine association / version mismatch.' }
$null=@(Get-NexusModuleOrder $project); Assert-NexusRuntimeModules $project
if($Name -cnotmatch '\A[A-Za-z][A-Za-z0-9_]{0,63}\z' -or $Name -imatch '\A(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])\z') { throw 'Invalid source identifier.' }
$keywords='abstract alignas alignof and and_eq as asm auto base bitand bitor bool break byte case catch char char8_t char16_t char32_t checked class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decimal decltype default delegate delete do double dynamic_cast else enum event explicit export extern false finally fixed float for foreach friend goto if implicit in inline int interface internal is lock long mutable namespace new noexcept not not_eq null nullptr object operator or or_eq out override params private protected public readonly ref register reinterpret_cast requires return sbyte sealed short signed sizeof stackalloc static static_assert static_cast string struct switch template this thread_local throw true try typedef typeid typename uint ulong unchecked union unsafe unsigned ushort using virtual void volatile wchar_t while xor xor_eq'.Split(' ')
if($Name -cin $keywords) { throw 'Source name cannot be a C++ / C# keyword.' }
$module=@($project.modules | Where-Object { $_.id -ceq $ModuleId }); if($module.Count -ne 1) { throw 'Select an existing Module.' }; $module=$module[0]
$directory=[IO.Path]::GetFullPath((Join-Path $root $module.path)); if(-not $directory.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or -not [IO.Directory]::Exists($directory)) { throw 'Invalid Module Source directory.' }
$cursor=$directory; while($cursor -and $cursor.Length -ge $root.Length) { if((Get-Item -Force -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Source cannot traverse a reparse point.' }; $cursor=[IO.Path]::GetDirectoryName($cursor) }
if($module.language -ceq 'CSharp') {
 if($Name -cin @('OnCreate','OnEnable','OnUpdate','OnDisable','OnDestroy')) { throw 'Behaviour name conflicts with a generated member.' }
 $namespace='NexusGameScripts.M'+(Get-NexusModuleCodeIdentifier $ModuleId)
 # 旧生成のModule.Behaviour等と衝突しない系列を使う。保存済みScript IDは書き換えない。
 $text=[IO.File]::ReadAllText((Join-Path $templates 'Behaviour/CSharp.cs.in')).Replace('@NAMESPACE@',$namespace).Replace('@CLASS@',$Name).Replace('@TYPE_ID@',$ModuleId+'.behaviours.'+$Name)
 $file=Join-Path $directory ($Name+'.cs'); if(Test-Path -LiteralPath $file) { throw 'Source already exists.' }
 $temporary=$file+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
 try { [IO.File]::WriteAllText($temporary,$text,[Text.UTF8Encoding]::new($false)); [IO.File]::Move($temporary,$file) }
 finally { if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
} else {
 if($Kind -ceq 'Behaviour') {
  & (Join-Path $PSScriptRoot 'Add-NativeBehaviour.ps1') -Directory $directory -Templates $templates -ModuleId $ModuleId -Name $Name
  Write-Output ('C++ Behaviour created: '+$Name+'. Build and restart the Host, then attach the Behaviour in Details.')
  return
 }
 # 既存scaffoldのstate型・System ID・メンバと同名の型を作ると、コンパイルまたは登録で衝突する。
 if($Name -cmatch '\A(Nexus|NEXUS_)' -or $Name -cin @('std','NativeMover','GameComponents','RegisterNativeMover','ConnectNativeMover','ConnectSettings','CrowdSimulation','GameState','TargetRequest','NativeCombat','Target','Descriptor','Update','SetTarget','game_','world_','targets_','settings_','settingsType_','RegisterCrowdBinding','RebindCrowdBinding','InvokeCrowdSetTarget','CrowdPatchContract')) { throw 'Native System name conflicts with the Public API or scaffold.' }
 # 既存の明示V2登録を持つ生成scaffoldだけを拡張する。任意C++を解析・自動Bindingしない。
 $main=Join-Path $directory 'Main.cpp'; $before=[IO.File]::ReadAllBytes($main); $text=[Text.Encoding]::UTF8.GetString($before).TrimStart([char]0xfeff)
 $anchor='RegisterCrowdBinding(*context)'
 if(-not $text.Contains($anchor) -or -not $text.Contains('return RebindCrowdBinding(*context);')) { throw 'Native System creation requires the explicit scaffold registration contract; no files changed.' }
 if(-not $text.Contains('const auto registered = RegisterCrowdBinding(*context); if(registered != NEXUS_GAME_OK) return registered;') -and -not $text.Contains('return RegisterCrowdBinding(*context);')) { throw 'Native registration statement was customized. Register the System / Binding explicitly; no files changed.' }
 $writes=@{}
 foreach($file in @('CrowdSimulationSystem.h','CrowdSimulationSystem.cpp','CrowdBinding.h')) {
  # 一度だけ置換し、CrowdAgent等の新しい名前を次の置換で再加工しない。
  $body=[regex]::Replace([IO.File]::ReadAllText((Join-Path $templates ('NativeModule/'+$file+'.in'))),'CrowdSimulationSystem|CrowdSimulation|Crowd',$Name)
  $body=$body.Replace('{"'+$Name+'", 15}','{"'+$Name+'", '+$Name.Length+'}')
  $target=$(if($file -eq 'CrowdBinding.h') { $Name+'Binding.h' } else { $file.Replace('CrowdSimulationSystem',$Name) })
  $path=Join-Path $directory $target; if(Test-Path -LiteralPath $path) { throw 'Native source already exists.' }; $writes[$path]=$body
 }
 # 明示SetTargetサンプルと同じ署名のFacadeだけを生成する。任意C++型の自動公開ではない。
 $managed=@($project.modules | Where-Object { $_.language -ceq 'CSharp' } | Select-Object -First 1)
 if($managed.Count) {
  $managedRoot=[IO.Path]::GetFullPath((Join-Path $root $managed[0].path))
  if(-not $managedRoot.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or -not [IO.Directory]::Exists($managedRoot)) { throw 'Invalid Managed source directory.' }
  $cursor=$managedRoot; while($cursor -and $cursor.Length -ge $root.Length) { if((Get-Item -Force -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Managed Source cannot traverse a reparse point.' }; $cursor=[IO.Path]::GetDirectoryName($cursor) }
  # 先に元のtokenを分離する。Module IDがCrowdSimulationでもSystem名へ誤変換しない。
  $facade=[IO.File]::ReadAllText((Join-Path $templates 'Hybrid/CrowdSimulation.cs.in')).Replace('"NativeCombat"','"@NEXUS_MODULE_ID@"').Replace('"CrowdSimulation"','"@NEXUS_SYSTEM_ID@"').Replace('class CrowdSimulation','class @NEXUS_FACADE_CLASS@')
  $facade=$facade.Replace('@NEXUS_MODULE_ID@',$ModuleId).Replace('@NEXUS_SYSTEM_ID@',$Name).Replace('@NEXUS_FACADE_CLASS@',$Name)
  $path=Join-Path $managedRoot ($Name+'.cs'); if(Test-Path -LiteralPath $path) { throw 'Managed facade already exists.' }; $writes[$path]=$facade
 }
 $text='#include "'+$Name+'Binding.h"'+"`r`n"+$text
 $text=$text.Replace('const auto registered = RegisterCrowdBinding(*context); if(registered != NEXUS_GAME_OK) return registered;', 'const auto registered = RegisterCrowdBinding(*context); if(registered != NEXUS_GAME_OK) return registered;'+"`r`n"+'    const auto extra'+$Name+' = Register'+$Name+'Binding(*context); if(extra'+$Name+' != NEXUS_GAME_OK) return extra'+$Name+';')
 # Hybrid形式も明示的な既存returnを順次登録へ置換する。
 $text=$text.Replace('return RegisterCrowdBinding(*context);','const auto extra'+$Name+' = Register'+$Name+'Binding(*context); if(extra'+$Name+' != NEXUS_GAME_OK) return extra'+$Name+'; return RegisterCrowdBinding(*context);')
 $text=$text.Replace('return RebindCrowdBinding(*context);','const auto rebind'+$Name+' = Rebind'+$Name+'Binding(*context); if(rebind'+$Name+' != NEXUS_GAME_OK) return rebind'+$Name+'; return RebindCrowdBinding(*context);')
 $created=@()
 try {
  if([Convert]::ToBase64String([IO.File]::ReadAllBytes($main)) -cne [Convert]::ToBase64String($before)) { throw 'Concurrent Native source modification.' }
  foreach($entry in $writes.GetEnumerator()) {
   # 途中まで書かれたcppを公開しない。新規名へのmoveだけを許し、別の編集者のファイルは上書きしない。
   $temporary=$entry.Key+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
   try { [IO.File]::WriteAllText($temporary,$entry.Value,[Text.UTF8Encoding]::new($false)); [IO.File]::Move($temporary,$entry.Key); $created+=$entry.Key }
   finally { if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
  }
  if([Convert]::ToBase64String([IO.File]::ReadAllBytes($main)) -cne [Convert]::ToBase64String($before)) { throw 'Concurrent Native source modification before commit.' }
  $temporary=$main+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
  # Windows PowerShellでは$nullが空文字へ変換されるため、backupなしを明示するNullStringを渡す。
  try { [IO.File]::WriteAllText($temporary,$text,[Text.UTF8Encoding]::new($false)); [IO.File]::Replace($temporary,$main,[System.Management.Automation.Language.NullString]::Value) }
  finally { if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
 } catch { foreach($file in $created) { if([IO.File]::Exists($file) -and [IO.File]::ReadAllText($file) -ceq $writes[$file]) { [IO.File]::Delete($file) } }; throw }
}
if($module.language -ceq 'Cpp') { Write-Output ('Native System created: '+$Name+'. Run a full Standard Build and restart the Host; new registration / headers require a fresh Module lifecycle.') }
else { Write-Output ('Behaviour created: '+$Name+'. Build or Reload C# using the common Project commands.') }
} finally { $lock.Dispose() }
