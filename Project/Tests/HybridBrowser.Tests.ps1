param(
 [Parameter(Mandatory=$true)][string]$EngineRegistry,
 [ValidateSet('Debug','Develop','Release')][string]$Configuration='Debug'
)
# 実Browserの作成UIを、専用Registryと新規出力で検証する。Build / Game起動や既定Registry変更は行わない。
# golden座標は1120x760 / 125% DPI用。条件外では操作前に中断し、未検証を成功扱いしない。
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$engineRegistryFile=[IO.Path]::GetFullPath($EngineRegistry)
$launcherFile=Join-Path $workspace ('Generated/Modules/x64/'+$Configuration+'/NexusLauncher/NexusLauncher.exe')
if(-not [IO.File]::Exists($engineRegistryFile) -or -not [IO.File]::Exists($launcherFile)) {throw 'Build the selected Launcher and provide an explicit test Engine registry.'}
$runRoot=Join-Path $workspace ('Generated/HybridVerification/BrowserCreation/'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($runRoot) | Out-Null
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
/*-----------------------------------------------------------------------------------------
 * HybridBrowserInput
 * - 非表示BrowserのWin32入力と終了を制御する試験用adapter
 * - UI HWNDだけをProcessの存続中に借用する。Game / ECS / Managed Runtimeを所有しない
 * - 座標送信中だけ呼出ThreadのDPI contextを変更し、必ず元へ戻す
 *---------------------------------------------------------------------------------------*/
public static class HybridBrowserInput {
 delegate bool EnumCallback(IntPtr window,IntPtr data);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumCallback callback,IntPtr data);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr window,StringBuilder text,int capacity);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr window,StringBuilder text,int capacity);
 [DllImport("user32.dll",SetLastError=true)] public static extern bool PostMessageW(IntPtr window,uint message,IntPtr wParam,IntPtr lParam);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
 [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 // EnumWindowsは同期呼出し。delegateをNative側へ保存せず、この探索中だけ借用させる。
 public static IntPtr Find(uint process) { IntPtr found=IntPtr.Zero; EnumWindows((window,data)=> { uint id; GetWindowThreadProcessId(window,out id); var text=new StringBuilder(256); GetClassNameW(window,text,text.Capacity); if(id==process && text.ToString()=="NexusEngine.ProjectWindow") {found=window;return false;} return true;},IntPtr.Zero); return found; }
 public static bool Ready(IntPtr window) {var text=new StringBuilder(256);GetWindowTextW(window,text,text.Capacity);return text.ToString().EndsWith(" - Ready");}
 public static void Mouse(IntPtr window,int x,int y,uint message) {
  // DPI非対応のPowerShellから送るとWindowsが座標を再変換するため、受信Windowと同じ物理座標に揃える。
  var previous=SetThreadDpiAwarenessContext(new IntPtr(-4));
  if(previous==IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
  try {if(!PostMessageW(window,message,message==0x201?new IntPtr(1):IntPtr.Zero,new IntPtr((y<<16)|(x&65535)))) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());}
  finally {if(previous!=IntPtr.Zero) SetThreadDpiAwarenessContext(previous);}
 }
 public static void Text(IntPtr window,string value) {foreach(char ch in value) if(!PostMessageW(window,0x102,new IntPtr(ch),new IntPtr(1))) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());}
 public static void Key(IntPtr window,uint key,bool down) {if(!PostMessageW(window,down?0x100u:0x101u,new IntPtr(key),new IntPtr(down?1:0xC0000001L))) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());}
}
'@
function Wait-For([scriptblock]$Predicate,[string]$Description) {
 # Ready通知まで待ち、非同期workerの完了前にWindowを破棄しない。
 $deadline=[DateTime]::UtcNow.AddSeconds(15)
 while(-not (& $Predicate)) {if([DateTime]::UtcNow -gt $deadline) {throw ('Timed out: '+$Description)};Start-Sleep -Milliseconds 100}
}
function Click([IntPtr]$Window,[int]$X,[int]$Y) {
 [HybridBrowserInput]::Mouse($Window,$X,$Y,0x200);Start-Sleep -Milliseconds 80
 [HybridBrowserInput]::Mouse($Window,$X,$Y,0x201);Start-Sleep -Milliseconds 180
 [HybridBrowserInput]::Mouse($Window,$X,$Y,0x202);Start-Sleep -Milliseconds 300
}
function Scroll-Bottom([IntPtr]$Window,[int]$X,[int]$Y) {
 [HybridBrowserInput]::Mouse($Window,$X,$Y,0x200);Start-Sleep -Milliseconds 250
 $null=[HybridBrowserInput]::PostMessageW($Window,0x20a,[IntPtr](-30*120*65536),[IntPtr]::Zero)
 Start-Sleep -Milliseconds 400
}
foreach($language in @('Cpp','CSharp')) {
 # 毎回新しいdirectoryを使い、前回の成功物や利用者のSourceへ書き込まない。
 $caseRoot=Join-Path $runRoot $language; [IO.Directory]::CreateDirectory($caseRoot) | Out-Null
 $name='Browser'+$language; $file=Join-Path $caseRoot ($name+'/'+$name+'.nxproject'); $recent=Join-Path $caseRoot 'Recent.json'
 $arguments=@('--engine-registry',$engineRegistryFile,'--recent-registry',$recent,'--data',(Join-Path $workspace 'Project'),'--page','new','--smoke',(Join-Path $caseRoot 'Browser.png'),'--smoke-frames','500')
 $start=[Diagnostics.ProcessStartInfo]::new($launcherFile)
 $start.UseShellExecute=$false; $start.CreateNoWindow=$true; $start.WorkingDirectory=$workspace
 $start.RedirectStandardError=$true
 $start.Arguments=($arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
 $start.EnvironmentVariables['LOCALAPPDATA']=Join-Path $caseRoot 'User'
 $browserProcess=[Diagnostics.Process]::Start($start)
 $diagnosticStream=[IO.File]::OpenWrite((Join-Path $caseRoot 'Input.log'))
 $diagnosticCopy=$browserProcess.StandardError.BaseStream.CopyToAsync($diagnosticStream)
 $caseFailed=$true
 try {
  Wait-For { [HybridBrowserInput]::Find($browserProcess.Id) -ne [IntPtr]::Zero } 'Browser window'
  $browserWindow=[HybridBrowserInput]::Find($browserProcess.Id)
  if([HybridBrowserInput]::GetDpiForWindow($browserWindow) -ne 120 -or [HybridBrowserInput]::IsWindowVisible($browserWindow)) {throw 'This captured-coordinate probe requires hidden 125% DPI.'}
  Wait-For { [HybridBrowserInput]::Ready($browserWindow) } 'Loaded templates'
  $null=[HybridBrowserInput]::PostMessageW($browserWindow,0x7,[IntPtr]::Zero,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 300
  Click $browserWindow 840 230; [HybridBrowserInput]::Text($browserWindow,$name)
  Click $browserWindow 840 310; [HybridBrowserInput]::Text($browserWindow,$caseRoot)
  Scroll-Bottom $browserWindow 450 500
  Click $browserWindow 450 $(if($language -eq 'Cpp') {555} else {313})
  if($language -eq 'CSharp') {
   # popup行のpixel位置へ依存せず、表示されたComboをキーボードで選択する。
   Click $browserWindow 840 422
   [HybridBrowserInput]::Key($browserWindow,0x28,$true);Start-Sleep -Milliseconds 180
   [HybridBrowserInput]::Key($browserWindow,0x28,$false);Start-Sleep -Milliseconds 180
   [HybridBrowserInput]::Key($browserWindow,0x0d,$true);Start-Sleep -Milliseconds 180
   [HybridBrowserInput]::Key($browserWindow,0x0d,$false);Start-Sleep -Milliseconds 180
  }
  Scroll-Bottom $browserWindow 1000 680
  Click $browserWindow 840 645
  Wait-For {Test-Path -LiteralPath $file} ('Created '+$language+' Project')
  $descriptor=Get-Content -Raw -Encoding UTF8 -LiteralPath $file | ConvertFrom-Json
  # 見た目だけで成功とせず、UIが生成した共通DescriptorとManaged Projectを照合する。
  if($descriptor.modules.Count -ne 1 -or $descriptor.modules[0].language -cne $language) {throw 'UI selection did not produce the requested initial Module.'}
  if($language -eq 'CSharp' -and -not (Test-Path -LiteralPath (Join-Path (Split-Path $file) 'GameScripts.csproj'))) {throw 'Managed Gameplay was not selected.'}
  Wait-For { [HybridBrowserInput]::Ready($browserWindow) -and (Test-Path -LiteralPath $recent) } 'Registry update'
  Write-Output ('PASS: actual Browser creation '+$language+' -> '+$file)
  $caseFailed=$false
 } finally {
  # 自分が起動したProcessのWindowだけを正常終了させる。他のLauncher / Editorは停止しない。
  if($caseFailed -and -not $browserProcess.HasExited) {$null=$browserProcess.WaitForExit(45000)}
  if(-not $browserProcess.HasExited) {$null=[HybridBrowserInput]::PostMessageW([HybridBrowserInput]::Find($browserProcess.Id),0x10,[IntPtr]::Zero,[IntPtr]::Zero)}
  if(-not $browserProcess.WaitForExit(15000)) {throw 'Browser did not stop normally.'}
  $null=$diagnosticCopy.GetAwaiter().GetResult(); $diagnosticStream.Dispose()
  if($browserProcess.ExitCode -ne 0) {throw ('Browser exited '+$browserProcess.ExitCode)}
  $browserProcess.Dispose()
 }
}
