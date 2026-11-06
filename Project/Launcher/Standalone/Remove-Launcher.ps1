param(
    [Parameter(Mandatory=$true)][string]$Root,
    [int]$ParentPid = 0,
    [string]$InstallationBase = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs/NexusEngineLauncher'),
    [string]$RegistryKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\NexusEngineLauncher',
    [string[]]$ShortcutDirectories = @([Environment]::GetFolderPath('DesktopDirectory'), (Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs')),
    [switch]$NoUI
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
function Canonical([string]$Path) { return [IO.Path]::GetFullPath($Path).TrimEnd('\','/') }
function No-Links([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while($current) {
        if((Test-Path -LiteralPath $current) -and ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "リンクを含む配置は削除できません: $current" }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}
function Within([string]$Relative) {
    if([string]::IsNullOrWhiteSpace($Relative) -or $Relative.Contains(':') -or [IO.Path]::IsPathRooted($Relative)) { throw '所有ファイルの相対パスが不正です。' }
    $path = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if(-not $path.StartsWith($Root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw '削除対象がインストール先の範囲外です。' }
    return $path
}
$opened = @()
$lifecycleLock = New-Object Threading.Mutex($false, 'Local\NexusEngine.LauncherLifecycle')
$locked = $false
try {
    $Root = Canonical $Root
    $installationId = [guid]::Empty
    if((Canonical ([IO.Path]::GetDirectoryName($Root))) -ine (Canonical $InstallationBase) -or -not [guid]::TryParseExact([IO.Path]::GetFileName($Root), 'N', [ref]$installationId)) { throw 'セットアップが作成した版別フォルダーだけを削除できます。' }
    No-Links $Root
    if($ParentPid -gt 0 -and (Get-Process -Id $ParentPid -ErrorAction SilentlyContinue)) { Wait-Process -Id $ParentPid -Timeout 60 }
    $locked = $lifecycleLock.WaitOne(0)
    if(-not $locked) { throw '別のインストールまたはアンインストールが実行中です。完了後に再試行してください。' }
    $marker = Join-Path $Root '.nexus-launcher-install.json'
    $receipt = Get-Content -LiteralPath $marker -Raw -Encoding UTF8 | ConvertFrom-Json
    if($receipt.schemaVersion -ne 1 -or $receipt.kind -cne 'launcher' -or (Canonical $receipt.root) -ine $Root -or $receipt.identity -cne [IO.Path]::GetFileName($Root)) { throw '削除対象とインストール記録が一致しません。' }
    $expected = @{}
    foreach($property in $receipt.files.PSObject.Properties) { $null = Within $property.Name; $expected[$property.Name] = [string]$property.Value }
    if(-not $expected.ContainsKey('NexusLauncher.exe') -or -not $expected.ContainsKey('launcher.json')) { throw 'インストール記録が不完全です。' }
    $expected['.nexus-launcher-install.json'] = (Get-FileHash -LiteralPath $marker -Algorithm SHA256).Hash
    $directories = @{}
    foreach($relative in $receipt.directories) { $directories[(Within $relative)] = $true }
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($Root)
    $files = @(); $foundDirectories = @()
    while($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach($item in Get-ChildItem -LiteralPath $directory -Force) {
            No-Links $item.FullName
            if($item.PSIsContainer) {
                if(-not $directories.ContainsKey($item.FullName)) { throw "追加されたフォルダーを保護しました: $($item.FullName)" }
                $foundDirectories += $item.FullName; $pending.Push($item.FullName)
            } else {
                $relative = $item.FullName.Substring($Root.Length + 1)
                if(-not $expected.ContainsKey($relative)) { throw "追加されたファイルを保護しました: $relative" }
                if($item.Attributes -band [IO.FileAttributes]::ReadOnly) { throw "読み取り専用ファイルがあります: $relative" }
                $files += $item.FullName
            }
        }
    }
    # 全ファイルの検証完了まで、新しい読み書きを防ぐロックを保持する。
    foreach($file in $files) {
        $stream = [IO.File]::Open($file, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Delete)
        $opened += $stream
        $hash = [Security.Cryptography.SHA256]::Create()
        try { $digest = [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','') } finally { $hash.Dispose() }
        if($digest -ine $expected[$file.Substring($Root.Length + 1)]) { throw "変更されたファイルを保護しました: $file" }
    }
    # メモリーへ配置済みのEXEも確認し、ほかの実行中プロセスを強制終了しない。
    foreach($process in Get-Process) {
        try { $executable = $process.Path } catch { continue }
        if($executable -and $executable.StartsWith($Root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "この配置のアプリケーションが実行中です: $executable" }
    }
    # 解決済み・検証済みのパスだけを削除し、インストール先を再帰削除しない。
    foreach($file in $files | Where-Object { $_ -ine $marker }) { No-Links $file; [IO.File]::Delete($file) }
    [IO.File]::Delete($marker)
    foreach($stream in $opened) { $stream.Dispose() }; $opened = @()
    foreach($directory in $foundDirectories | Sort-Object Length -Descending) { [IO.Directory]::Delete($directory, $false) }
    [IO.Directory]::Delete($Root, $false)
    $shell = New-Object -ComObject WScript.Shell
    try {
        foreach($directory in $ShortcutDirectories) {
            if(-not $directory) { continue }
            $link = Join-Path $directory 'NexusEngine Launcher.lnk'
            if(-not (Test-Path -LiteralPath $link)) { continue }
            $shortcut = $shell.CreateShortcut($link)
            try { $owned = $shortcut.TargetPath -and (Canonical $shortcut.TargetPath) -ieq (Join-Path $Root 'NexusLauncher.exe') } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut) }
            if($owned) { Remove-Item -LiteralPath $link }
        }
    } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($RegistryKey)
    if($key) {
        try { $location = [string]$key.GetValue('InstallLocation'); $ownedKey = $location -and (Canonical $location) -ieq $Root } finally { $key.Dispose() }
        if($ownedKey) { [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKey($RegistryKey, $false) }
    }
    if($NoUI) { Write-Output 'PASS: launcher installation removed' }
    else { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show('ランチャーをアンインストールしました。エンジンとプロジェクトは保持しています。', 'NexusEngine Launcher') }
} catch {
    if($NoUI) { Write-Error $_; exit 1 }
    Add-Type -AssemblyName System.Windows.Forms
    [void][Windows.Forms.MessageBox]::Show("アンインストールを完了できませんでした。`r`n$($_.Exception.Message)`r`n`r`n配置: $Root", 'NexusEngine Launcher')
    exit 1
} finally {
    foreach($stream in $opened) { $stream.Dispose() }
    if($locked) { $lifecycleLock.ReleaseMutex() }; $lifecycleLock.Dispose()
    # ランチャー自身が作成した一時ヘルパーだけを削除する。
    try {
        $jobBase = Join-Path ([IO.Path]::GetTempPath()) 'NexusLauncherRemoval'
        $jobId = [guid]::Empty
        if((Canonical ([IO.Path]::GetDirectoryName($PSScriptRoot))) -ieq (Canonical $jobBase) -and [guid]::TryParseExact([IO.Path]::GetFileName($PSScriptRoot), 'N', [ref]$jobId)) {
            No-Links $PSScriptRoot
            Remove-Item -LiteralPath $PSCommandPath
            [IO.Directory]::Delete($PSScriptRoot, $false)
        }
    } catch { }
}
