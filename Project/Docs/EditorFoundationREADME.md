# NexusEngine Editor Foundation

## 現在の到達点と確認待ち

**これは基盤実装の中間成果であり、全完了条件を満たした状態ではない。**
既存の単一プロセス起動経路へEditor Workspaceを統合した。
`NexusEditor.exe` / `NexusRuntime.exe` の分割、実IPC、共有GPU映像はまだ接続していない。
現在の同一プロセス起動では、Graphics所有の専用RenderTargetをViewportへ表示する経路を接続した。
表示は「ローカルSceneプレビュー」と明示し、Remote接続済みとは扱わない。
ビルド、Editor起動、GPU実行、対話操作は依頼に従い一切実施していない。

ユーザーへ提示した以下の設計確認の回答を待っている。

1. 編集LevelをEditorSessionに残してRuntimeへ同期するか、編集LevelもRuntimeだけが所有するか。
2. 初期GPU共有を同一ユーザー・同一adapterに限定できるか、cross-adapter対応も必要か。

確認前にWorldの所有とGPU共有方式を確定しないため、既存のローカル編集経路を保った。
Runtimeのみが所有する場合はEditorContextのSession参照をsnapshot/documentサービスへ置換し、
PropertyEditServiceの適用先を非同期要求・応答へ変更する必要がある。
現在のReflection descriptorはRuntime側dispatcherでそのまま再利用できる。

調査結果は [EditorFoundationSurvey.md](EditorFoundationSurvey.md) を参照。

## 実装した機能

| 領域 | 実装内容 | 実行確認 |
|---|---|---|
| Panel | 共通契約、登録、重複拒否、表示切替、安定ID | 未実施 |
| Docking | 初期分割、Reset、個人Layout.iniの読込・終了時保存 | 未実施 |
| Menu | File/Edit/Window/Tools/Help、Command経由の実行可否 | 未実施 |
| File | New/Open/Save/SaveAs、パス入力modal、最近10件、終了 | 未実施 |
| Selection | 永続IDによる複数選択、Ctrlで反転、最後の選択をDetailsへ | 未実施 |
| Outliner | 親子、検索、選択、Details経由の名前変更、描画componentの表示切替、context menu、親変更drop | 未実施 |
| Details | 型情報の列挙、値コピー編集、検証、操作単位Undo/Redo | 未実施 |
| Localization | 外部UTF-8辞書、英日91キー、実行中切替、en→キーfallback | 未実施 |
| Settings | 個人/Editor/Project分離、既定値、永続化、Preferences | 未実施 |
| Log | 6段階filter、文字列検索、時刻、category/source、Clear/Copy/追従 | 未実施 |
| Terminal | help / echo / log.clear / editor.undo / editor.redo、履歴、単一候補Tab補完 | 未実施 |
| Content | フォルダtree、移動、検索、種別、double click、asset path drag payload | 未実施 |
| Viewport | 同一processの専用RT表示、Panel寸法Resize、Camera aspect同期、Remote転送契約 | 実行未確認・Remote転送未実装 |
| IPC | v1 envelope、制限付きwire codec、Resize/Frame codec、接続interface | 実Channel未実装 |

変更中のLevelがある時のNew/Open/メニューExitは無効にする。
Content BrowserからのLevel Openも未保存変更がある場合はログに理由を表示して拒否する。
現状では保存してから切替える運用であり、破棄/保存/取消の共通確認dialogは未実装。
OSウィンドウの閉じる操作にはこの保護がまだ適用されない。

Cut/Copy/Paste/Duplicateは未登録Commandとしてメニューで無効になる。
Entity削除は取り消せないことをラベルに明示し、削除後にプロパティ履歴を破棄する。
親変更は既存TransformSystemで循環を防ぎ、Local Transformを維持する。
親変更のUndo、複数Entityの一括property編集、継承する表示状態は未実装。

## クラスの責務とファイル

以下の`.h/.cpp`は同名ペア（`.h`のみと明記したものを除く）。

| ファイル | 責務 |
|---|---|
| `Editor/Core/EditorApplication`（変更） | 既存FrameworkとGUI backendのライフサイクル。WorkspaceへUI組立を委譲 |
| `Editor/Core/EditorWorkspace`（追加） | 各サービスのcomposition root、document変更の同期、command接続 |
| `Editor/Core/EditorSession`（変更） | 既存LevelDocument所有と変更状態。SelectionServiceを所有 |
| `Editor/Gui/Panels/IEditorPanel.h`（追加） | 内容描画の共通契約、非所有EditorContext |
| `Editor/Gui/Panels/PanelRegistry`（追加） | Panel所有、開閉状態、安定Window ID、Begin/End |
| `Editor/Gui/DockingLayout`（追加） | 配置の初期構築、Reset、保存・復元 |
| `Editor/Gui/MainMenuBar`（追加） | CommandとPanelからメニューを構築、Tools登録 |
| `Editor/Gui/Panels/OutlinerPanel`（変更） | 既存ECSから階層snapshotを表示。選択と編集はサービスへ |
| `Editor/Gui/Panels/DetailsPanel`（変更） | 具体Componentを知らずReflectionからpropertyを描画 |
| `Editor/Gui/Panels/UtilityPanels`（追加） | OutputLog/Terminal/ContentBrowser/Preferences/Viewportの各Panel |
| `Editor/Gui/PropertyDrawerRegistry`（追加） | PropertyKind→Drawerの登録と値コピーの編集 |
| `Editor/Gui/Widgets/PropertyWidgets`（変更） | 共通Styleを継承する型別入力と編集区間通知 |
| `Editor/ImGui/ImGuiStyle`（追加） | Renderer内にあった既存配色・寸法を集約。設定反映時にも同じ基準を使用 |
| `Editor/Services/EditorCommandRegistry.h`（追加） | 操作要求、実行可否、拡張command登録 |
| `Editor/Services/SelectionService.h`（追加） | Entity参照を保持しない複数PersistentId選択 |
| `Editor/Services/PropertyEditService`（追加） | PropertyHandle再解決、検証付き編集、操作集約、履歴 |
| `Editor/Services/LocalizationService`（追加） | 英日辞書、fallback、安定ID付きlabel |
| `Editor/Services/EditorSettings`（追加） | scope別データと保存先、読込順序、検証、atomic置換 |
| `Editor/Services/ConsoleService.h`（追加） | エンジン内部command、実行、履歴、補完。OS shellなし |
| `Editor/Services/AssetCatalog`（追加） | 既存asset root内のファイル列挙。外部symlinkを拒否 |
| `Editor/Runtime/ViewportSession`（追加） | Control/Frameの仲介、frame lease、画像token解決 |
| `Runtime/Reflection/TypeRegistry`（追加） | UI非依存型記述、既存Component adapter |
| `Runtime/Level/LevelDocument`（変更） | 既存保存・復元。ImGuiとEditor向け未定義メソッドを除去 |
| `Foundation/Logging/BufferedLogSink.h`（追加） | 所有ログ値、排他、保管上限、UIへのsnapshot |
| `Foundation/Logging/Logger`（変更） | Sinkの同期解除API、既定Sink登録時の容量検査 |
| `Foundation/IPC/RuntimeProtocol`（追加） | Graphics/Editor非依存のwire契約とcodec |
| `Graphics/Viewport/IViewportFrameTransport.h`（追加） | API型を公開しないGPU frame取得・解放契約 |
| `Graphics/Viewport/ViewportImage.h`（追加） | UI backend向けの非所有・不透明画像参照 |
| `Graphics/Backend/DirectX12/Viewport/ViewportTarget`（追加） | 同一process用SDR出力とRTV/SRV、状態遷移の所有 |
| `Graphics/Renderer/GraphicsSystem`（変更） | Scene出力先分離、記録前のPanel Resize、UI前のSwapChain寸法復元 |
| `Graphics/Renderer/GraphicsContext`（変更） | DepthなしのUI描画先を復元可能にする |
| `Editor/ImGui/ImGuiRenderer`（変更） | 既存ITextureProvider経由でViewport tokenを画像参照へ解決 |

その他の変更・追加:

- `main.cpp`: Project設定のassetPathを既存Renderer初期化へ接続。
- `Editor/Core/ScenePanels.h`: 旧LevelDocument GUI呼出を互換用no-opへ変更。旧自由関数は未使用。
- `NexusEngine.vcxproj`: 新規ソース/ヘッダ登録、Configを出力へコピーするTarget。
- `Config/Project.settings`, `Config/Editor.settings`: 共有設定の初期値。
- `Resources/Editor/Localization/en.lang`, `ja.lang`: UTF-8翻訳。
- `Tools/ValidateEditorFoundation.ps1`: コンパイルしない静的検査。
- 本README、調査資料、既存EditorSession/Gui設計資料の追記。

## 依存関係と既存設計からの変更

```text
EditorApplication
  └─ EditorWorkspace
       ├─ PanelRegistry → IEditorPanel → Editorサービス
       ├─ MainMenuBar → EditorCommandRegistry → 既存EditorSession
       ├─ PropertyEditService → Runtime TypeRegistry → 既存Level/ECS
       └─ ViewportSession → IRuntimeConnection / IViewportFrameTransport

Runtime Level/ECS/Reflection → Foundation
Graphics Frame Transport → Foundation IPC
Foundation Logging → ILogSink
```

RuntimeにEditor/ImGuiのincludeはない。Detailsは`GetComponent<T>`を呼ばない。
Component具体型の登録はRuntime/Reflectionへ集約する。
新Componentの登録はTypeRegistry、表示型の拡張はDrawerRegistryで行う。
Runtimeはlabel keyをmetadataとして持つだけで翻訳やUI生成を行わない。

LevelDocumentの旧ShowGuiを除去したのはRuntime→ImGui依存をなくすため。
旧Saveは名前から毎回保存先を再生成しており、SaveAsの保存先を失っていたため修正した。
保存先がなければSaveAsを要求し、null Levelも検査する。
LevelSerializerの既存形式・ECS Storage・Rendererの通常描画は変更していない。

EditorApplicationの従来の影/光学/PostProcess診断は`ShowLegacyDiagnostics`へ残し、Toolsから表示する。
この既存診断画面のラベルは英語のまま。新しい基盤Panelは辞書を使用する。
この診断は現在の同一プロセスRendererへの直接設定なので、別プロセス化後は対応commandが必要。

## Reflectionと履歴

PropertyValueはbool / int / float / UTF-8 string / float2 / float3 / float4 / enum値。
初期の実Component登録はName、Transform.translation/scale、Primitive.enabled/tint、Camera.fov。
int/float2/enum用Drawerも用意したが、その型を持つ新Componentの登録が必要。
Quaternionをfloat4として誤編集しないため回転は今回公開していない。
DirectionalLight、Color専用UI、AssetReference、配列、構造体は今後の登録対象。

PropertyHandleはPersistentId + type ID + property ID。Componentのアドレスやoffsetを保存しない。
Begin→Preview→Commitでドラッグや文字入力を1操作へまとめる。
履歴は256操作、World切替とEntity削除で破棄する。有限値・FOV・名前長をsetterが検証する。
変更状態は保守的に保持し、Undoで保存時の値へ戻っても自動ではcleanにならない。
Nameは最大1024byte、UIの各入力bufferにも上限がある。

## LocalizationとSettings

辞書形式は1行 `key "UTF-8 value"`。標準`std::quoted`でquoteとbackslashをescapeする。
共通JSONライブラリがないため追加依存を避けた外部テキスト形式。
改行を含む翻訳値は現行形式では使わない。ID欠落時は選択言語→英語→IDの順にfallbackする。
Text/Labelは所有stringを返し、言語切替で参照先を無効化しない。
ImGui Window/Itemは`translated text###stable ID`を使い、言語変更でlayoutを失わない。

既存の黒基調/Orange Accentは`ImGuiStyle.cpp`を唯一の基準とする。
PreferencesがImGui標準themeで上書きしていた箇所は共通Style適用へ変更した。
検索欄は既存SearchBarへ統一し、型別入力はWidgets/PropertyWidgetsを再利用する。

既存ImGuiRendererはInter + NotoSerifJPをmergeしている。そのatlasを再利用し、
ImGui 1.92.9bのFontSizeBase/FontScaleMainで論理サイズを変更する。
言語切替だけでatlasを破棄・再構築しない。GPU font textureは既存backendが所有する。

| scope | 保存先 | 保存タイミング |
|---|---|---|
| Project | 起動working directoryの`Config/Project.settings` | `SaveProject()`明示呼出。起動で読込 |
| Editor | 同`Config/Editor.settings` | `SaveEditor()`明示呼出。起動で読込 |
| User | `%LOCALAPPDATA%/NexusEngine/Editor/User.settings` | Preferences反映、recent変更、終了時 |
| Layout | 同個人ディレクトリ`Layout.ini` | Auto Save Layoutがtrueなら正常終了時 |

LOCALAPPDATAがない場合はOS一時ディレクトリ配下の同じ相対位置を使用する。
読込は既定値→Project→Editor→User。言語/themeの未知値、非有限font/scaleは既定値へ戻す。
共有と個人設定のファイルを混在させない。
設定ファイルは同一directoryのtempへ書き、成功後MoveFileExの置換で更新する。
Layoutは現在直接書込みであり、crash時のatomic保存・定期保存は今後の対応。

Grid/Gizmoは保存契約のみで、描画機能は未実装。Project名のウィンドウタイトル反映も未接続。
Startup LevelとAsset Pathは起動に反映する。モデルは既存Sponzaデモの固定選択を維持する。
Project/Editor設定専用UIは未実装で、外部ファイル/APIから設定する。

## Logging / Contentの制限

Loggerは既存の同期配送。Sinkはmessage/category/sourceを所有コピーし、string_viewを保持しない。
mutex下で取得した値snapshotをUI threadで描画する。Sink内でLoggerを再呼出ししない。
最大2048件、本文8192byte、category256byte、source約512byteで、長時間実行時も上限がある。
Log配送のmutexとbuffer mutexの順序はLogger→buffer。UIはLogger mutexを持たずbufferへアクセスする。
終了時はRemoveSinkで配送完了と解除を同期する。初期化途中の破棄でもSinkを残さない。
ReleaseのTrace/Debug除外は既存Loggerの仕様を維持する。
別プロセスRuntimeからのログ転送は今後Control Channel adapterが担当する。

AssetDatabaseは既存コードにない。AssetCatalogはファイル閲覧専用でDBを装わない。
フォルダは最大4096項目、treeは深さ12まで。Outlinerは深さ128まで描画する。
Levelのdouble clickはOpen、その他はパスをログ出力する。preview/importは未実装。
Drag payload `NEXUS_ASSET_PATH`にはnull終端UTF-8 pathをコピーする。
symlink解決後にroot外のpathを拒否する。大規模DB/非同期走査/thumbnailは拡張対象。

## 別プロセス構成（これから接続する範囲）

### 現在のローカルViewport表示

前の実装ではSceneをSwapChain全体へ描き、Panelには実画像の供給元がなかったため何も表示できなかった。
現在は次の経路を接続した。ビルド・GPU実行確認はまだ行っていない。

```text
EditorApplication::PrepareRender
  → GraphicsSystem::PrepareSceneViewport（Resizeがある時のみGPU idle待機）
  → 実RTサイズからCamera aspectを抽出
GraphicsSystem::RenderFrame
  → HDR Scene + Depth
  → PostProcess Resolve → 専用RGBA8 ViewportTarget
  → RenderTarget状態からShaderResource状態へ遷移
  → BackBuffer/Window寸法とDepthなしUI描画先を復元
  → ImGuiRenderer::Resolve → ViewportSession::Image → ViewportPanel
```

GraphicsSystemがTextureとRTV/SRVを所有し、Panelは画像参照の読み取りと次frameへのサイズ要求だけを行う。
共有shader-visible heap内のSRVを使用し、PostProcess専用heapから戻した後にImGuiを記録する。
Scene書込みとImGui samplingは同じDirect Queue上で直列実行するため、ローカル経路は単一出力Textureで競合しない。
これはprocess間共有ではない。Remote経路のring、shared handle/fenceは引き続き未実装。

Resize要求はPanelから即実行せず次のPrepareRenderで適用する。
GPU idle後にSDR/Depth/HDRを同じ寸法へ更新し、Cameraのaspectも新しい実寸法へ合わせる。
SRV slotは維持する。WindowのResizeはSwapChainだけを変更し、Sceneサイズを上書きしない。
画像とdescriptorはGPU使用完了後、allocatorより先に解放する。
サイズ0/非表示時は最後の解像度を保持する。Resize中のみqueue idle待機するため、連続Resizeの性能は未評価。
ローカルPreviewへの入力操作・Picking/Gizmoは未実装で、入力をRuntimeへ送信したことにはしない。

```text
NexusEditor.exe                         NexusRuntime.exe
  UI / Panel / Selection                Level / ECS / Physics / Render
  RuntimeProcessManager -- Control --> RuntimeCommandDispatcher
  ViewportSession       <-- notifications / errors / logs --
  Graphics consumer     <-- Shared Texture + Shared Fences -- Graphics producer
```

ProcessManager、Dispatcher、実Channel、Graphics producer/consumer、二つのexe targetは未実装。
NexusRuntimeの単独起動/Play/Pause/Stop/異常終了検知は、現行コードで実行可能とは主張しない。
接続interfaceは将来の構成の境界を固定するもので、接続済みを返すstubは用意していない。

ControlとFrameを分け、画像pixelをControlへ送らない。
Controlの候補はWindows Named Pipe（overlapped I/O、限定ACL、bounded queue、cancel可能）。
TCPとの比較: local peer/processの照合とhandle受渡しに向く。Windows専用でstream packet組立が必要。
JSON RPCとの比較: 現在の固定幅codecは小さく上限検証が容易だが、拡張時に明示的なcodec追加が必要。
同一processとの比較: Runtime crash隔離とStandalone再利用が得られるが、状態同期/非同期編集が増える。
CPU readbackとの比較: GPU共有はGPU→CPU→GPU転送を避けるが、adapterとFence依存が強くなる。

## 通信プロトコル

現在実装したv1 packetはlittle-endianの44byte header + 最大1MiB payload。

| offset | wire型 | 内容 |
|---|---|---|
| 0 | u32 | magic `NEXS` |
| 4 | u16 | version=1 |
| 6 | u16 | MessageType |
| 8 | u32 | payload byte数 |
| 12 | u64 | session nonce |
| 20 | u64 | request ID（通知は0） |
| 28 | u64 | Level revision |
| 36 | u64 | reserved=0 |

未知version/type、reserved非zero、切れたpacket、余分なbyte、過大payloadをcodecで拒否する。
Control Channel実装側は部分受信、切断、queue上限、peer/session照合、timeoutを担当する。
IRuntimeConnection.Submitは接続世代のsession/requestを付与してchannelへ渡す契約とする。
生C++構造体、Entity pointer、ID3D12Resource等をpacketへmemcpyしてはいけない。
共有handleは通常のポインタではなく、正当な相手processに複製したhandleの値として扱う必要がある。

MessageTypeには起動Ready/終了、Resize/Input/Selection/PropertyUpdate、Play/Pause/Stop、
LoadLevel/Error/Log/Snapshot/Reply、FramePool/FrameReady/FrameReleased/RetirePool/PoolRetired/Heartbeatを定義。
**全messageのpayload codecが完成したわけではない。**
今回codecを実装したのはResizeとFrame、Sessionが組み立てるのはInput/Selection。

- Resize: `viewport:u64, generation:u64, width:u32, height:u32`。最大16384、非表示は両方0。
- Frame: `viewport:u64, generation:u64, sequence:u64, slot:u32, readyValue:u64, releasedValue:u64`。
  slotは0..2、ID/世代/sequence/Fence値は非zero。
- Input: `viewport:u64, kind:u32, code:u32, x:f32, y:f32`。floatはbit表現をu32としてencode。
  現Panelは相対mouse move/down/upの入口のみ。keyboard、focus loss、captureの仕様は未完。
- Selection: `count:u32`と`high:u64,low:u64`の組。現Sessionは主選択0..1件の送信入口のみ。
- PropertyUpdate/Reply/Snapshot/LoadLevelはWorld所有の回答後にrevision競合・編集確定仕様とともに追加する。

状態はDisconnected / Starting / Connected / Running / Paused / Stopping / Errorを定義済み。
状態遷移の実装はまだない。想定はReadyでConnected、Play ACKでRunning、Pause ACKでPaused、
Shutdown要求でStopping、Goodbyeとprocess終了でDisconnected、timeout/切断/異常終了でError。
ErrorはEditor全体の終了理由にせず、Viewportの接続だけを停止・再接続する。

## GPU共有の方式案と所有権（未実装・対応範囲の回答待ち）

同一adapterを初期対応とする場合の候補は、SwapChainとは別に作成したSDR Textureの3-slot ring。
既存のHDR PostProcess→SDR Resolveの出力先を専用RTVへ差し替える追加経路を設ける。
BackBufferを共有Textureとして公開したり、UIを含む完成SwapChainをコピーして転送したりしない。
Editor上位へ公開するのはtexture tokenと寸法だけ。COM Resource/Heap/descriptor/handleはGraphics backendが所有する。
UIの画像参照は既存ITextureProviderで解決する。

| 資源 | 作成・所有候補 | 解放条件 |
|---|---|---|
| slot Texture / producer ready Fence | Runtime Graphics | 両processが旧世代を退役し、各GPU参照が完了 |
| consumer released Fence | Editor Graphics、slotごと | Runtimeが参照を閉じ、Editor samplingが完了 |
| 複製したOS handle | 受信側process | OpenSharedHandle成功後、または失敗処理でCloseHandle |
| import側COM参照/SRV | Editor Graphics | Editor queue完了後。descriptorを先に再利用しない |
| frame lease | ViewportSession（非所有資源token） | hostがUI sampling command提出後Release |

各slotに独立したready/released Fenceを用い、別slotの高いFence値で未完了slotの使用を誤って許可しない。
Runtimeはそのslotの前回consumer完了を確認した時だけ書き込む。
Editorが使わなかったframeにもrelease/skip応答が必要。全slot使用中は上書きせずframe生成を抑制する。
producer/consumerは共有TextureをCOMMONで引き渡す。RTV/SRVへの遷移とCOMMONへの復帰はBackendに閉じる。

同一adapter方式の利点はreadback不要で既存描画経路への変更が小さい点。
欠点はGPU同期と世代退役の複雑さ、adapter一致が必要な点。
cross-adapterが必要ならheap/resource flags、layout、対応format、copy経路、性能を別途検討する。
現在のGraphicsAdapterSelectorには相手LUIDの強制選択経路がなく、ここも追加が必要。

## 起動から表示までの想定シーケンス

1. ProcessManagerが信頼したexe path、session nonce、専用endpointを使ってRuntimeを起動。
2. Controlでversion/capability/adapterを照合しReadyを受け取る。未一致はError。
3. Resizeを受けてRuntime Graphicsがoffscreen出力とframe poolを生成。
4. handleを正しい相手processへ複製しFramePoolを通知。EditorがimportとSRVを作成しACK。
5. Runtimeが空slotへResolve、COMMONへ戻しproducer Fenceをsignal。
6. FrameReady通知。Editor hostが完成Fence/世代を検証してframeをAcquire。
7. ViewportPanelはtoken由来の画像だけを表示。hostがUI commandを提出。
8. 提出後にconsumer FenceをsignalしてFrameReleasedを送る。Runtimeは次の書込み前に完了を検証。

ViewportSessionのBeginFrame/CompleteSubmissionはこのhost順序用の入口であり、
現行EditorApplicationからは呼んでいない。未接続状態でGPU資源を生成しない。
GPU queueのWaitを実装する際、異常終了した相手の未signal値へ無期限に依存させないこと。
completed値をCPUで非blocking確認してから提出する方式も候補。UINT64_MAXのdevice removalを完了と扱わない。

## Resize / 切断 / crashの想定シーケンス

Resizeはviewport IDとgenerationで識別する。古いpoolを即破棄しない。
Editorは旧leaseを最後のsampling提出まで保持、Runtimeは新世代poolを別に作成して公開する。
新pool import ACK後にframeを切替え、旧世代はRetirePool→両queue完了→PoolRetiredで回収する。
Resizeの連打は最新サイズへ集約し、退役pool数にも上限とtimeoutを設ける。
サイズ0は非表示/最小化として生成を止め、復帰時に新世代で再開する。

切断では新規frame取得を止め、入力状態を解除し、期限付きShutdownを試みる。
Runtime crashではprocess終了/pipe EOFを検出してErrorへ移し、Editorは動作を続ける。
Editor crashではRuntimeがheartbeat/EOFを検出し、共有poolを自身のqueue完了後に閉じる。
相手のFenceが永遠にsignalされない場合、UI threadや共有queueを無限待機させない。
device lossではpoolを無効化し、device再生成と再handshakeを経て新nonce/世代を使う。
再接続で過去のEntity応答やFrameReadyを適用しない。必要ならLevel全体snapshotから再同期する。
これらの復旧動作は設計案であり、実Channel/GPU hostの実装と検証が必要。

## 静的確認と次の作業

実施したコマンド:

```powershell
./Project/Tools/ValidateEditorFoundation.ps1
git diff --check
```

静的scriptはMSBuild XML、compile source存在・重複・登録、RuntimeのUI include、
PanelのDX12 API混入、Detailsの具体Component依存、辞書形式・キー重複・英日対応を検査する。
91英日キーでPASS。git diff --checkも問題なし。
コンパイル成功、link成功、Editor起動、Docking操作、日本語font描画、保存復元、Undo、IPC/GPU動作は未検証。
生成物や既存実行中processは操作していない。

次は確認事項の回答に基づき、World所有と対応GPU範囲を確定し、
process別composition root、Runtime dispatcher、Control Channel、Graphics共有Backendを実装する。
その後に実Runtime frame表示、keyboard/input capture、revision付きproperty ACK、log転送、
Play/Pause/Stop、timeoutとcrash復旧、Standalone、複数Viewportを接続する。
実行可能になってから、ユーザーがビルドを許可した段階で対話/GPU検証が必要。
