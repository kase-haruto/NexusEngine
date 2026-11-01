param(
    [Parameter(Mandatory=$true)][string]$SourceManifest,
    [Parameter(Mandatory=$true)][string]$Destination,
    [Parameter(Mandatory=$true)][string]$EngineId,
    [Parameter(Mandatory=$true)][string]$Version
)
$ErrorActionPreference = 'Stop'
$sourceFile = [IO.Path]::GetFullPath($SourceManifest)
$sourceRoot = [IO.Path]::GetDirectoryName($sourceFile)
$source = Get-Content -LiteralPath $sourceFile -Raw -Encoding UTF8 | ConvertFrom-Json
if($source.schemaVersion -ne 1 -or $source.association -cne 'development' -or $source.toolset -cne 'v145') { throw 'Publish requires a Development Engine with toolset v145.' }
if($source.configuration -cnotin @('Debug','Develop','Release')) { throw 'Invalid Engine configuration.' }
if($EngineId -cnotmatch '^[A-Za-z0-9_.-]+$' -or $Version -cnotmatch '^\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?$') { throw 'Select an explicit installed Engine ID and semantic version.' }
function Within([string]$Root,[string]$Relative) {
    if([IO.Path]::IsPathRooted($Relative)) { throw 'Engine path must be relative.' }
    $path = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if(-not $path.StartsWith($Root.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Engine path escapes its Root.' }
    return $path
}
$sdkSource = Within $sourceRoot $source.paths.sdk
$destinationPath = [IO.Path]::GetFullPath($Destination).TrimEnd('\','/')
if([IO.Directory]::Exists($destinationPath) -or [IO.File]::Exists($destinationPath)) { throw 'Installed Engine destination already exists. It will not be overwritten.' }
if($destinationPath.StartsWith($sdkSource.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Install outside the Engine source directories.' }
$parent = [IO.Path]::GetDirectoryName($destinationPath)
if(-not $parent) { throw 'Installed Engine destination needs a parent directory.' }
[IO.Directory]::CreateDirectory($parent) | Out-Null
$staging = Join-Path $parent ('.nexus-install-' + [guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($staging) | Out-Null
$work = Join-Path $staging '.Build'
$sdk = Join-Path $staging 'SDK'
$bin = Join-Path $staging 'Bin'
$configuration = $source.configuration
$utf8 = [Text.UTF8Encoding]::new($false)
function Digest([string]$File) {
    $hash = [Security.Cryptography.SHA256]::Create(); $inputFile = [IO.File]::OpenRead($File)
    try { return [BitConverter]::ToString($hash.ComputeHash($inputFile)).Replace('-','').ToLowerInvariant() }
    finally { $inputFile.Dispose(); $hash.Dispose() }
}
function Copy-File([string]$From,[string]$To) {
    if(-not [IO.File]::Exists($From)) { throw "Missing Engine input: $From" }
    $item = Get-Item -LiteralPath $From
    if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Engine input must not be a link: $From" }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($To)) | Out-Null
    [IO.File]::Copy($From,$To,$false)
}
function Copy-Tree([string]$From,[string]$To,[scriptblock]$Filter) {
    if(-not [IO.Directory]::Exists($From)) { throw "Missing Engine directory: $From" }
    foreach($item in Get-ChildItem -LiteralPath $From -Recurse -Force) {
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Engine directory contains a link: $($item.FullName)" }
        if(-not $item.PSIsContainer -and (& $Filter $item)) {
            Copy-File $item.FullName (Join-Path $To $item.FullName.Substring($From.Length).TrimStart('\','/'))
        }
    }
}
function Escape-Property([string]$Value) {
    $text = $Value.Replace('%','%25')
    foreach($pair in @(@(';','%3B'),@('$','%24'),@('@','%40'),@("'",'%27'),@('(','%28'),@(')','%29'),@('*','%2A'),@('?','%3F'))) { $text = $text.Replace($pair[0],$pair[1]) }
    return $text
}
function Xml([string]$Value) { return [Security.SecurityElement]::Escape((Escape-Property $Value)) }
function Quote([string]$Value) {
    $text = [Text.StringBuilder]::new(); [void]$text.Append('"'); $slashes = 0
    foreach($character in $Value.ToCharArray()) {
        if($character -eq '\') { $slashes++; continue }
        if($character -eq '"') { [void]$text.Append('\',$slashes * 2 + 1) } else { [void]$text.Append('\',$slashes) }
        [void]$text.Append($character); $slashes = 0
    }
    [void]$text.Append('\',$slashes * 2); [void]$text.Append('"'); return $text.ToString()
}
function Invoke-Child([string]$Executable,[string[]]$Arguments,[string]$Directory) {
    $start = [Diagnostics.ProcessStartInfo]::new($Executable); $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.WorkingDirectory = $Directory; $start.Arguments = ($Arguments | ForEach-Object { Quote $_ }) -join ' '
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $values = @{}; foreach($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) { $values[$entry.Key] = $entry.Value }
    $start.EnvironmentVariables.Clear(); foreach($key in $values.Keys) { $start.EnvironmentVariables[$key] = $values[$key] }
    $child = [Diagnostics.Process]::Start($start)
    try {
        $copyOutput = $child.StandardOutput.BaseStream.CopyToAsync([Console]::OpenStandardOutput())
        $copyError = $child.StandardError.BaseStream.CopyToAsync([Console]::OpenStandardError())
        $child.WaitForExit(); $null = $copyOutput.GetAwaiter().GetResult(); $null = $copyError.GetAwaiter().GetResult()
        if($child.ExitCode -ne 0) { throw "Engine publishing child failed ($($child.ExitCode)): $Executable" }
    } finally { $child.Dispose() }
}
function Remove-Owned([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    # 削除は今回所有するstaging本体か、その.Buildに限定する。
    if($resolved -cne $staging -and $resolved -cne $work) { throw 'Unsafe publishing cleanup target.' }
    if([IO.Path]::GetDirectoryName($staging) -cne $parent -or -not [IO.Path]::GetFileName($staging).StartsWith('.nexus-install-')) { throw 'Invalid staging ownership.' }
    if(Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
try {
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $msbuild = @(& $locator -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe')[0]
    if(-not $msbuild -or -not [IO.File]::Exists($msbuild)) { throw 'MSBuild with C++ v145 is required to publish hosts.' }
    # 発行に必要なlibraryとLauncherだけを個別に更新する。SolutionやGameはビルドしない。
    foreach($buildTarget in @('NexusRuntime','NexusEditorSupport','NexusLauncher')) {
        Invoke-Child $msbuild @((Join-Path $sdkSource "Build/$buildTarget.vcxproj"),'/verbosity:minimal',"/p:Configuration=$configuration",'/p:Platform=x64') $sdkSource
    }
    # Engine実装cppとGame/SamplesはSDKへ含めない。公開範囲は既存include構造から段階的に絞る。
    foreach($directory in @('Foundation','Core','Runtime','Graphics','Platform','Editor','Tools','ThirdParty')) {
        Copy-Tree (Join-Path $sdkSource $directory) (Join-Path $sdk $directory) {
            param($item)
            # 旧一括生成headerはGame型を含むため、Sourceに残しても共有SDKへは公開しない。
            $relative = $item.FullName.Substring($sdkSource.Length).TrimStart('\','/').Replace('\','/')
            if($relative.StartsWith('Runtime/Reflection/Generated/',[StringComparison]::OrdinalIgnoreCase)) { return $false }
            $item.Extension -cin @('.h','.hpp','.inl') -or $item.Name -match '^(LICENSE|COPYING|UPSTREAM)'
        }
    }
    foreach($file in @('Build/Nexus.Common.props','Build/Nexus.Game.props','Build/Nexus.Compatibility.json','Tools/Reflection/Generate-Reflection.ps1','Tools/Project/Build-Game.ps1','Tools/Project/Write-GameBuildStamp.ps1','Tools/Project/Create-LauncherShortcut.ps1','Runtime/Reflection/EngineGenerated/ReflectionSchema.json')) { Copy-File (Join-Path $sdkSource $file) (Join-Path $sdk $file) }
    foreach($directory in @('Shaders')) {
        Copy-Tree (Join-Path $sdkSource $directory) (Join-Path $sdk $directory) { $true }
        Copy-Tree (Join-Path $sdkSource $directory) (Join-Path $staging "Data/$directory") { $true }
    }
    Copy-Tree (Join-Path $sdkSource 'Resources/Editor') (Join-Path $staging 'Data/Resources/Editor') { $true }
    Copy-Tree (Join-Path $sdkSource 'Resources/Assets/Fonts') (Join-Path $staging 'Data/Resources/Assets/Fonts') { $true }
    Copy-Tree (Within $sourceRoot $source.paths.templates) (Join-Path $staging 'Templates') { $true }
    foreach($library in @('NexusRuntime','NexusEditorSupport')) {
        Copy-File (Join-Path $sourceRoot "Generated/Modules/x64/$configuration/$library/$library.lib") (Join-Path $sdk "Lib/x64/$configuration/$library.lib")
    }
    foreach($dependency in $source.runtimeFiles) {
        $inputPath = Within $sourceRoot $dependency
        Copy-File $inputPath (Join-Path $bin ([IO.Path]::GetFileName($inputPath)))
    }
    Copy-File (Join-Path $sourceRoot "Generated/Modules/x64/$configuration/NexusLauncher/NexusLauncher.exe") (Join-Path $bin 'NexusLauncher.exe')
    $identity = '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><NexusHostEngineId Condition="''$(NexusHostEngineId)''==''''">' + (Xml $EngineId) + '</NexusHostEngineId><NexusHostEngineVersion Condition="''$(NexusHostEngineVersion)''==''''">' + (Xml $Version) + '</NexusHostEngineVersion><NexusHostEngineAssociation>installed</NexusHostEngineAssociation></PropertyGroup><ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>NEXUS_HOST_ENGINE_ID=&quot;$(NexusHostEngineId)&quot;;NEXUS_HOST_ENGINE_VERSION=&quot;$(NexusHostEngineVersion)&quot;;NEXUS_HOST_ENGINE_ASSOCIATION=&quot;installed&quot;;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup></Project>'
    [IO.File]::WriteAllText((Join-Path $sdk 'Build/Nexus.EngineIdentity.props'),$identity,$utf8)
    [IO.Directory]::CreateDirectory($work) | Out-Null
    foreach($hostKind in @('Runtime','Editor')) {
        $targetName = if($hostKind -ceq 'Editor') { 'NexusEditor' } else { 'NexusRuntimeHost' }
        $definitions = if($hostKind -ceq 'Runtime') { 'NEXUS_GAME_HOST;%(PreprocessorDefinitions)' } else { '%(PreprocessorDefinitions)' }
        $withEditor = if($hostKind -ceq 'Editor') { 'true' } else { 'false' }
        [IO.File]::WriteAllText((Join-Path $work "$targetName.cpp"),'#include "Core/Hosting/ProjectHost.inl"',$utf8)
        $project = @'
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
 <ItemGroup Label="ProjectConfigurations"><ProjectConfiguration Include="@CONFIG@|x64"><Configuration>@CONFIG@</Configuration><Platform>x64</Platform></ProjectConfiguration></ItemGroup>
 <PropertyGroup Label="Globals"><ProjectGuid>{@GUID@}</ProjectGuid><WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion></PropertyGroup>
 <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
 <PropertyGroup Label="Configuration"><ConfigurationType>Application</ConfigurationType><PlatformToolset>v145</PlatformToolset><CharacterSet>Unicode</CharacterSet></PropertyGroup>
 <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
 <PropertyGroup><NexusEngineRoot>@SDK@</NexusEngineRoot><NexusEngineAssociation>installed</NexusEngineAssociation><NexusWithEditor>@EDITOR@</NexusWithEditor></PropertyGroup>
 <Import Project="$(NexusEngineRoot)\Build\Nexus.Game.props" />
 <PropertyGroup><OutDir>@BIN@\</OutDir><IntDir>@WORK@\@TARGET@\</IntDir></PropertyGroup>
 <ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>@DEFINITIONS@</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>
 <ItemGroup><ClCompile Include="@TARGET@.cpp" /></ItemGroup>
 <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
'@
        foreach($pair in @(@('@CONFIG@',$configuration),@('@GUID@',[guid]::NewGuid().ToString()),@('@SDK@',(Xml $sdk)),@('@EDITOR@',$withEditor),@('@BIN@',(Xml $bin)),@('@WORK@',(Xml $work)),@('@TARGET@',$targetName),@('@DEFINITIONS@',$definitions))) { $project = $project.Replace($pair[0],$pair[1]) }
        $target = Join-Path $work "$targetName.vcxproj"; [IO.File]::WriteAllText($target,$project,$utf8)
        Invoke-Child $msbuild @($target,'/verbosity:minimal',"/p:Configuration=$configuration",'/p:Platform=x64') $work
    }
    # 配置内のSDKだけでホストがリンク・起動できることを公開前に確認する。
    $smoke = Join-Path $work 'Smoke'
    foreach($directory in @('Assets','Levels','Source','Config')) { [IO.Directory]::CreateDirectory((Join-Path $smoke $directory)) | Out-Null }
    Copy-File (Join-Path $staging 'Templates/Blank/Startup.level') (Join-Path $smoke 'Levels/Startup.level')
    $descriptor = [ordered]@{schemaVersion=1;projectId=[guid]::NewGuid().ToString();name='Installed smoke';projectVersion='0.1.0';engine=@{association='installed';id=$EngineId;version=$Version};paths=@{assets='Assets';levels='Levels';source='Source';config='Config'};startupLevel='Levels/Startup.level'}
    $smokeFile = Join-Path $smoke 'Smoke.nxproject'; [IO.File]::WriteAllText($smokeFile,($descriptor | ConvertTo-Json -Depth 8),$utf8)
    foreach($hostName in @('NexusRuntimeHost','NexusEditor')) { Invoke-Child (Join-Path $bin "$hostName.exe") @('--game','--project',$smokeFile,'--engine',(Join-Path $staging 'Data'),'--check-game') $smoke }
    Remove-Owned $work
    $inventory = [ordered]@{}
    foreach($file in Get-ChildItem -LiteralPath $staging -Recurse -File | Sort-Object FullName) {
        $relative = $file.FullName.Substring($staging.Length).TrimStart('\','/').Replace('\','/')
        $inventory[$relative] = Digest $file.FullName
    }
    $manifest = [ordered]@{schemaVersion=1;association='installed';id=$EngineId;version=$Version;configuration=$configuration;buildId=[guid]::NewGuid().ToString();toolset='v145';runtimeLibrary=$source.runtimeLibrary;paths=@{sdk='SDK';data='Data';templates='Templates';editor='Bin/NexusEditor.exe';runtime='Bin/NexusRuntimeHost.exe'};runtimeFiles=@('Bin/dxcompiler.dll','Bin/dxil.dll');inventory=$inventory}
    [IO.File]::WriteAllText((Join-Path $staging 'NexusEngine.nxengine'),($manifest | ConvertTo-Json -Depth 8),$utf8)
    # 既存版を置換しない。同じvolumeの完成したDirectoryだけを公開する。
    [IO.Directory]::Move($staging,$destinationPath)
    Write-Output (Join-Path $destinationPath 'NexusEngine.nxengine')
} finally { Remove-Owned $staging }
