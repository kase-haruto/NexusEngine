param(
    [ValidateSet('Debug', 'Develop', 'Release')][string]$Configuration = 'Debug',
    [string]$Project = 'Project/NexusEngine.vcxproj',
    [string]$MSBuildPath
)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $MSBuildPath) {
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $MSBuildPath = @(& $locator -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe')[0]
}
if (-not $MSBuildPath -or -not (Test-Path -LiteralPath $MSBuildPath)) { throw 'MSBuild was not found. Supply -MSBuildPath.' }
$projectPath = (Resolve-Path (Join-Path $workspace $Project)).Path
$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = $MSBuildPath
$start.WorkingDirectory = $workspace
$start.UseShellExecute = $false
# Some tool hosts provide both Path and PATH; Framework MSBuild treats those names as
# the same key and fails before CL starts. Normalize the child environment only.
$buildEnvironment = @{}
foreach ($entry in $start.Environment.GetEnumerator()) { $buildEnvironment[$entry.Key] = $entry.Value }
$start.Environment.Clear()
foreach ($key in $buildEnvironment.Keys) { $start.Environment[$key] = $buildEnvironment[$key] }
$start.ArgumentList.Add($projectPath)
$start.ArgumentList.Add("/p:Configuration=$Configuration")
$start.ArgumentList.Add('/p:Platform=x64')
$start.ArgumentList.Add("/p:SolutionDir=$workspace\Project\")
$start.ArgumentList.Add('/m')
$start.ArgumentList.Add('/verbosity:quiet')
$process = [System.Diagnostics.Process]::Start($start)
$process.WaitForExit()
exit $process.ExitCode
