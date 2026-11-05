$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = Join-Path $workspace ('Generated/LauncherShortcutValidation/' + [guid]::NewGuid().ToString())
$binary = Join-Path $root '日本語 Engine Folder/NexusLauncher.exe'
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($binary)) | Out-Null
[IO.File]::WriteAllText($binary,'shortcut target fixture; never executed')
$destination = Join-Path $root '日本語 NexusEngine.lnk'
$generator = Join-Path $workspace 'Project/Tools/Project/Create-LauncherShortcut.ps1'
& $generator -Launcher $binary -Destination $destination
$shell = New-Object -ComObject WScript.Shell
$shortcut = $null
try {
    $shortcut = $shell.CreateShortcut($destination)
    if($shortcut.TargetPath -ine $binary) { throw 'Shortcut target changed.' }
    if($shortcut.WorkingDirectory -ine [IO.Path]::GetDirectoryName($binary)) { throw 'Shortcut working directory changed.' }
    if($shortcut.Arguments) { throw 'Project Browser shortcut must not select a game implicitly.' }
    $before = [Convert]::ToBase64String([IO.File]::ReadAllBytes($destination))
    $rejected = $false
    try { & $generator -Launcher $binary -Destination $destination } catch { $rejected = $true }
    if(-not $rejected) { throw 'Existing shortcut was overwritten.' }
    if([Convert]::ToBase64String([IO.File]::ReadAllBytes($destination)) -cne $before) { throw 'Existing shortcut bytes changed.' }
} finally {
    if($shortcut) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut) }
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
}
Write-Output "PASS: Unicode launcher shortcut target/CWD, no implicit Project, and existing shortcut preservation. Fixtures: $root"
