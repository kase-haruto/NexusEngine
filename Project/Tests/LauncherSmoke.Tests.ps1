param(
    [ValidateSet('Debug','Develop','Release')][string]$Configuration = 'Debug',
    [string]$EngineRegistry,
    [string]$ProjectFile,
    [string]$ReviewProject,
    [string]$ReviewTarget,
    [int]$ClientWidth = 0,
    [int]$ClientHeight = 0,
    [string]$Executable
)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = Join-Path $workspace ('Generated/LauncherValidation/' + [guid]::NewGuid().ToString())
[IO.Directory]::CreateDirectory($root) | Out-Null
$recent = Join-Path $root 'Recent.json'
if(-not $EngineRegistry) { $EngineRegistry = Join-Path $root 'Engines.json' }
$EngineRegistry = [IO.Path]::GetFullPath($EngineRegistry)
if($ProjectFile) {
    & (Join-Path $workspace "Generated/Modules/x64/$Configuration/NexusProjectTool/NexusProjectTool.exe") register $ProjectFile $recent
    if($LASTEXITCODE -ne 0) { throw 'Cannot prepare recent-project fixture.' }
}
$exe = if($Executable) { [IO.Path]::GetFullPath($Executable) } else { Join-Path $workspace "Generated/Modules/x64/$Configuration/NexusLauncher/NexusLauncher.exe" }
if($ClientWidth -or $ClientHeight) {
    if($ClientWidth -lt 600 -or $ClientHeight -lt 420) { throw 'Resize validation needs at least a 600x420 client area.' }
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class NexusLauncherResize {
    private delegate bool Callback(IntPtr window,IntPtr data);
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left,Top,Right,Bottom; }
    [DllImport("user32.dll")] private static extern bool EnumWindows(Callback callback,IntPtr data);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern int GetClassNameW(IntPtr window,StringBuilder text,int length);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window,out Rect rect);
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr window,out Rect rect);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr window,IntPtr after,int x,int y,int width,int height,uint flags);
    [DllImport("user32.dll")] private static extern IntPtr GetWindowDpiAwarenessContext(IntPtr window);
    [DllImport("user32.dll")] private static extern bool AreDpiAwarenessContextsEqual(IntPtr left,IntPtr right);
    public static bool Resize(uint process,int width,int height) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window,data) => { uint id; GetWindowThreadProcessId(window,out id); if(id == process) { var text = new StringBuilder(256); GetClassNameW(window,text,text.Capacity); if(text.ToString() == "NexusEngine.ProjectWindow") { found = window; return false; } } return true; },IntPtr.Zero);
        if(found == IntPtr.Zero) return false;
        if(!AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(found),new IntPtr(-4))) throw new InvalidOperationException("Launcher is not Per Monitor V2 DPI aware.");
        Rect outer,client; if(!GetWindowRect(found,out outer) || !GetClientRect(found,out client)) throw new InvalidOperationException("Cannot query Launcher size.");
        if(!SetWindowPos(found,IntPtr.Zero,0,0,width+(outer.Right-outer.Left)-(client.Right-client.Left),height+(outer.Bottom-outer.Top)-(client.Bottom-client.Top),0x16)) throw new InvalidOperationException("Cannot resize Launcher.");
        return true;
    }
}
'@
}
$pages = @('recent','new','engines'); if($ReviewProject) { $pages += 'migration' }
foreach($page in $pages) {
    $capture = Join-Path $root ($page + '.png')
    $start = [Diagnostics.ProcessStartInfo]::new($exe)
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.RedirectStandardError = $true
    # この検証スクリプトはPowerShell 7で実行する。UIプロセス自体はPowerShellを必要としない。
    $startPage = if($page -eq 'migration') { 'recent' } else { $page }
    foreach($argument in @('--recent-registry',$recent,'--engine-registry',$EngineRegistry,'--page',$startPage,'--smoke',$capture)) { $start.ArgumentList.Add($argument) }
    if($ClientWidth) { $start.ArgumentList.Add('--smoke-frames'); $start.ArgumentList.Add('45') }
    if($page -eq 'migration') {
        $start.ArgumentList.Add('--review-project'); $start.ArgumentList.Add([IO.Path]::GetFullPath($ReviewProject))
        if($ReviewTarget) { $start.ArgumentList.Add('--review-target'); $start.ArgumentList.Add([IO.Path]::GetFullPath($ReviewTarget)) }
    }
    $child = [Diagnostics.Process]::Start($start)
    try {
        $errorTask = $child.StandardError.ReadToEndAsync()
        if($ClientWidth) {
            $resizeDeadline = [DateTime]::UtcNow.AddSeconds(10)
            while(-not [NexusLauncherResize]::Resize($child.Id,$ClientWidth,$ClientHeight)) {
                if($child.HasExited -or [DateTime]::UtcNow -gt $resizeDeadline) { throw 'Launcher exited before resize or its window was not found.' }
                Start-Sleep -Milliseconds 20
            }
        }
        $deadline = [DateTime]::UtcNow.AddSeconds(70)
        while(-not $child.WaitForExit(1000)) { if([DateTime]::UtcNow -gt $deadline) { $child.Kill($true); throw "Launcher smoke timed out: $page" } }
        $errorText = $errorTask.GetAwaiter().GetResult()
        if($child.ExitCode -ne 0) { throw "Launcher failed ($($child.ExitCode)): $errorText" }
    } finally { $child.Dispose() }
    $bytes = [IO.File]::ReadAllBytes($capture)
    if($bytes.Length -lt 32 -or [BitConverter]::ToString($bytes[0..7]) -ne '89-50-4E-47-0D-0A-1A-0A') { throw 'Capture is not a PNG image.' }
    if($ClientWidth) {
        $width = ([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
        $height = ([int]$bytes[20] -shl 24) -bor ([int]$bytes[21] -shl 16) -bor ([int]$bytes[22] -shl 8) -bor [int]$bytes[23]
        if($width -ne $ClientWidth -or $height -ne $ClientHeight) { throw "Resize capture differs: $width x $height" }
    }
}
Write-Output "PASS: Launcher hidden rendering and shutdown on all three pages. Captures: $root"
