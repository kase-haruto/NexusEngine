# Content Browserのソース閲覧・アタッチ

Content BrowserのAssets / Source切替で、ProjectのSource directoryを閲覧できる。C# / C++ソースを右クリックしてテキストエディタで開き、登録済みのComponent / Script型を選択Entityへアタッチする。ファイルをDetailsのdrop領域またはHierarchyのEntity行へドラッグしても追加できる。

## 現在のサンプル処理

HybridSampleのStartup.levelでCubeに設定されているScript Type IDはgame.playerである。Source/Game/PlayerController.csがTargetをGame.CrowdSimulation.SetTargetへ渡し、Source/Game/CrowdSimulation.csが公開Native APIを呼び出す。移動計算はSource/NativeCombat/CrowdSimulationSystem.cppのUpdateが既存Schedulerで行う。Main.cppにはNative command / Eventの登録がある。

## 調査と採用構造

既存ContentBrowserPanelはAssetCatalogの範囲検証・一覧cache・UTF-8 path payloadを使ってAssetsを表示していた。Sourceの表示、右クリック操作、ソースと登録型の対応は不足していた。EditorWorkspaceは共通TypeRegistry、SceneEditService、既存SourceCodeEditorを組み立てているため、この経路を拡張した。

Sourceにも同じAssetCatalogを使う。新しいSourceFileActionsは宣言位置と登録済み型を照合する値処理だけを担当する。World / Module / Script instanceを所有しない。SourceAttachmentは共通のImGui payload検査で、Sceneへの追加はSceneEditServiceの既存Clone / transaction / Undoを使う。

型はファイル名から推測しない。TypeDescriptorへ編集用sourceFilesを追加し、C++は既存Reflection生成器から宣言headerと同名実装の対応を補完する。明示登録の場合もTypeDescriptor.sourceFilesへUTF-8パスを指定できる。C#はExporterがAssemblyと一致するPortable PDBを読み、型が宣言したmethod / constructorのsequence pointから文書パスをコピーする。Editorはsidecar JSONの値だけを読む。

PDBの照合とsequence pointは公式APIを使用する。[PEReader.TryOpenAssociatedPortablePdb](https://learn.microsoft.com/en-us/dotnet/api/system.reflection.portableexecutable.pereader.tryopenassociatedportablepdb?view=net-10.0)と[MethodDebugInformation.GetSequencePoints](https://learn.microsoft.com/en-us/dotnet/api/system.reflection.metadata.methoddebuginformation.getsequencepoints?view=net-10.0)を参照した。Attributeのコンストラクタからソース位置を取得する試作では既存Assembly参照に互換性問題が生じたため、公開ScriptTypeAttributeのコンストラクタを復元し、PDB方式に置き換えた。

## 操作

Details上部のScript選択欄は追加操作となり、型ごとのOpen/Detachと全解除を提供する。追加・解除は既存document履歴に入り、Undo / Redoで公開値も復元する。異なる型を最大32件扱う[複数Script対応](MultipleScriptsREADME.md)を追加した。今回の追加変更は実行未検証。[Monoの構成とLifecycle](MonoRuntimeREADME.md)も参照する。

1. Game EditorでCubeなどを選択し、Playを停止する。
2. Content BrowserでSourceを選び、Game / PlayerController.csなどのソースへ移動する。
3. 右クリックの「選択Entityへアタッチ」から型を選ぶ。複数選択時は一つのtransactionで変更する。
4. 一つの型に対応するファイルはDetailsの「C# / C++ Componentをここへドロップ」、またはHierarchyのEntity行へドラッグできる。Hierarchyのdrop先は現在の選択を変更しない。
5. Detailsで公開値を編集し、通常のLevel保存を行う。Undo / Redoでアタッチ前後へ戻れる。
6. 右クリックの「テキストエディタで開く」、またはダブルクリックでソースを開く。OSの関連付けを使い、関連付けがない場合はWindowsのNotepadへ渡す。

新規Script / Componentを作成した後はBuildしてEditorを再起動する。C# ReloadでMetadataを更新した場合は次の操作時に現在のRegistryから候補を再検索する。一ファイルに複数型がある場合は右クリックで型を選択する。

## アタッチ対象と互換性

- C#はScriptBehaviourを継承した登録済み型が対象。Source対応の取得には一致するPortable PDBとsequence pointが必要になる。PDB欠損時は型の実行・既存Levelの読込を継続し、Browserの候補は表示しない。
- C++は共通TypeRegistryにadd / containsと宣言位置を登録したEntity Componentが対象。ゲーム固有Native SystemはModuleの登録・Schedulerで動作し、Systemの.cppをEntityへ追加する操作は提供しない。CrowdSimulationSystem.cppはテキストエディタで開ける。
- ScriptComponentはEntityに一つで、内部に主Scriptと追加Scriptの値を持つ。別の型の追加で既存Scriptを置換しない。同じ型の二重追加は行わず、Editorでの構造変更はPlay停止中だけ許可する。
- sourceFilesは開発用Metadataであり、Levelには安定型IDと公開値を保存する。旧sidecarでsourceFilesが省略されていても読み込む。
- 古い配置の絶対パス、Root外symlink、同名の別ファイルは候補にしない。Project移動後は再BuildでSource対応を更新する。
- 現在のBrowserはProjectContext.Sourceを表示する。Source Root以外に置いたModuleのソース閲覧はこの切替の対象外となる。

## 所有権・依存関係・終了

EditorWorkspaceがAssets / SourceのAssetCatalogを所有し、PanelとEditorContextは借用する。Panelを先に破棄する。Drag payloadはコピーしたUTF-8 pathであり、Entity / Component / Native pointerを含まない。アタッチ候補は型IDの値で保持し、操作時に現在のRegistryを再検索する。構造変更はPanel描画の後、Editor所有Threadで共通transactionへ渡す。失敗時は候補Sceneを破棄し、元のWorldと選択を復元する。

PDB ReaderとStreamはExporter内でDisposeし、JSONへコピーした文書名だけがNativeへ渡る。Reloadで旧AssemblyやReflection objectを参照し続けるcacheは追加しない。SourceCodeEditorは関連付け済みアプリへファイルを渡す。起動先アプリの寿命をWorldやWorkspaceへ結び付けない。

## 変更ファイル

| ファイル | 変更内容 |
|---|---|
| Editor/Services/AssetCatalog.cpp | C# / C++ファイル種別 |
| Editor/Services/SourceFileActions.h / .cpp | Source範囲と共通型の宣言位置照合 |
| Editor/Core/EditorWorkspace.h / .cpp | Source一覧の所有、Panelとソースを開く操作の接続 |
| Editor/Gui/Panels/UtilityPanels.h / .cpp | Source切替、右クリック、ソースpayload |
| Editor/Gui/Panels/IEditorPanel.h | Source RootをContextで借用 |
| Editor/Gui/Widgets/SourceAttachment.h | 共通drop検査と現在型IDの解決 |
| Editor/Gui/Panels/DetailsPanel.cpp | 常設drop領域と描画後の追加 |
| Editor/Gui/Panels/OutlinerPanel.cpp | Entity行へのdropと対象ID指定 |
| Editor/Services/SceneEditService.h / .cpp | 選択と独立した対象Entityへの既存transaction |
| Runtime/Reflection/TypeRegistry.h | 編集用sourceFilesと生成情報の補完 |
| Runtime/Reflection/ReflectedInspector.h | C++宣言位置の登録引数 |
| Tools/Reflection/Generate-Reflection.ps1 | 宣言headerと同名実装の対応生成 |
| Runtime/Reflection/EngineGenerated/GeneratedReflection.cpp、Game/Generated/GeneratedGameReflection.cpp | 既存生成器が更新する宣言位置 |
| Runtime/Reflection/ManagedScriptMetadata.cpp | 任意sourceFilesの検証とScript型差し替え |
| Managed/Nexus.Managed/ScriptMetadata.cs | 一致するPortable PDBからSource対応を出力 |
| Tools/Project/SourceCodeEditor.h / .cpp | 単独ソースを関連付け済みEditorで開く |
| Resources/Editor/Localization/en.lang / ja.lang | Source操作と失敗理由の表示 |
| Build/NexusEditorSupport.vcxproj、Tests/NexusEditorTests.vcxproj | 新規実装・既存Process依存のBuild登録 |
| Tests/EditorServicesTests.cpp、Tests/ManagedReflectionTests.cpp | 対応照合、scope、drop、原子性、型差し替えと履歴の回帰 |

## 検証記録

2026-11-11に次を確認した。ログはGenerated/HybridVerification/SourceAttachment.*へ保存した。

| 対象 | 結果 |
|---|---|
| NexusEditorTests | Debug Buildは警告0・エラー0、実行exit 0。Source対応・同名ファイル隔離・Root検査・実ImGui payload・対象Entity指定・Undo / Redo・失敗時の復元、旧sidecar互換・Script型差し替えを確認 |
| NexusManagedGameplayTests | Debug Buildは警告0・エラー0、実行exit 0。PDB読取追加後のHosting、同じECS、lifecycle、Reload・状態復元・参照解放の回帰を確認 |
| HybridSample共通Build | Editor / Runtimeと2 Native / 2 ManagedのBuildが成功。GameScripts.metadata.jsonで4つのC#型のSource対応を確認 |
| GameRuntime --check-game | exit 0。Native Module / SystemとManaged sessionの起動・終了を確認 |
| 更新GameEditor | HybridSampleを指定して表示起動。DirectX 12・Workspace初期化・最初の描画frameとプロセス応答を確認 |

最初のAttribute試作による生成DLLの参照不一致は、公開コンストラクタの復元と確認用Moduleの再構築で解消した。最終成果物はPortable PDB方式である。既存Level / Assetは編集していない。

静的確認ではinclude・宣言 / 定義・link対象、UTF-8 path、型IDと保存互換、ReaderのDispose、Source範囲、所有Thread、履歴の原子性を照合した。Reflection生成ScriptのPowerShell AST、Project XML、git diff --checkにエラーはない。

OSの関連付け先アプリを実際に開く操作と、新しい右クリックメニューをユーザーが操作する確認は未実施。今回の追加機能はDebug構成で検証した。Portable PDBがない型、sequence pointを持たない型、任意C++クラスの動的Component化は対応対象外である。複数型ファイルのdrop時の選択UI、Source Root外のModule一覧、編集先アプリの個別設定は拡張候補となる。
