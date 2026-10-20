# Editor現状調査・設計レビュー

> 2026-10-21追記: 以下は実装前の調査記録です。以後のローカルEditor拡張と検証は
> [Editor利用ガイド](EditorUserGuide.md)へ記載しています。

調査日: 2026-10-20。対象は現在の作業ツリー（未コミットの変更を含む）。
この資料はPhase 1の調査結果とPhase 2以降の実装計画であり、計画項目の実装完了を意味しない。

## 1. 現在の構成

```text
main
  └─ NexusFramework（Window / フレーム進行 / GraphicsSystem）
       ├─ EditorApplication（更新・描画拡張・GUIライフサイクル）
       │    ├─ EditorSession → LevelDocument → Level / ECS
       │    ├─ EditorWorkspace → Panel / Command / Editorサービス
       │    └─ ImGuiRenderer（Win32 / DX12 adapter）
       └─ PrimitiveRenderer / GraphicsSystem

Runtime ECS → RenderSceneExtractor → RenderScene → Renderer
```

| 領域 | 実装と責務 | 現在の制限 |
|---|---|---|
| Panel | `IEditorPanel`、`PanelRegistry`、`EditorContext`。unique_ptr所有、重複ID拒否、開閉、共通Begin/End | Workspace内で具象Panelを一括生成。Factory、遅延生成、解除、複数インスタンスは未実装 |
| Workspace | サービスの組立、Document切替同期、メニュー・ダイアログ | ファイル操作、設定、ショートカット、Command登録まで集中 |
| ImGui | Context、Win32/DX12 backend、Docking、Interと日本語フォントの統合 | OSのDPI変更とUI倍率の統一管理は未整備 |
| Layout | DockBuilder初期配置、Reset、Layout.ini読込・正常終了時保存 | 名前付き配置、atomic保存、プロジェクト別保存先は未整備 |
| Command | ID、execute、canExecute。Menu、Ctrl+S/Z/Y、Terminalから利用 | キー再割当、競合解決、操作スコープ、Toolbar未実装 |
| Outliner | 親子表示、検索、Ctrl選択、context menu、親子変更drop | 階層を毎フレーム再構築。Shift範囲選択、親子変更Undo未実装 |
| Details | TypeRegistry列挙、PropertyHandle、型別Drawer、共通PropertyWidgets | 主選択だけ編集。Component追加削除、回転、配列、Asset参照など未対応 |
| Undo/Redo | Begin→Preview→Commitで単一propertyの連続入力を1操作にする。履歴256件 | 複合transaction、取消、保存位置判定、構造編集の履歴がない |
| Selection | PersistentId集合、Ctrl反転、主選択 | Viewport Pickingと未接続。一括編集なし |
| ContentBrowser | ファイル列挙、階層、検索、種別、Level Open、パスpayload | Asset DBではない。その他Assetを開く操作はログ出力のみ |
| Console | 上限付きOutputLog、検索・レベルfilter・copy。Terminal履歴・補完 | Log全件snapshotと表示文字列を毎フレーム構築。Remote log未接続 |
| Settings | Project / Editor / Userを別ファイルへ保存。User Preferencesあり | Project/Editor専用UIなし。Grid/Gizmoは設定契約のみ |
| Localization | en/ja辞書、実行中切替、ja→en→キーfallback、安定Window ID | Renderer診断等の英語直書きが残る |
| Viewport | 専用SDR RT、Panelサイズ要求、Camera aspect反映、opaque texture token | 同一processの画像表示。Picking/Gizmo/編集カメラ・ローカル入力は未実装 |
| Remote Runtime | wire codec、session/request/revision、Control/Frame interface | 実Channel、Process host、Dispatcher、GPU共有、別exe、復旧は未実装 |

主な根拠:

- [起動](../main.cpp)、[Framework](../Core/Framework/NexusFramework.cpp)
- [EditorApplication](../Editor/Core/EditorApplication.cpp)、[Workspace](../Editor/Core/EditorWorkspace.cpp)
- [PanelRegistry](../Editor/Gui/Panels/PanelRegistry.cpp)、[DockingLayout](../Editor/Gui/DockingLayout.cpp)
- [Details](../Editor/Gui/Panels/DetailsPanel.cpp)、[Reflection](../Runtime/Reflection/TypeRegistry.cpp)
- [PropertyEditService](../Editor/Services/PropertyEditService.cpp)、[Outliner](../Editor/Gui/Panels/OutlinerPanel.cpp)
- [UtilityPanels](../Editor/Gui/Panels/UtilityPanels.cpp)、[AssetCatalog](../Editor/Services/AssetCatalog.cpp)
- [ViewportSession](../Editor/Runtime/ViewportSession.cpp)、[RuntimeProtocol](../Foundation/IPC/RuntimeProtocol.h)

### モジュール依存と所有権

Runtime / Graphics / FoundationにEditor・ImGuiへの直接includeは見つからない。
ただしRuntimeのLevelはGraphicsの照明設定型、DirectionalLightComponentはShadowの型、
RenderSceneExtractorはRenderSceneに依存する。UI非依存とGraphics非依存は区別する。
現在は単一exeへのソース登録であり、独立ライブラリのビルド境界による強制はない。

EditorApplicationがSessionとWorkspaceを所有し、WorkspaceはSessionを非所有参照する。
PanelはRegistryが所有し、EditorContextは描画中にサービスを借用する。
GraphicsがViewportのTextureとdescriptorを所有する。PanelはGPU資源を所有しない。
Graphics終了時はGPU idleを待ち、Editor拡張・Renderer、Viewport資源、allocator、Deviceの順で解放する。
ログのSinkはLogger所有、表示バッファはSinkとPanelが共有する。Workspace終了時にSink登録を同期解除する。

## 2. 問題点と優先順位

| 優先度 | 問題 | 影響・対処方針 |
|---|---|---|
| P0 | メニューの未保存保護をOSの閉じる操作が経由しない | 保存/破棄/取消を共通のDocument操作として扱う。WM_CLOSEからも同じ終了要求へ接続 |
| P0 | 削除はUndo不可でproperty履歴も消去。親子変更も履歴外 | 共通履歴に構造編集を接続し、失敗時は部分変更を残さない |
| P1 | dirtyフラグとRevision更新が分散 | 保存位置と現在の履歴位置を比較。履歴外編集は明示的に記録する |
| P1 | Workspaceが多責務 | Document操作、標準機能登録、Shortcut処理を適切な単位へ移す |
| P1 | Outliner再構築、Browserの同期走査、Log全件整形が毎フレーム | 変更番号、無効化、snapshot、表示範囲のclipを導入する前に基準値を測る |
| P2 | パネル追加で中央に具象型が増える | Factoryと機能別の登録入口を追加。描画中の登録解除は遅延適用 |
| P2 | Shortcutと表示文字列が別々に定義される | Command metadataから表示と入力割当を参照する |
| P2 | UI::ThemeとImGui Styleの二系統 | 意味ベースのtokenを共通の入力として両者に反映する |
| P2 | Layoutが全Project共通、直接上書き | Project識別子・配置名で分離し、正常に書けた場合だけ置換する |
| P2 | 古い調査資料の未実装記述が現状と不一致 | 過去の記録と現在の仕様を明示し、このレビューを入口にする |

Undoで保存時の値へ戻っても現在は自動でcleanに戻らない。
メニューのNew/Open/Exitは変更中に無効になるが、保存/破棄/取消を選ぶ共通フローはない。
この二点を通知の追加だけで解決したことにしない。

## 3. Unreal Engine / Unityとの比較

UEはViewport選択、Transform tools、snap、Camera、Play controlsを制作操作として接続している。
UnityはSerializedObject / SerializedProperty経由でUndoと複数対象編集を扱う。
この操作とデータ変更の集約を参考にし、NexusEngineでは既存PropertyHandleとRegistryを活かす。

| 機能 | 次に不足を埋める範囲 |
|---|---|
| MainMenu / Toolbar | 共通Command metadata、保存、実行/停止、操作mode、入力競合処理 |
| Outliner | 範囲選択、親子変更・削除Undo、検索時の祖先表示方針、キーボード移動 |
| Details | 複数対象の共通propertyとmixed value、回転、Component追加削除、reset、metadata |
| Viewport | PickingとSelection連携、編集Camera、Gizmo、snap、入力capture、focus喪失時の解除 |
| ContentBrowser | 安定Asset ID、import/reimport、依存関係、thumbnail、参照更新、仮想化 |
| Console | Log更新番号、整形cache、clip、Remote log adapter |
| Settings / Workspace | scope別UI、Project別保存、名前付き配置、DPI対応 |
| Notification | 成功・失敗・進捗の共通通知。重要な失敗はLogにも記録 |
| 将来ツール | Profiler → Material / Shader → Animation / Particle。共通拡張入口に接続 |

参考にした一次資料:

- [UE Editor Interface](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface)
- [UE IPropertyHandle](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/PropertyEditor/IPropertyHandle)
- [Unity Editor API](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/Editor.html)

## 4. 推奨構成と代替案

### PanelとWidget

既存IEditorPanel / PanelRegistryを拡張する案を推奨する。
別のPanelManagerを重ねる案は所有と開閉状態が二重化するため、現段階では採用しない。
Panel descriptorは安定ID、翻訳key、Factory、初期開閉状態を持ち、具象型の生成は機能側が登録する。
標準機能と追加ツールを同じ登録入口に接続し、Workspaceは登録済み機能を組み立てる。
Factoryが借用するサービスはPanelより長く生存させ、解除時にcaptureも解放する。

Widgetは既存の自由関数方式を維持する。継承ツリーを新設しない。
SearchBarとPropertyWidgetsを再利用し、Toolbar、PropertyRow、Breadcrumb、通知等は利用箇所ができた時に追加する。
文字列bufferはWidget内で管理し、外部APIはstring、安定ItemId、ItemOptions、ItemResultで統一する。

### Commandと編集履歴

```text
Menu / Toolbar / Shortcut / Panel
  → EditorCommandRegistry（操作ID・metadata・実行可否）
  → 編集transaction（成功時のみ履歴へ登録）
  → PropertyEditService / 構造編集
  → Runtime Reflection / ECS
```

Command実行とUndo履歴は別の責務とする。保存、Panel開閉などは編集履歴に入れない。
property専用履歴を個別に増やす案より、構造編集も扱える共通transactionを推奨する。
Begin/Preview/CommitにCancelを加え、複数property・複数Entityの編集を一括で確定できるようにする。
履歴にはComponentの生アドレスを持たせず、Document識別・PersistentId・値を保持する。
保存位置に戻ればcleanにし、redo枝の破棄や履歴上限で保存位置を失った場合も正しくdirtyとする。
削除復元は全Componentと親子関係を保持する。未知Componentを黙って捨てる実装は採用しない。
構造編集失敗時のrollback、選択復元、古いEntity handleの無効化を検証する。

### Reflection / Details

Runtime: TypeRegistry / PropertyDescriptor / Metadata、Editor: PropertyHandle / PropertyEditService、
UI: PropertyDrawerRegistry / PropertyWidgetsという既存の分離を維持する。
Details内にComponentごとの分岐を追加しない。
Metadataへrange、unit、category、readonly、tooltip key、Drawer識別を段階的に追加する。
Quaternionは専用の変換・正規化処理で扱い、単なるfloat4編集にはしない。
配列・構造体・Asset参照は表現と保存契約を定めてから追加する。

### Settings / Theme / Localization

User / Editor / Projectのscopeを維持する。Panel配置・Shortcut等の個人設定はProject共有設定へ混入させない。
Palette、Spacing、font、icon、row height、rounding、状態色をtoken化し、ImGuiと独自描画へ同じ値を渡す。
Theme/DPI変更はNewFrame前に反映し、倍率の累積を防ぐ。複数monitor移動も検証する。
翻訳はkeyで登録し、Labelの安定IDと表示名を分離する。
既存fallbackと日本語fontを再利用し、実行中の言語変更で選択・Docking・入力状態を失わないようにする。

### Editor / Runtime通信

編集WorldをEditorSessionが所有し、再生用Runtimeへsnapshotと差分を同期する案を現時点の推奨とする。
既存の保存・Reflection・履歴を再利用できるため。ただし編集WorldもRuntimeだけが所有する要件なら、
PanelのSession参照をsnapshot経由へ、property適用を非同期要求/ACKへ変更する必要がある。
World所有とcross-adapter対応範囲は実装前に確定する設計条件であり、確定済みとは扱わない。

ControlはNamed Pipeのoverlapped I/Oを第一候補とする。localhost TCPは遠隔化に有利だが、
現行Windows専用構成ではprocess照合とhandle受渡しを含む設計が必要となる。
既存wire codecのsession/request/revisionを利用し、部分受信の組立、サイズ制限、bounded queue、timeoutを実装する。
UI threadで通信完了を待たない。Scene識別とEntity/Component/Property識別を明示し、C++メモリを直接送らない。

映像はControlにpixelを混在させず、同一adapterなら共有Textureと共有Fenceを第一候補とする。
CPU readback/uploadは実装・検証用の代替経路になり得るが、常用時の帯域・遅延を評価する必要がある。
DirectX12固有処理はGraphics/Backend内に置き、Panelはopaque tokenを解決して表示する。
producer/consumerのFence、slot所有、resource generation、lease解放、旧pool退役を管理する。
handle共有API自体は[Microsoft公式資料](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createsharedhandle)を参照。

入力はViewport相対座標、key/button状態、focusを転送し、focus喪失・切断時に解除する。
選択とpropertyはACK/revisionで整合させ、古いsessionの応答を拒否する。
process終了・pipe EOF・heartbeat timeoutで取得を停止し、Editorは維持する。
再接続は新sessionと全snapshotから復元し、相手がsignalしないFenceを無期限に待たない。
資源は自processのGPU利用完了後に破棄する。詳細候補は既存[Foundation設計](EditorFoundationREADME.md)も参照。

## 5. 実装ロードマップと完了条件

| Phase | 変更対象と理由 | 完了を証明する検証 |
|---|---|---|
| 2A 編集基盤 | PropertyEditService、共通履歴、EditorSession。構造編集と保存状態を統一 | property連続入力、Cancel、複数対象/複数property、削除/親子復元、失敗rollback、履歴分岐/上限、保存位置、World切替 |
| 2B Document操作 | WorkspaceからNew/Open/Save/終了フローを分離。WM_CLOSEも同じ要求へ | 保存/破棄/取消、SaveAs取消・失敗時に元Documentを維持、OS終了とメニュー終了の一致 |
| 2C 拡張基盤 | Panel Factory、機能登録、Command metadata、Shortcut、設定・Widget入口 | 中央クラスを変えないPanel追加、重複拒否、遅延生成/解除、借用参照の寿命、キー競合、入力中とmodalの抑止 |
| 3 UI/UX | Theme、Docking、Widgets、翻訳、通知。共通操作規則を整備 | Dark/Light、DPI変更、英日切替、名前付きLayout復元、安定ID、keyboard focus、tooltip、成功/失敗通知 |
| 4 既存Panel | Outliner→Details→ContentBrowser→Console→Viewport | 階層/検索/範囲選択、一括編集、Asset操作、log clip、Picking/Gizmo/Camera、選択同期、性能計測 |
| 5 Runtime・専門ツール | process別起動、Control実装、Graphics共有、Play/Pause/Stop、Profiler等 | 実Runtime画像、連続Resize、負荷時backpressure、crash/再接続、device loss、古い応答拒否、GPU参照の安全な終了 |

各段階で変更ファイル・責務・影響・検証結果を記録する。未検証の機能を完了扱いしない。
Phase 5の専門ツールは共通基盤を通して追加し、Renderer診断の具象参照をPanelへ増やさない。

### 性能測定計画

現在、速度改善を裏付ける計測結果はない。次の基準測定後に最適化する。

| 対象 | シナリオ | 記録する値・無効化条件 |
|---|---|---|
| Outliner | 1千/1万/10万Entity、平坦/深い階層、折畳み、検索、複数選択 | UI CPU時間、allocation、階層再構築回数。作成/削除/親子/名前変更で更新 |
| Asset | 1千/1万件のfolder、初回/再表示/検索/外部変更 | filesystem走査時間、UI停止、cache容量。監視通知/明示Refreshで無効化 |
| Details | 多Component、同じ選択の継続、選択切替、一括編集 | 列挙/resolve時間、allocation。型登録/Component構成/選択変更でcache更新 |
| Log | 無更新/連続追加、filter切替、copy | snapshot/整形/描画CPU時間。追加番号/filter/言語変更で更新 |
| Viewport/IPC | 安定size/連続Resize、表示/非表示、Runtime遅延 | GPU idle待機、frame遅延、queue長、pool数、帯域、復旧時間 |

Entityの深さ制限、Asset最大4096件、Log最大2048件は既存制限であり、仮想化・高速化の達成証拠ではない。
UIの行clip、遅延処理、検索cache、非同期走査は測定した支配要因へ適用する。

## 6. 最初の変更対象と拡張手順

| ファイル | 最初に変更する目的 | 主な影響 |
|---|---|---|
| Editor/Services/PropertyEditService.h/.cpp | 共通transactionへ接続、Cancel・複合編集・保存位置 | Details、Undo/Redo、Document切替 |
| Editor/Core/EditorSession.h/.cpp | Document寿命、選択、履歴外変更の扱い | 保存状態、World切替 |
| Editor/Core/EditorWorkspace.h/.cpp | Document操作と機能登録を分離、削除を履歴へ接続 | File/Edit menu、Terminal、Shortcut |
| Editor/Core/EditorApplication.cpp | OS終了要求を共通Document操作へ接続 | WM_CLOSE、Editorの終了順序 |
| Editor/Gui/Panels/OutlinerPanel.cpp | 親子変更のtransaction化、階層cache | 選択、検索、drag/drop |
| Editor/Gui/Panels/PanelRegistry.h/.cpp | Factory、遅延生成、解除、安定descriptor | Window menu、設定復元、追加Panel |
| Editor/Services/EditorCommandRegistry.h | metadataとShortcutの共通参照 | MainMenu、将来Toolbar、入力処理 |

RuntimeやECSへ変更が必要な場合は、UI非依存のsnapshot/復元等として設計し、Editor固有処理を持ち込まない。
新規class/function/memberには既存規則の日本語コメント（class `/*----- */`、Doxygen、member `//<`）を付ける。

現在の追加手順と改善後の目標:

- Panel: IEditorPanelを実装→Workspaceで生成登録→翻訳追加。改善後は機能側からdescriptor/Factoryを登録する。
- Widget: Gui/Widgetsに自由関数を追加→ItemOptions/ItemResultで状態を返す→Panel/Drawerから利用する。
- Command: 安定IDとexecute/canExecuteを登録→Menu/Terminalから実行。改善後はShortcut/Toolbarもmetadataを共有する。
- PropertyDrawer: Runtimeの型記述へproperty登録→必要なPropertyKind/Drawerを登録→Detailsは変更しない。
- Theme/言語: 共通token/辞書keyへ追加し、個別Panelへ色や表示文字列を直書きしない。

## 7. 検証状況と変更範囲

2026-10-20に`Project/Tools/ValidateEditorFoundation.ps1`を実行し、PASSを確認した。
検査範囲はproject XML、source登録、非推奨filesystem API、Runtime/UI include、Panel/GPU境界、
Detailsの具象Component依存、共通style/widget利用、英日92組の翻訳keyである。
文字列とソース構造の検査であり、コンパイル・実行時の依存や動作の保証ではない。

既存GuiTestsはSearchBarとLevel menuの選択/変更中無効化などを対象とする。
調査時点で共通Undo、構造編集復元、終了確認、IPC/GPU復旧の動作を証明するテストは確認できない。
ビルド、Editor起動、GPU実行、対話操作、性能測定は今回実施していない。

今回作成・更新するものはこの設計資料と資料へのリンクのみ。C++、設定、shader、テストコードは変更しない。
既存の未コミット変更は利用者の作業として保持する。
