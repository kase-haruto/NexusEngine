# Editor基盤調査（2026-10-16）

ビルド・実行は依頼に従い実施しない。調査開始時の `git status --short` は空。
既存コードはC++23 (`std::expected`) を使用しており、C++20への設定変更はしない。

| 領域 | 既存実装 | 再利用 / 不足 |
|---|---|---|
| 起動 | main → NexusFramework → EditorApplication / PrimitiveRenderer | 現在は単一exe。プロセス別のcomposition rootが必要 |
| GUI | Editor/Gui/Core、Widgets、ImGuiRenderer | Docking有効、日本語NotoSerifJPをInterへmerge済み。Panel管理は未実装 |
| ECS | Runtime/Scene、型別dense storage、世代付きEntityId | そのまま使用。Editor独自Worldは追加しない |
| Level | Level / LevelDocument / LevelSerializer v3 | 永続ID、階層、保存を再利用。DocumentのImGui依存を除去する |
| 階層 | HierarchyComponent / TransformSystem | SetParentが循環と不正参照を検証する。UIでは親子を二重所有しない |
| 選択 | EditorSessionの単一PersistentId | 複数IDサービスへ分離が必要 |
| Reflection | なし。InspectorはTransformを直接参照 | Runtimeの型記述とEditorのDrawerを新設する |
| ログ | Foundation/Logging、6段階、同期Logger、ILogSink | 上限付きコピーSinkを追加。string_viewを保存しない |
| Asset | ModelLoader / ModelAssetData / ModelResource、固定assetDirectory | AssetDatabaseなし。初期Browserはファイルカタログと明示する |
| 設定 | GraphicsSystemDesc / 個別描画設定、Levelの独自JSON serializer | 共通JSONライブラリなし。User/Editor/Projectの保存契約が必要 |
| Viewport | SwapChainへHDR PostProcessの結果をResolve、UIをその後描画 | Scene Panel用Textureなし。SwapChainは共有しない。専用出力先が必要 |
| GPU | GraphicsSystemのPImpl、CommandQueue、frame fence、DescriptorAllocator | 共有Texture/Fence/IPCはなし。Backendに閉じた追加が必要 |

Runtime/RenderingはGraphicsの値snapshotへ依存するがEditorには依存しない。
例外はLevelDocument.cppのImGui UIであり、削除してEditorへ移す。
既存の光学・影・PostProcess診断は同一プロセスのRenderer制御である。
別プロセスへ移す際はそのままPanelから呼び出せず、対応する制御メッセージが必要。

## 確認事項

編集Levelの所有者（Editorの編集WorldかRuntimeのみか）と、初期GPU共有の対応範囲
（同一ユーザー・同一adapterかcross-adapterも必要か）はユーザーへ確認中。
この選択に依存するWorld同期・プロセス接続は回答後に確定する。
