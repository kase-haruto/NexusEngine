param([ValidateSet('Start', 'Capture', 'Close')][string]$Action = 'Start')
$ErrorActionPreference = 'Stop'
$previewRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$previewExecutable = Join-Path $previewRoot 'Generated/Outputs/Debug/NexusEngine.exe'
$previewDirectory = Join-Path $previewRoot 'Generated/EditorPreview'
$previewStateFile = Join-Path $previewDirectory 'Process.json'
[IO.Directory]::CreateDirectory($previewDirectory) | Out-Null

if ($Action -eq 'Start') {
    if (-not (Test-Path -LiteralPath $previewExecutable)) { throw 'Build the Debug editor first.' }
    if (Test-Path -LiteralPath $previewStateFile) {
        $previousPreview = Get-Content -LiteralPath $previewStateFile -Raw | ConvertFrom-Json
        $existingPreview = Get-Process -Id $previousPreview.id -ErrorAction SilentlyContinue
        if ($existingPreview -and $existingPreview.Path -eq $previewExecutable -and
            $existingPreview.StartTime.ToUniversalTime() -eq ([datetime]$previousPreview.started).ToUniversalTime()) {
            throw 'The preview is already running. Use Capture or Close.'
        }
    }
    # 実利用者の設定を変更せず、preview専用の個人設定を使用する。
    $previousLocalData = $env:LOCALAPPDATA
    try {
        $env:LOCALAPPDATA = Join-Path $previewDirectory 'User'
        $previewProcess = Start-Process -FilePath $previewExecutable -WorkingDirectory (Split-Path $previewExecutable) `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $previewDirectory 'Startup.log') `
            -RedirectStandardError (Join-Path $previewDirectory 'Error.log')
    } finally { $env:LOCALAPPDATA = $previousLocalData }
    @{ id = $previewProcess.Id; started = $previewProcess.StartTime.ToUniversalTime().ToString('o') } |
        ConvertTo-Json | Set-Content -LiteralPath $previewStateFile -Encoding utf8
    "Started editor preview PID=$($previewProcess.Id)"
    exit 0
}

$previewState = Get-Content -LiteralPath $previewStateFile -Raw | ConvertFrom-Json
$previewProcess = Get-Process -Id $previewState.id -ErrorAction SilentlyContinue
if (-not $previewProcess) {
    'Preview has exited.'
    Get-Content -LiteralPath (Join-Path $previewDirectory 'Startup.log'), (Join-Path $previewDirectory 'Error.log') -ErrorAction SilentlyContinue | Select-Object -Last 30
    exit 0
}
if ($previewProcess.Path -ne $previewExecutable -or
    $previewProcess.StartTime.ToUniversalTime() -ne ([datetime]$previewState.started).ToUniversalTime()) {
    throw 'The recorded PID belongs to a different process.'
}
if ($Action -eq 'Close') {
    if (-not $previewProcess.CloseMainWindow()) { throw 'The preview has no main window.' }
    if ($previewProcess.WaitForExit(5000)) { "Preview exited with code $($previewProcess.ExitCode)." }
    else { 'Preview is still running; a save confirmation may be open.' }
    exit 0
}

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class EditorPreviewWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    public delegate bool EnumWindowCallback(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowCallback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, System.Text.StringBuilder name, int count);
    public static IntPtr Find(uint process) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window,data) => {
            uint owner; GetWindowThreadProcessId(window,out owner);
            var name = new System.Text.StringBuilder(128); GetClassName(window,name,128);
            if(owner == process && name.ToString() == "NexusEngine.ProjectWindow") { found = window; return false; }
            return true;
        },IntPtr.Zero);
        return found;
    }
}
'@
$previewHandle = [EditorPreviewWindow]::Find($previewProcess.Id)
if ($previewHandle -eq [IntPtr]::Zero) { throw 'The preview has no main window yet. Check its startup logs.' }
[EditorPreviewWindow]::ShowWindow($previewHandle, 5) | Out-Null
[EditorPreviewWindow]::SetForegroundWindow($previewHandle) | Out-Null
Start-Sleep -Milliseconds 500
$previewRect = [EditorPreviewWindow+Rect]::new()
if (-not [EditorPreviewWindow]::GetWindowRect($previewHandle, [ref]$previewRect)) { throw 'Cannot read preview window bounds.' }
$previewWidth = $previewRect.Right - $previewRect.Left
$previewHeight = $previewRect.Bottom - $previewRect.Top
if ($previewWidth -le 0 -or $previewHeight -le 0) { throw 'Preview window has invalid dimensions.' }
$previewBitmap = [Drawing.Bitmap]::new($previewWidth, $previewHeight)
$previewGraphics = [Drawing.Graphics]::FromImage($previewBitmap)
try {
    $previewGraphics.CopyFromScreen($previewRect.Left, $previewRect.Top, 0, 0, $previewBitmap.Size)
    $previewImage = Join-Path $previewDirectory 'Editor.png'
    $previewBitmap.Save($previewImage, [Drawing.Imaging.ImageFormat]::Png)
    "Captured $previewImage"
} finally { $previewGraphics.Dispose(); $previewBitmap.Dispose() }
