# 独立Git管理とProject / SDKの移動

## 共有するものとマシン固有情報

EngineとGameは別のGitリポジトリとして管理する。GameにはSource、Assets、Levels、Config、`.nxproject`、Game自身のビルド定義を記録する。DescriptorのEngine要求はassociation/id/versionだけであり、Engine SourceやSDKの絶対パスを含めない。

各マシンのEngine配置はEngine Registryへ登録する。Gameの`Nexus.Engine.local.props`とbuild stampはビルド時に生成し、Gitへ含めない。Build、Intermediate、Cache等を共有データ名に使っている場合は、そのDirectoryを一括除外しない。通常のテンプレートは標準配置を使う。

Source EngineをGameへコピーする方法は採用していない。submoduleならcommitを固定できるが、全GameでソースcheckoutとEngineビルドを持つ負担がある。CMake packageは現在のMSBuild構造の全面移行になるため導入せず、DevelopmentのProjectReferenceとInstalledの固定SDKリンクを選んだ。Source Control GUIやProvider interfaceは必要になった段階で追加する。

## 空DirectoryとGit clone

Gitは空Directoryを保存しない。新規Project作成時は、初期化後も空の必須Directoryへ`.gitkeep`を配置する。Blank ProjectのAssetsや、データ専用ProjectのSource/Configも、Gitに記録したファイルだけから復元できる。MarkerはSourceのGameコードとして扱わず、Asset Browserにも表示しない。

この処理は新規作成時だけ行う。既存Projectを開いた際に勝手にMarkerや設定を追加しない。古いProjectで空DirectoryがGitにない場合は、そのDirectoryとMarkerを明示的にリポジトリへ追加する。

cloneしたマシンでは要求Engineを登録してLauncherからOpenする。CLIなら次のように明示する。

```powershell
$tool = './Generated/Modules/x64/Debug/NexusProjectTool/NexusProjectTool.exe'
& $tool engine-register 'D:/Engines/0.1.6/NexusEngine.nxengine'
& $tool engine-default 'D:/Engines/0.1.6/NexusEngine.nxengine'
```

既定値は新規Project用であり、既存Projectの版を置換しない。Compiler toolset・configuration・CRTが一致するSDKを使う。Game Sourceの型を持つGameEditor/GameRuntimeをビルドする。

## SDKを移動した場合

EditorとRuntimeを終了してからSDK Directoryを移動する。Manifest内のパスは相対なのでSDK自体を書き換える必要はないが、マシンRegistryの絶対パスは明示更新する。同じidentity/configurationを別配置へ重複登録しない。

```powershell
& $tool engine-remove 'D:/OldEngine/NexusEngine.nxengine'
& $tool engine-register 'E:/NewEngine/NexusEngine.nxengine'
```

Removeは登録だけを削除し、Engineのファイルを削除しない。欠落した配置も削除できる。CLIでは末尾に専用Registryファイルを渡せる。LauncherのEngine Settingsにも同じ操作がある。

移動前の登録で解決できないときはエラーにし、たとえ別のEngineが既定値でも代用しない。再登録後はLauncherのOpenやCLI build/checkでlocal propsを新配置へ再バインドする。Project DescriptorとAsset/Level参照は変更しない。

## Gameの移動とclone

Root全体を移動すれば、Descriptor相対のSource/Assets/Levels/Configは新Rootで解決する。Recentへの再登録は欠損した旧パスを置き換える。元のworking copyも存在するcloneなら両方を一覧に残し、同じProject UUIDを理由に一方を削除しない。

Git cloneではBuild/Intermediate/local propsを引き継がない。要求Engineを明示登録して再ビルドする。EditorからRuntimeへ渡すDescriptorとData Rootも新しい絶対パスに解決され、CWDへ依存しない。

## 検証

- `ProjectRelocation.Tests.ps1`：専用SDKコピーとGameを移動し、旧登録の失敗、別版fallback拒否、明示再登録、Game移動、成果物を含まないclone相当、再バインド、日本語Shader Rootからの3 GPU frameを確認。
- `ProjectGitClone.Tests.ps1`：専用GameでGit index/treeを作成し、commitやユーザーidentity設定なしでarchiveを展開。Gitが実際に記録したファイルだけで必須Directoryを復元し、Data ProjectとC++ Projectの両方を開いて起動する。成果物・local propsが追跡されないことも確認。

2026-11-01の記録：移動は`Generated/RelocationValidation/fc4a2513-5430-47f9-ba8a-e8cc201166a1`、Git tree往復は`Generated/GitCloneValidation/3e126420-050f-498b-8179-5c5df2af4d2a`。既存のGame/SDKやSourceリポジトリの`.git`を移動・削除・commitしていない。
