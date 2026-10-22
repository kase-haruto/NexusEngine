# NexusEngine Editor

更新日: 2026-10-21。現在の実装はWindows / DirectX12 / ImGuiの同一プロセスEditorです。
構成の比較と長期計画は[設計レビュー](EditorArchitectureReview.md)、描画基盤は[Graphics設計](GraphicsREADME.md)を参照してください。

## 起動・ビルド

リポジトリルートから実行します。Visual StudioのC++ツールとWindows SDKが必要です。

```powershell
./Project/Tests/Invoke-GraphicsBuild.ps1 -Project Project/NexusEngine.vcxproj
Push-Location Generated/Outputs/Debug
./NexusEngine.exe
Pop-Location
```

実行時の作業ディレクトリには`Config`、`Resources`、`Shaders`が必要です。
通常はビルド時に出力先へコピーされます。起動Levelは`Config/Project.settings`の`startupLevel`で指定します。

## 編集操作

| 操作 | 入力・入口 |
|---|---|
| 新規 / 開く | Ctrl+N / Ctrl+O、File menu |
| 保存 / 名前を付けて保存 | Ctrl+S / Ctrl+Shift+S。保存先未指定ならパス入力を表示 |
| Undo / Redo | Ctrl+Z / Ctrl+Y、Edit menu、Toolbar |
| Entity / Camera / Light作成 | Edit menu。ToolbarにはEntity作成も表示 |
| Model Instance作成 | Edit menu。Detailsで描画するMeshを選択 |
| 複数選択 | Outliner / ViewportのCtrl+クリック |
| 範囲選択 | OutlinerでShift+クリック。Ctrl+Shiftで既存選択へ追加 |
| 削除 | Delete / context menu。選択Entityだけ削除し、残る子はrootへ戻す |
| 親子変更 | Outliner行へdrag/drop。root行へdropすると親を解除。Local Transformを保持 |
| 詳細編集 | Details。全選択に共通するpropertyを一括編集し、異なる値はその旨を表示 |
| 再生 / 停止 | F5 / Shift+F5、Toolbar。一時停止・再開もToolbarから実行 |
| Panelの開閉 | Window menu |
| 名前付き配置 | Window → Workspace layouts → General / Lighting / Animation |
| 配置の初期化 | Window menuのReset |
| 言語・テーマ・倍率 | Edit → Preferences。英日、Dark/Light、font、手動UI倍率 |

文字入力中、activeな操作中、popup表示中はglobal Shortcutを抑止します。
失敗は通知とLogで確認できます。File切替と終了には保存/破棄/取消の確認があります。
OSの閉じる操作も共通確認を通ります。SaveAs取消・保存失敗では元Documentを保持します。

### Viewport

- クリックで静的Meshの最近傍三角形を選択します。Ctrlで選択を反転します。
- 選択したMeshは約2.5pxのオレンジ色の輪郭で強調します。Sceneの深度を参照しない独立マスクから外周を合成するため、手前の物体に遮られていても表示します。複数選択では選択集合の外周を表示します。
- 右ボタンを押しながらWASD/QEで編集Cameraを移動し、マウスで向きを変えます。Shiftで加速します。
- ホイールで前後移動、Fで主選択の境界へCameraを寄せます。
- W/E/RまたはViewport内の選択肢で移動/回転/拡縮を切り替えます。
- ImGuizmoの軸・平面・回転リング・拡縮ハンドルをドラッグします。操作の開始から終了までを一回のUndoにまとめます。
- Ctrlを押すと移動0.5、回転15度、拡縮0.1の刻みに揃えます。Escapeで操作開始値へ戻します。

ImGuizmoは主選択のLocal軸で表示し、Nodeのオフセットを含むメッシュ境界の中心をpivotにします。形状を持たないEntityではTransformの原点を使います。
回転・拡縮は同じEntity空間の形状中心を保ちます。複数選択も主選択の形状中心をpivotにWorld空間でまとめて操作し、各Entityの親座標系へ戻します。
ギズモ上のクリックを優先し、ドラッグやEscape取消からマウスを離すまで、背後の物体の選択へ入力を渡しません。
親子を同時選択した場合も二重に変換しません。非一様scaleの親等でLocal TRSに戻せないshearが生じる操作は拒否し、直前の有効値を保持します。
Camera操作は編集DocumentのTransformを変更しません。再生中はRuntime Cameraを使います。
Skin変形Meshのpickingは未対応です。DetailsのEuler角には特異姿勢があるため、Viewportの回転リングも利用してください。

### Details・履歴

Details上部の「Add Component / Componentを追加」からPrimitive Rendering、Camera、DirectionalLightを追加できます。
複数選択時は不足しているEntityだけに追加し、一回のUndoで戻せます。追加Cameraは非primary、既存Lightがあれば追加Lightは無効で始まります。

Primitive RenderingのMesh欄をクリックしてglTF / GLB / OBJを選ぶか、Content BrowserのファイルをMesh欄へドロップします。
Mesh Nodeでモデル全体または個別Nodeを選択できます。Mesh変更時は全Nodeへ戻し、以前のモデルのNode番号を引き継ぎません。
Base Color Texture欄も同じ操作で画像を選べます。PNG / JPEG / BMP / TIFFに対応し、Xでモデル付属の材質へ戻します。
AssetはプロジェクトのAssetフォルダ内から選びます。型違い・破損ファイル・外部への参照は拒否し、理由を欄に表示します。

Meshには実Geometryから生成した深度付き形状プレビュー、画像には実画像のプレビューを表示します。
Content Browserでは対応ファイルにマウスを置くとプレビューが出ます。
Meshと画像の参照はAssetフォルダからの相対パスでLevelへ保存し、Undo / Redoと再読込に対応します。
Base Colorの差替えは選択Entityの全材質へ適用し、Raster・影・光学描画で同じGeometry / Materialを使います。
初めて使うモデル・材質の組ではGPU snapshotを再構築するため、一時的に編集が止まる場合があります。

Name、Transform（位置・Euler角UIによるQuaternion回転・拡縮）、Primitive（表示・色・静的Lighting）、
Camera（FOV・clip・primary/enabled）、DirectionalLight（強度・色・方向・enabled）を登録しています。
primary Cameraと有効なDirectionalLightの重複、無効clip、非有限値、特異scale、ゼロQuaternion等をsetterで拒否します。

ドラッグ・連続文字入力は一つの履歴になります。複数対象も一括Undoできます。
異なる型の複数propertyも`PreviewValues`で一つのtransactionにできます。
作成、削除、親子変更、property編集は同じ時系列に入り、保存位置へUndo/Redoで戻るとcleanになります。
履歴は最大256件、Document切替で破棄します。Redo分岐は新しい編集で破棄します。

構造編集の履歴はCPU World全体のsnapshotを保持します。未知のコピー可能Componentも保持できますが、
大きいWorldでの操作時間・メモリには追加最適化が必要です。コピー不可Componentがある場合、構造編集と再生開始を拒否します。
ComponentにScene内の生ポインタや外部所有資源を入れる場合は、その型自身のコピー契約を定める必要があります。
EntityはWorldより長く保持せず、Editorの参照にはPersistentIdを使ってください。

再生Worldは編集Worldの複製です。停止すると破棄し、編集Worldと編集時のModel Animation状態を保持します。
再生中は編集と保存を無効にします。再生結果を編集Worldへ適用する機能はありません。

## 設定・配置・一覧

個人設定は`LOCALAPPDATA/NexusEngine/Editor/User.settings`、共有設定はProjectの`Config`へ保存します。
名前付き配置はProjectパスから作る識別子ごとに`Workspaces/<project-id>/<profile>.ini`へ保存します。
自動保存有効時は配置切替と終了時に保存します。書込み成功後に同一volume上の一時ファイルを置換します。
OSのmonitor DPI変更と手動UI倍率を掛け合わせ、NewFrame前にStyleを基準値から再適用します。
独自Widget用のThemeも適用済みImGui Styleから同期します。

OutlinerはDocument、Scene、Entity数、編集Revisionが変わった時に階層を再構築します。
入力プレビュー中にも再構築し、名前編集を検索へ反映します。展開/検索後の行をclipします。
Logは追加/Clearの更新番号と検索・filter・言語を基にsnapshot/整形を更新し、表示範囲だけ描きます。
Asset一覧は2秒cacheと明示Refreshを使い、最大128フォルダを保持します。4096件での列挙打切りは廃止しました。
走査は同期なので、初回の大規模folder・network driveの待機は今後の非同期化対象です。

## 拡張方法と責務

`EditorApplication::AddEditorExtension`へ初期化前に登録関数を渡すと、中央のWorkspaceを変更せずPanel/Commandを追加できます。

```cpp
editor.AddEditorExtension([](NexusEngine::EditorWorkspace& workspace) {
    workspace.Panels().RegisterFactory("MyTool", "panel.myTool",
        [] { return std::make_unique<MyToolPanel>(); }, false);
});
```

Panelは`IEditorPanel`を実装し、安定IDと辞書keyを返します。Factoryは初めて表示した時に呼ばれます。
借用サービスはPanelより長く生存させます。描画callback内の登録は拒否し、解除はEnd後に遅延適用します。
翻訳keyはen/ja両辞書へ登録します。PropertyDrawerはRuntime型記述とUI Drawerに追加し、Details本体は変更しません。
Widgetは既存Gui/Widgetsの自由関数方式、Commandはexecute/canExecuteと安定IDを使います。
ShortcutはCommandRegistryのBindを通し、重複割当を拒否します。Menu/Toolbarは同じ操作と割当表示を参照します。

| 実装 | 責務 |
|---|---|
| EditorHistory | 履歴位置、Redo枝、保存位置、成功時の状態番号 |
| PropertyEditService | 値の再解決、検証、複数property Preview/Commit/Cancel |
| SceneEditService | 完成Worldの交換、作成/削除/親子変更と選択の復元 |
| EditorDocumentService | 保存/破棄/取消、SaveAsの継続、New/Open/終了 |
| PanelRegistry | Factory、Panel所有、表示・安定ID・解除 |
| ViewportTools | 編集Camera、CPU picking、Gizmo。GPU資源は所有しない |
| EditorWorkspace | 上記サービスの組立、共通UIへの接続 |

## 検証

```powershell
./Project/Tests/Invoke-GraphicsBuild.ps1 -Project Project/Tests/NexusEditorTests.vcxproj
./Generated/Outputs/EditorTests/NexusEditorTests.exe
./Project/Tools/ValidateEditorFoundation.ps1
```

自動テストはproperty transaction/Cancel/rollback、保存位置と分岐・上限、構造編集と独自Componentの復元、
保存失敗/取消/再Open、再生分離、Shortcut競合、遅延Panel生成と描画中解除、静的三角形picking、Camera/Light検証を対象とします。
画像を付けたViewport、10,000 EntityのOutliner、未保存modal、Command経由のPlay/Pause/Stop、Theme再適用、配置切替もCPU UIで確認します。
10,000 Entityの安定100frameで階層再構築がないことと、描画頂点数を検査します。
計測例は約0.6ms/CPU UI frame（描画頂点3,630）です。Debugの単一環境での値であり、GPU描画時間や初回ロード時間は含みません。

2026-10-21にDebug本体のビルド、Editor/ECS/GUIテストのビルドと実行（終了コード0）、静的検査を確認しました。
RTX 3060 Tiで実起動し、Docking、Toolbar、SponzaのViewport描画、Content Browserを画面で確認し、正常終了Logも確認しました。
複数monitor間のDPI切替、device loss、全操作の実マウスによる検証は未実施です。

実画面確認用の`Project/Tools/Preview-Editor.ps1 -Action Start/Capture/Close`は、専用の個人設定を使い、
起動Logと画像を`Generated/EditorPreview`へ保存します。Captureは対象windowを前面へ表示します。

## 今後の範囲

別プロセスRuntimeの起動、実IPC、共有GPU画像、切断復旧は設計契約のみで、今回のローカル再生とは別です。
glTF / GLB / OBJのMesh選択とBase Color画像の差替えに対応しています。AssetのGUID管理、自動再import、Normal / Metallic等の画像差替えは今後の範囲です。
Clipboard操作、Component削除、配列/構造体の汎用Drawer、Profilerと専門Asset Editorも今後の範囲です。
これらを実装済みとしてUI上で有効化したり、Remote接続済みとして表示したりしません。

## Detailsのカテゴリ編集

編集窓口をDetailsへ統合しました。上部の名前欄とComponent追加はどのカテゴリでも表示されます。左の縦アイコンで「全般」「描画」「マテリアル」「カメラ」「ライト」「その他」を切り替えます。アイコンには説明Tooltipがあり、選択Entityに対応するカテゴリだけが表示されます。専用カテゴリのないComponentは「その他」へ自動表示されます。

メッシュ・Node・描画の有効化・静的ライティングは「描画」、Transformは「全般」、PBR・Base Color画像・Tintは「マテリアル」で編集します。各ページのスクロール位置はカテゴリ別に保持します。Detailsは引き続き独立してDockできます。旧PropertiesウィンドウはWindowメニューから削除し、旧Propertiesだけを開いていた設定もDetailsへ移行します。保存済みDock配置はリセットしません。

Materialでは主選択のEntityのSlotを選び、「元の材質からOverrideを作成」で編集を始めます。Base Color、Metallic、Roughness、Normal Scale、発光色、遮蔽強度を編集できます。共有モデルや他のEntityの材質は変わりません。変更はUndo/RedoとLevel保存に対応します。

現在はEntityあたり1 SlotをOverrideします。Slotを変えると前のSlotのOverrideを置き換えます。Base Color画像とTintは従来どおりEntity全体に適用され、画像欄にはContent BrowserからDropできます。ほかの埋込画像はモデルから継承します。静的な間接光の変更には再ベイクが必要です。

実装の依存関係、制約、新しいカテゴリの追加方法は[Detailsカテゴリ編集の開発ノート](PropertiesPanel.md)を参照してください。
