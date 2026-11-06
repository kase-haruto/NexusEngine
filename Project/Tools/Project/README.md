# Launcher のショートカット作成

`Create-LauncherShortcut.cmd` をダブルクリックすると、デスクトップに `NexusEngine.lnk` を作成します。PowerShell の実行ポリシーはこの起動プロセスだけに適用し、システム設定は変更しません。失敗時はエラーを表示したまま停止します。

PowerShell から実行する場合:

```powershell
.\Create-LauncherShortcut.cmd
```

既定ではリポジトリ内の Debug 版 NexusLauncher を使用します。未ビルドの場合は、エラーに表示されるコマンドを Visual Studio Developer PowerShell で実行してから再試行してください。

別の構成、インストール済み Launcher、作成先も指定できます。

```powershell
.\Create-LauncherShortcut.cmd -Configuration Release
.\Create-LauncherShortcut.cmd -Launcher 'D:\NexusEngines\Stable\Bin\NexusLauncher.exe' -Destination 'C:\Users\MyUser\Desktop\NexusEngine Stable.lnk'
```

同じ Launcher・作業フォルダーを指し、引数がないショートカットが既にある場合は、作成済みとして正常終了します。既存ファイルは上書きしません。異なるリンクがある場合は、別名の `-Destination` を指定するか、既存のショートカットを手動で削除してから再実行してください。
