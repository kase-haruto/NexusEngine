param(
    [Parameter(Mandatory=$true)][string]$TargetManifest,
    [string]$SourceManifest,
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(-not $SourceManifest) { $SourceManifest = Join-Path $workspace "NexusEngine.$Configuration.nxengine" }
$SourceManifest = [IO.Path]::GetFullPath($SourceManifest); $TargetManifest = [IO.Path]::GetFullPath($TargetManifest)
$tool = Join-Path $workspace "Generated/Modules/x64/$Configuration/NexusProjectTool/NexusProjectTool.exe"
$launcher = Join-Path $workspace "Generated/Modules/x64/$Configuration/NexusLauncher/NexusLauncher.exe"
$root = Join-Path $workspace ('Generated/LauncherInteractionValidation/' + [guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($root) | Out-Null
$registry = Join-Path $root 'Engines.json'; $recent = Join-Path $root 'Recent.json'
foreach($manifest in @($SourceManifest,$TargetManifest)) { & $tool engine-register $manifest $registry; if($LASTEXITCODE -ne 0) { throw 'Cannot register test Engine.' } }
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class NexusLauncherInput {
    private delegate bool EnumCallback(IntPtr window,IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumCallback callback,IntPtr data);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window,uint message,IntPtr wParam,IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern int GetClassNameW(IntPtr window,StringBuilder text,int capacity);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern int GetWindowTextW(IntPtr window,StringBuilder text,int capacity);
    public static bool Ready(IntPtr window) { var text = new StringBuilder(256); GetWindowTextW(window,text,text.Capacity); return text.ToString().EndsWith(" - Ready"); }
    public static IntPtr Find(uint process) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window,data) => { uint id; GetWindowThreadProcessId(window,out id); var text = new StringBuilder(256); if(id == process) { GetClassNameW(window,text,text.Capacity); if(text.ToString() == "NexusEngine.ProjectWindow") { found = window; return false; } } return true; },IntPtr.Zero);
        return found;
    }
    public static void Mouse(IntPtr window,int x,int y,uint message) { PostMessageW(window,message,message == 0x201 ? new IntPtr(1) : IntPtr.Zero,new IntPtr((y << 16) | (x & 65535))); }
    public static void Text(IntPtr window,string value) { foreach(char ch in value) PostMessageW(window,0x102,new IntPtr(ch),new IntPtr(1)); }
    public static void Key(IntPtr window,uint key,bool down) { PostMessageW(window,down ? 0x100u : 0x101u,new IntPtr(key),new IntPtr(1)); }
}
'@
function Click([IntPtr]$Window,[int]$X,[int]$Y) {
    [NexusLauncherInput]::Mouse($Window,$X,$Y,0x200)
    [NexusLauncherInput]::Mouse($Window,$X,$Y,0x201)
    Start-Sleep -Milliseconds 80
    [NexusLauncherInput]::Mouse($Window,$X,$Y,0x202)
    Start-Sleep -Milliseconds 120
}
function Wait-Until([scriptblock]$Condition,[int]$Seconds = 20,[string]$Description = 'Launcher condition') {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while(-not (& $Condition)) { if([DateTime]::UtcNow -gt $deadline) { throw ("Timed out: " + $Description) }; Start-Sleep -Milliseconds 100 }
}
function Read-LiveLog([string]$File) {
    $stream = [IO.File]::Open($File,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader = [IO.StreamReader]::new($stream,[Text.Encoding]::UTF8)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}
function Start-Launcher([string]$Page,[string]$Capture,[string]$Review = '') {
    $start = [Diagnostics.ProcessStartInfo]::new($launcher); $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.RedirectStandardError = $true
    $start.Environment['LOCALAPPDATA'] = Join-Path $root 'User'
    foreach($argument in @('--recent-registry',$recent,'--engine-registry',$registry,'--page',$Page,'--smoke',$Capture,'--smoke-frames','1000')) { $start.ArgumentList.Add($argument) }
    if($Review) { foreach($argument in @('--review-project',$Review,'--review-target',$TargetManifest)) { $start.ArgumentList.Add($argument) } }
    return [Diagnostics.Process]::Start($start)
}
function Observe-Window($Child) {
    Wait-Until { [NexusLauncherInput]::Find($Child.Id) -ne [IntPtr]::Zero } -Description 'native ProjectWindow'
    $window = [NexusLauncherInput]::Find($Child.Id)
    if([NexusLauncherInput]::GetDpiForWindow($window) -ne 96) { throw 'This pixel-coordinate interaction test requires a 96 DPI test monitor.' }
    if([NexusLauncherInput]::IsWindowVisible($window)) { throw 'Interaction test unexpectedly displayed a window.' }
    Wait-Until { [NexusLauncherInput]::Ready($window) } -Description 'Launcher ready title'
    return $window
}
function Finish-Launcher($Child) {
    try {
        if(-not $Child.WaitForExit(45000)) { $null = [NexusLauncherInput]::PostMessageW([NexusLauncherInput]::Find($Child.Id),0x10,[IntPtr]::Zero,[IntPtr]::Zero); throw 'Launcher did not finish its interaction capture.' }
        $errorText = $Child.StandardError.ReadToEnd()
        if($Child.ExitCode -ne 0) { throw "Launcher exited $($Child.ExitCode): $errorText" }
    } finally { $Child.Dispose() }
}
$parent = Join-Path $root 'Games'; [IO.Directory]::CreateDirectory($parent) | Out-Null
$gameName = 'UI 日本語 Game'; $created = Join-Path (Join-Path $parent $gameName) ($gameName + '.nxproject')
$child = Start-Launcher 'new' (Join-Path $root 'Created.png')
try {
    $window = Observe-Window $child
    Click $window 800 184; [NexusLauncherInput]::Text($window,$gameName)
    [NexusLauncherInput]::Key($window,9,$true); Start-Sleep -Milliseconds 80
    [NexusLauncherInput]::Key($window,9,$false); Start-Sleep -Milliseconds 120
    [NexusLauncherInput]::Text($window,$parent)
    Start-Sleep -Milliseconds 200
    Click $window 850 477
    Write-Output 'Waiting for Project creation from Launcher input...'
    Wait-Until { Test-Path -LiteralPath $created } -Description 'new Project Descriptor'
    Wait-Until { (Test-Path -LiteralPath $recent) -and ((Get-Content -LiteralPath $recent -Raw | ConvertFrom-Json).projects.Count -eq 1) }
    Wait-Until { [NexusLauncherInput]::Ready($window) } -Description 'Recent view after creation'
    Click $window 420 181; [NexusLauncherInput]::Text($window,'日本語')
    Start-Sleep -Milliseconds 200
    Click $window 295 475
    Write-Output 'Waiting for the Editor opened from Recent to complete GPU frames...'
    Wait-Until {
        $logs = @(Get-ChildItem -LiteralPath (Join-Path (Join-Path $parent $gameName) 'Build/Logs') -Filter '*.editor.log' -ErrorAction SilentlyContinue)
        @($logs | Where-Object { (Read-LiveLog $_.FullName) -match 'PASS: completed 3 GPU frames and normal shutdown' }).Count -eq 1
    } -Seconds 45 -Description 'Editor launched by Recent Open'
} finally { Finish-Launcher $child }
$migrationGame = Join-Path $root 'Migration 日本語 Game'
& $tool create-template $migrationGame 'UI Migration' $SourceManifest blank
if($LASTEXITCODE -ne 0) { throw 'Cannot prepare migration UI fixture.' }
$file = Join-Path $migrationGame 'Migration 日本語 Game.nxproject'; $original = [IO.File]::ReadAllText($file)
$target = Get-Content -LiteralPath $TargetManifest -Raw | ConvertFrom-Json
$child = Start-Launcher 'recent' (Join-Path $root 'MigrationRestored.png') $file
try {
    $window = Observe-Window $child
    Wait-Until { [NexusLauncherInput]::Ready($window) }
    Click $window 170 413
    Click $window 280 442
    Write-Output 'Waiting for reviewed Engine application from Launcher input...'
    Wait-Until { (Get-Content -LiteralPath $file -Raw | ConvertFrom-Json).engine.id -ceq $target.id } -Description 'reviewed Engine applied'
    Wait-Until { [NexusLauncherInput]::Ready($window) }
    Click $window 510 288
    Write-Output 'Waiting for Descriptor recovery from Launcher input...'
    Wait-Until { [IO.File]::ReadAllText($file) -ceq $original } -Description 'original Descriptor restored'
} finally { Finish-Launcher $child }
Write-Output "PASS: actual hidden Launcher input created a Unicode Game with Tab navigation, searched Recent, opened its GPU Editor, explicitly applied an Engine review and restored exact Descriptor bytes. Captures: $root"
