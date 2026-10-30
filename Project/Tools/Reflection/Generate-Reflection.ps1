param([string]$ProjectRoot = (Join-Path $PSScriptRoot '../..'))
$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$hashAlgorithm = [Security.Cryptography.SHA256]::Create()
try { $rootHash = [BitConverter]::ToString($hashAlgorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($ProjectRoot.TrimEnd('\', '/').ToUpperInvariant()))).Replace('-', '') }
finally { $hashAlgorithm.Dispose() }
# 同じプロジェクトを複数の MSBuild や Broker が生成しても、生成物の書込みを競合させない。
$generationMutex = [Threading.Mutex]::new($false, ('Local\NexusReflection_' + $rootHash))
$ownsGenerationMutex = $false
try {
try { $ownsGenerationMutex = $generationMutex.WaitOne(60000) }
catch [Threading.AbandonedMutexException] { $ownsGenerationMutex = $true }
if(-not $ownsGenerationMutex) { throw 'Timed out waiting for the reflection generator.' }
$generatedDirectory = Join-Path $ProjectRoot 'Runtime/Reflection/Generated'
[IO.Directory]::CreateDirectory($generatedDirectory) | Out-Null
$schemas = @()
$ids = @{}
$types = @{}
# 制限された宣言マクロだけを解析する。未対応構文は黙って読み飛ばさずエラーにする。
foreach($sourceRoot in @('Foundation', 'Runtime', 'Graphics', 'Editor', 'Samples', 'Game')) {
    if(-not (Test-Path -LiteralPath (Join-Path $ProjectRoot $sourceRoot))) { continue }
    foreach($header in Get-ChildItem -LiteralPath (Join-Path $ProjectRoot $sourceRoot) -Recurse -Filter '*.h' -File | Sort-Object FullName) {
        if($header.FullName.Substring($ProjectRoot.Length) -match '[/\\]Generated[/\\]') { continue }
        $source = [IO.File]::ReadAllText($header.FullName)
        $source = [regex]::Replace($source, '(?m)^\s*#\s*define[^\r\n]*', '')
        $source = [regex]::Replace($source, '(?s)/\*.*?\*/|//[^\r\n]*', '')
        foreach($match in [regex]::Matches($source, 'NEXUS_REFLECT_(VALUE|COMPONENT)\s*\(([^)]*)\)')) {
            $kind = $match.Groups[1].Value
            $arguments = @($match.Groups[2].Value.Split(',') | ForEach-Object { $_.Trim() })
            $firstMember = if($kind -eq 'COMPONENT') { 3 } else { 2 }
            if($arguments.Count -le $firstMember -or $arguments[0] -notmatch '^\w+(::\w+)*$' -or $arguments[1] -notmatch '^"[A-Za-z0-9_.-]+"$') { throw "Invalid reflection declaration: $($header.FullName)" }
            $type = $arguments[0]; $id = $arguments[1]
            if($ids.ContainsKey($id) -or $types.ContainsKey($type)) { throw "Duplicate reflected type: $type / $id" }
            $ids[$id] = $true; $types[$type] = $true
            $label = if($kind -eq 'COMPONENT') { $arguments[2] } else { '""' }
            if($label -notmatch '^"[A-Za-z0-9_.-]*"$') { throw "Invalid reflection label: $type" }
            $members = @(); $memberIds = @{}
            foreach($argument in $arguments[$firstMember..($arguments.Count - 1)]) {
                if($argument -notmatch '^(\w+)(?:\s*=\s*(\w+))?$') { throw "Invalid reflection member: $type / $argument" }
                $member = $Matches[1]; $memberId = if($Matches[2]) { $Matches[2] } else { $member }
                if($memberIds.ContainsKey($memberId)) { throw "Duplicate reflection field: $type / $memberId" }
                $memberIds[$memberId] = $true
                $members += [pscustomobject]@{ Name = $member; Id = $memberId }
            }
            $relative = $header.FullName.Substring($ProjectRoot.Length).TrimStart('\', '/').Replace('\', '/')
            $schemas += [pscustomobject]@{ Kind = $kind; Type = $type; Id = $id; Label = $label; Header = $relative; Members = $members }
        }
    }
}
$schemas = @($schemas | Sort-Object Type)
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('// 自動生成ファイル。型定義側の NEXUS_REFLECT 宣言から再生成する。')
$lines.Add('#pragma once')
$lines.Add('#include "Foundation/Reflection/ReflectionValue.h"')
foreach($include in @($schemas.Header | Sort-Object -Unique)) { $lines.Add("#include `"$include`"") }
$lines.Add('namespace NexusEngine {')
foreach($schema in $schemas) {
    $lines.Add("template<> struct ReflectionTraits<$($schema.Type)> {")
    $lines.Add("    static constexpr const char* Id = $($schema.Id);")
    $lines.Add("    static constexpr const char* Label = $($schema.Label);")
    $lines.Add('    template<class Visitor> static void Visit(Visitor&& visitor) {')
    foreach($member in $schema.Members) { $lines.Add("        visitor(`"$($member.Id)`", &$($schema.Type)::$($member.Name));") }
    $lines.Add('    }'); $lines.Add('};')
}
$lines.Add('}')
$headerText = ($lines -join "`n") + "`n"
$lines.Clear()
$lines.Add('// 自動生成ファイル。型定義側の NEXUS_REFLECT 宣言から再生成する。')
$lines.Add('#include "GeneratedReflection.h"')
$lines.Add('#include "Runtime/Reflection/ComponentReflection.h"')
$lines.Add('#include "Runtime/Reflection/ReflectedInspector.h"')
$lines.Add('namespace NexusEngine {')
$lines.Add('void RegisterGeneratedComponents(ComponentReflection& registry) {')
foreach($schema in $schemas | Where-Object Kind -eq 'COMPONENT') { $lines.Add("    registry.Register<$($schema.Type)>();") }
$lines.Add('}')
$lines.Add('void RegisterGeneratedInspectorTypes(TypeRegistry& registry) {')
foreach($schema in $schemas | Where-Object Kind -eq 'COMPONENT') { $lines.Add("    RegisterReflectedInspector<$($schema.Type)>(registry);") }
$lines.Add('}'); $lines.Add('}')
$sourceText = ($lines -join "`n") + "`n"
# 内容が変わった生成物だけ更新し、通常ビルドと Live++ の不要な再コンパイルを避ける。
foreach($file in @(@('GeneratedReflection.h', $headerText), @('GeneratedReflection.cpp', $sourceText))) {
    $path = Join-Path $generatedDirectory $file[0]
    if(-not [IO.File]::Exists($path) -or [IO.File]::ReadAllText($path) -cne $file[1]) {
        [IO.File]::WriteAllText($path, $file[1], [Text.UTF8Encoding]::new($false))
    }
}
Write-Output "Reflection: $($schemas.Count) types generated."
} finally {
    if($ownsGenerationMutex) { $generationMutex.ReleaseMutex() }
    $generationMutex.Dispose()
}
