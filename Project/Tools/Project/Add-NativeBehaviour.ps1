param(
 [Parameter(Mandatory=$true)][string]$Directory,
 [Parameter(Mandatory=$true)][string]$Templates,
 [Parameter(Mandatory=$true)][string]$ModuleId,
 [Parameter(Mandatory=$true)][string]$Name
)
$ErrorActionPreference='Stop'
# Add-GameSourceがModule検証と共有lockを保持したまま呼ぶ内部生成処理。
if($Name -cnotmatch '\A[A-Za-z][A-Za-z0-9_]{0,63}\z' -or $Name -cmatch '\A(Nexus|NEXUS_)' -or
 $Name -cin @('std','NativeMover','CrowdSimulationSystem','CrowdSimulation','GameComponents','GameState','NativeCombat',
 'OnCreate','OnEnable','OnUpdate','OnFixedUpdate','OnDisable','OnDestroy','context_','RegisterGeneratedBehaviours','ConnectGeneratedBehaviours')) {
 throw 'Behaviour name conflicts with a lifecycle member or the Native scaffold.'
}
$directoryPath=[IO.Path]::GetFullPath($Directory)
$utf8=[Text.UTF8Encoding]::new($false)
$before=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
$writes=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
function Original([string]$Name) {
 $path=Join-Path $directoryPath $Name
 if(-not $before.ContainsKey($path)) {
  if([IO.File]::Exists($path)) {
   if((Get-Item -Force -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Generated source cannot be a reparse point.' }
   $before[$path]=[IO.File]::ReadAllText($path)
  } elseif([IO.Directory]::Exists($path)) { throw "A directory occupies the generated file: $Name" }
  else { $before[$path]=$null }
 }
 return $before[$path]
}
function Plan([string]$File,[string]$Text,[bool]$New=$false) {
 $old=Original $File
 if($New -and $null -ne $old) { throw "Source already exists: $File" }
 $writes[(Join-Path $directoryPath $File)]=$Text
}
function CppString([string]$Value) { return '"'+$Value.Replace('\','\\').Replace('"','\"')+'"' }
function Registry([string[]]$Names) {
 $types=@(); $includes=''; $register=''; $connect=''
 foreach($class in $Names) {
  $typeId=$ModuleId+'.behaviours.'+$class
  $types+=,[ordered]@{id=$typeId;fullName=$ModuleId+'.'+$class;version=1;aliases=@();properties=@(
   [ordered]@{id='enabled';label='Enabled';aliases=@();kind='Bool';default=$true});sourceFiles=@(($class+'.h'),($class+'.cpp'))}
  $includes+='#include "'+$class+'.h"'+"`n"
  $system='behaviour.'+$class
  $systemLiteral=CppString $system; $typeLiteral=CppString $typeId
  $register+=@"
    {
        const auto descriptor = NexusGame::EntityBehaviourSystem<$class>::Descriptor({$systemLiteral, $($system.Length)});
        uint64_t system = 0;
        const auto status = registration.systems.add(registration.scope, &descriptor, &system);
        if(status != NEXUS_GAME_OK) {
            return status;
        }
    }

"@
  $connect+=@"
    {
        uint64_t system = 0;
        auto status = registration.systems.find(registration.scope, {$systemLiteral, $($system.Length)}, 1, &system);
        if(status != NEXUS_GAME_OK) {
            return status;
        }
        status = registration.systems.access(registration.scope, system, [](void* state, void* user) noexcept -> NexusGameStatus {
            static constexpr char type[] = $typeLiteral;
            return NexusGame::EntityBehaviourSystem<$class>::Connect(state,
                *static_cast<const NexusGameServicesContextV1*>(user), {type, sizeof(type) - 1});
        }, const_cast<NexusGameServicesContextV1*>(&services));
        if(status != NEXUS_GAME_OK) {
            return status;
        }
    }

"@
 }
 $json=','+(($types | ForEach-Object { ConvertTo-Json -InputObject $_ -Depth 10 -Compress }) -join ',')
 if([Text.Encoding]::UTF8.GetByteCount($json) -gt 60000) { throw 'Generated Behaviour metadata exceeds the supported source size.' }
 $header=@"
// Nexus generated Behaviour registry v1. Edit the Behaviour .h/.cpp files instead.
#pragma once
#include <Nexus/NativeGameSystemApi.h>
#include <Nexus/GameServices.h>
#define NEXUS_GENERATED_BEHAVIOUR_METADATA $(CppString $json)
NexusGameStatus RegisterGeneratedBehaviours(const NexusNativeSystemsRegistrationV1& registration) noexcept;
NexusGameStatus ConnectGeneratedBehaviours(const NexusNativeSystemsRegistrationV1& registration, const NexusGameServicesContextV1& services) noexcept;
"@
 $source=@"
// Nexus generated Behaviour registry v1. Do not put gameplay logic in this file.
#include "NexusGeneratedBehaviours.h"
$includes
// Register each Entity Behaviour with the existing scheduler; no separate ECS is created.
NexusGameStatus RegisterGeneratedBehaviours(const NexusNativeSystemsRegistrationV1& registration) noexcept {
$register    return NEXUS_GAME_OK;
}

// Borrow the Module's services for the lifetime already managed by its loader.
NexusGameStatus ConnectGeneratedBehaviours(const NexusNativeSystemsRegistrationV1& registration, const NexusGameServicesContextV1& services) noexcept {
$connect    return NEXUS_GAME_OK;
}
"@
 return @{Header=$header;Source=$source}
}
$main=Original 'Main.cpp'; $components=Original 'GameComponents.h'
if($null -eq $main -or $null -eq $components -or -not $main.Contains('RegisterCrowdBinding(*context)')) {
 throw 'Entity Behaviour creation requires a Native Game Module. Add a C++ Module in the Project panel first. No files changed.'
}
$configuration=Original 'NexusGeneratedBehaviours.json'
$names=@()
if($null -ne $configuration) {
 $saved=$configuration | ConvertFrom-Json
 if($saved.schemaVersion -ne 1 -or $saved.moduleId -cne $ModuleId -or @($saved.classes).Count -ge 254) { throw 'Invalid or full generated Behaviour registry.' }
 $names=@($saved.classes)
 foreach($class in $names) {
  if($class -cnotmatch '\A[A-Za-z][A-Za-z0-9_]{0,63}\z') { throw 'Invalid generated Behaviour name.' }
 }
 if(@($names | Select-Object -Unique).Count -ne $names.Count) { throw 'Duplicate generated Behaviour name.' }
 $previous=Registry $names
 if((Original 'NexusGeneratedBehaviours.h') -cne $previous.Header -or (Original 'NexusGeneratedBehaviours.cpp') -cne $previous.Source) {
  throw 'Generated registration files were customized. No files changed; preserve or restore the registry before adding another Behaviour.'
 }
 if(-not $main.Contains('RegisterGeneratedBehaviours(state.systems)') -or -not $main.Contains('ConnectGeneratedBehaviours(') -or
  -not $components.Contains('NEXUS_GENERATED_BEHAVIOUR_METADATA')) { throw 'Generated Behaviour registration hooks were customized. No files changed.' }
} else {
 if($null -ne (Original 'NexusGeneratedBehaviours.h') -or $null -ne (Original 'NexusGeneratedBehaviours.cpp')) { throw 'Untracked Behaviour registry files already exist.' }
 $registerAnchor='return RegisterNativeMover(state.systems);'
 $nativeConnect='const auto status = ConnectNativeMover(systems, *context); if(status != NEXUS_GAME_OK) return status;'
 $hybridConnect='const auto connected = ConnectNativeMover(state.systems, *context); if(connected != NEXUS_GAME_OK) return connected;'
 $metadataAnchor='}]}]}";'
 if(-not $main.Contains($registerAnchor) -or (-not $main.Contains($nativeConnect) -and -not $main.Contains($hybridConnect)) -or
  -not $components.Contains($metadataAnchor) -or $components.Contains('NEXUS_GENERATED_BEHAVIOUR_METADATA')) {
  throw 'Native scaffold registration or metadata was customized. No files changed.'
 }
 $main=$main.Replace($registerAnchor,@'
const auto mover = RegisterNativeMover(state.systems);
    if(mover != NEXUS_GAME_OK) {
        return mover;
    }
    return RegisterGeneratedBehaviours(state.systems);
'@)
 $systems=if($main.Contains($nativeConnect)) { 'systems' } else { 'state.systems' }
 $anchor=if($main.Contains($nativeConnect)) { $nativeConnect } else { $hybridConnect }
 $main=$main.Replace($anchor,$anchor+"`n"+@"
    const auto behaviours = ConnectGeneratedBehaviours($systems, *context);
    if(behaviours != NEXUS_GAME_OK) {
        return behaviours;
    }
"@)
 $components=$components.Replace('#pragma once',"#pragma once`n"+'#include "NexusGeneratedBehaviours.h"')
 $components=$components.Replace($metadataAnchor,'}]}" NEXUS_GENERATED_BEHAVIOUR_METADATA "]}";')
 Plan 'Main.cpp' $main
 Plan 'GameComponents.h' $components
}
if($names -ccontains $Name) { throw 'Behaviour already exists.' }
foreach($extension in @('h','cpp')) {
 $text=[IO.File]::ReadAllText((Join-Path $Templates ('Behaviour/Native.'+$extension+'.in'))).Replace('@CLASS@',$Name)
 Plan ($Name+'.'+$extension) $text $true
}
$names+=,$Name
$registry=Registry $names
Plan 'NexusGeneratedBehaviours.h' $registry.Header
Plan 'NexusGeneratedBehaviours.cpp' $registry.Source
Plan 'NexusGeneratedBehaviours.json' (ConvertTo-Json -InputObject ([ordered]@{schemaVersion=1;moduleId=$ModuleId;classes=@($names)}) -Depth 4 -Compress)

$committed=@()
try {
 foreach($entry in $writes.GetEnumerator()) {
  $current=if([IO.File]::Exists($entry.Key)) { [IO.File]::ReadAllText($entry.Key) } else { $null }
  if($current -cne $before[$entry.Key]) { throw 'Concurrent source modification; generation cancelled.' }
 }
 foreach($entry in $writes.GetEnumerator()) {
  $temporary=$entry.Key+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
  try {
   [IO.File]::WriteAllText($temporary,$entry.Value,$utf8)
   $current=if([IO.File]::Exists($entry.Key)) { [IO.File]::ReadAllText($entry.Key) } else { $null }
   if($current -cne $before[$entry.Key]) { throw 'Concurrent source modification before commit.' }
   if($null -eq $before[$entry.Key]) { [IO.File]::Move($temporary,$entry.Key) }
   else { [IO.File]::Replace($temporary,$entry.Key,[System.Management.Automation.Language.NullString]::Value) }
   $committed+=,$entry.Key
  } finally { if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
 }
} catch {
 foreach($path in $committed) {
  # この処理が書いた内容だけを巻き戻し、途中で利用者が変更したファイルは保持する。
  if([IO.File]::Exists($path) -and [IO.File]::ReadAllText($path) -ceq $writes[$path]) {
   if($null -eq $before[$path]) { [IO.File]::Delete($path) }
   else { [IO.File]::WriteAllText($path,$before[$path],$utf8) }
  }
 }
 throw
}
