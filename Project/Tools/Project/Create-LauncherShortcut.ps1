param(
    [string]$Launcher,
    [string]$Destination,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
if(-not $Launcher) {
    $repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
    $Launcher = Join-Path $repository "Generated/Modules/x64/$Configuration/NexusLauncher/NexusLauncher.exe"
}
$Launcher = [IO.Path]::GetFullPath($Launcher)
if(-not [IO.File]::Exists($Launcher)) {
    $buildProject = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Build/NexusLauncher.vcxproj'))
    throw @"
Launcher executable does not exist: $Launcher
Build NexusLauncher first from a Visual Studio Developer PowerShell:
  msbuild "$buildProject" /m /t:Build /p:Configuration=$Configuration /p:Platform=x64
For an installed Launcher, specify -Launcher "C:/path/to/Bin/NexusLauncher.exe".
"@
}
if(-not $Destination) {
    $desktop = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory)
    if(-not $desktop) { throw 'Windows Desktop directory is unavailable. Specify -Destination.' }
    $Destination = Join-Path $desktop 'NexusEngine.lnk'
}
$Destination = [IO.Path]::GetFullPath($Destination)
if([IO.Path]::GetExtension($Destination) -ine '.lnk') { throw 'Shortcut destination must end in .lnk.' }
if([IO.Directory]::Exists($Destination)) { throw "Shortcut destination is a directory: $Destination" }
$parent = [IO.Path]::GetDirectoryName($Destination)
if(-not [IO.Directory]::Exists($parent)) { throw "Shortcut parent directory does not exist: $parent" }
$temporary = Join-Path $parent ('.nexus-shortcut-' + [guid]::NewGuid().ToString() + '.lnk')
$shell = $null; $shortcut = $null
try {
    # 引数なしのLauncherを入口にし、特定GameやEngine版の選択をshortcutへ埋め込まない。
    $shell = New-Object -ComObject WScript.Shell
    if([IO.File]::Exists($Destination)) {
        $shortcut = $shell.CreateShortcut($Destination)
        if($shortcut.TargetPath -ieq $Launcher -and
           $shortcut.WorkingDirectory -ieq [IO.Path]::GetDirectoryName($Launcher) -and
           [string]::IsNullOrEmpty($shortcut.Arguments)) {
            Write-Output "NexusEngine Project Browser shortcut is already configured: $Destination"
            return
        }
        throw "A different shortcut already exists: $Destination (target: $($shortcut.TargetPath)). Specify another -Destination or remove the old shortcut manually."
    }
    $shortcut = $shell.CreateShortcut($temporary)
    $shortcut.TargetPath = $Launcher
    $shortcut.WorkingDirectory = [IO.Path]::GetDirectoryName($Launcher)
    $shortcut.Description = 'NexusEngine - Project Browser'
    $shortcut.IconLocation = $Launcher + ',0'
    $shortcut.WindowStyle = 1
    $shortcut.Save()
    # 完成済みの自分の一時ファイルだけを公開し、既存のshortcutは置き換えない。
    [IO.File]::Move($temporary,$Destination)
    Write-Output "Created NexusEngine Project Browser shortcut: $Destination"
} finally {
    if($shortcut) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut) }
    if($shell) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
    if([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
}
