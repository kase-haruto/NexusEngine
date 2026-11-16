$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = Join-Path $workspace ('Generated/InstalledMonoTests/' + [guid]::NewGuid().ToString('N'))
$sdk = Join-Path $root 'Installed SDK'
$mono = Join-Path $sdk 'ThirdParty/Mono/Runtime'
$deploy = Join-Path $workspace 'Project/Tools/Project/Deploy-MonoRuntime.ps1'
$generator = Join-Path $workspace 'Project/Tools/Project/New-EngineRelease.ps1'
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Fixture([string]$Path, [string]$Text = 'fixture') {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    [IO.File]::WriteAllText($Path,$Text,$utf8)
}
function Expect-Failure([scriptblock]$Action,[string]$Message) {
    try { & $Action } catch {
        if($_.Exception.Message -notlike "*$Message*") { throw }
        return
    }
    throw "Expected failure: $Message"
}
$required = @('bin/mono.exe','bin/mono-2.0-sgen.dll','lib/mono/4.5/mscorlib.dll','etc/mono/config','lib/mono/gac/System/4.0.0.0__fixture/System.dll')
foreach($relative in $required) { Write-Fixture (Join-Path $mono $relative) $relative }
& $deploy -MonoRoot $mono -ValidateOnly
$output = Join-Path $root 'Fresh Game/Build/Bin/x64/Release/Mono'
& $deploy -MonoRoot $mono -OutputDirectory $output
foreach($relative in $required) {
    if([IO.File]::ReadAllText((Join-Path $output $relative)) -cne $relative) { throw "Missing deployed file: $relative" }
}
if(-not [IO.File]::Exists((Join-Path $output 'Notices/LICENSE.txt'))) { throw 'Deployed runtime lacks its license.' }
# Same-source deployment must remain harmless; validation must not require output.
& $deploy -MonoRoot $output -OutputDirectory $output
Expect-Failure { & $deploy -MonoRoot (Join-Path $root 'Missing') -ValidateOnly } 'Incomplete Mono SDK'
Expect-Failure { & $deploy -MonoRoot $mono } 'OutputDirectory is required'
$incomplete = Join-Path $root 'Incomplete SDK'
foreach($relative in $required[0..3]) { Write-Fixture (Join-Path $incomplete $relative) }
Expect-Failure { & $deploy -MonoRoot $incomplete -ValidateOnly } 'lib/mono/gac'

# Publishing fails before native builds or staging when the author lacks Mono.
$source = Join-Path $root 'Development'
$sourceManifest = Join-Path $source 'Development.nxengine'
Write-Fixture $sourceManifest '{"schemaVersion":1,"association":"development","toolset":"v145","configuration":"Release","paths":{"sdk":"SDK"}}'
Write-Fixture (Join-Path $source 'SDK/Tools/Project/Deploy-MonoRuntime.ps1') ([IO.File]::ReadAllText($deploy))
$publish = Join-Path $workspace 'Project/Tools/Engine/Publish-Engine.ps1'
$published = Join-Path $root 'Published'
Expect-Failure { & $publish -SourceManifest $sourceManifest -Destination $published -EngineId nexus -Version 1.2.1 -MonoRoot (Join-Path $root 'Absent SDK') } 'Publishing requires the official Mono x64 SDK'
Expect-Failure { & $publish -SourceManifest $sourceManifest -Destination $published -EngineId nexus -Version 1.2.1 -MonoRoot $incomplete } 'lib/mono/gac'
if(Test-Path -LiteralPath $published) { throw 'Invalid publishing created output.' }

# Evaluate the actual MSBuild import with no inherited machine-specific SDK path.
$locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = @(& $locator -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe')[0]
$probe = Join-Path $root 'Probe.proj'
$targets = [Security.SecurityElement]::Escape((Join-Path $workspace 'Project/Build/Nexus.ManagedBootstrap.targets'))
Write-Fixture $probe ('<Project><Import Project="'+$targets+'" /></Project>')
function Resolve-Mono([string]$EngineSdk,[string]$GameOutput,[string[]]$Extra = @()) {
    $result = & $msbuild $probe -nologo -getProperty:NexusMonoRoot "-p:NexusEngineRoot=$EngineSdk" "-p:OutDir=$GameOutput/" '-p:NEXUS_MONO_ROOT=' @Extra
    if($LASTEXITCODE -ne 0) { throw 'MSBuild property probe failed.' }
    return ($result | Out-String).Trim().Replace('\','/')
}
$expected = $mono.Replace('\','/')
if((Resolve-Mono $sdk (Join-Path $root 'Empty Game')) -cne $expected) { throw 'Fresh build did not resolve bundled Mono.' }
if((Resolve-Mono $sdk 'unused' @('-p:NexusMonoRoot=C:/Explicit SDK')) -cne 'C:/Explicit SDK') { throw 'Explicit override lost precedence.' }
if((Resolve-Mono $sdk 'unused' @('-p:NEXUS_MONO_ROOT=C:/Environment SDK')) -cne 'C:/Environment SDK') { throw 'Environment override lost precedence.' }
if((Resolve-Mono (Join-Path $root 'Older SDK') ([IO.Path]::GetDirectoryName($output))) -cne $output.Replace('\','/')) { throw 'Existing output fallback failed.' }

# Release packaging rejects the historical incomplete layout before writing a ZIP.
$engine = Join-Path $root 'Engine'
Write-Fixture (Join-Path $engine 'NexusEngine.nxengine') '{"association":"installed","id":"nexus","version":"1.2.1","configuration":"Release"}'
$helper = Join-Path $engine 'SDK/Tools/Project/Deploy-MonoRuntime.ps1'
Write-Fixture (Join-Path $engine 'SDK/Build/Nexus.ManagedBootstrap.targets') ([IO.File]::ReadAllText((Join-Path $workspace 'Project/Build/Nexus.ManagedBootstrap.targets')))
$notes = Join-Path $root 'changes.txt'
Write-Fixture $notes 'Bundle Mono for new projects.'
$package = Join-Path $root 'Packages'
Expect-Failure { & $generator -EngineDirectory $engine -ChangesFile $notes -OutputDirectory $package } 'Mono deployment helper is missing'
Write-Fixture $helper ([IO.File]::ReadAllText($deploy))
Expect-Failure { & $generator -EngineDirectory $engine -ChangesFile $notes -OutputDirectory $package } 'Incomplete Mono SDK'
if(Test-Path -LiteralPath $package) { throw 'Invalid release created output.' }
& $deploy -MonoRoot $mono -OutputDirectory (Join-Path $engine 'SDK/ThirdParty/Mono/Runtime')
$null = & $generator -EngineDirectory $engine -ChangesFile $notes -OutputDirectory $package
$zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $package 'nexus-1.2.1-Release.zip'))
try {
    foreach($relative in $required) {
        if(-not $zip.GetEntry(('SDK/ThirdParty/Mono/Runtime/'+$relative))) { throw "ZIP missing Mono file: $relative" }
    }
} finally { $zip.Dispose() }
Write-Output "PASS: bundled Mono resolution, overrides, deployment, notices and release validation. Artifacts: $root"
