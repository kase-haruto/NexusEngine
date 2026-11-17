# NexusEngine Launcher 1.3.0

2026-11-16 / Windows x64

## 新機能

- 新規プロジェクト作成を、Engine Version → 画像付きTemplateカード → 選択内容の拡大Preview → 名前・保存先 → Create Projectの流れに変更しました。
- EmptyはEntityなし、Basic LevelはCameraとDirectional Lightだけを生成します。Demo Script / Actor / Component、Cube、Floor、Sky、Sample Assetは追加しません。
- Engine選択にVersion、Installed / Development、Build configuration、既定EngineのCurrent状態を表示します。
- カード全体をクリックでき、Tab・Enter・Spaceでの選択にも対応します。選択状態は枠とチェック、Hoverは背景で区別します。
- Template Metadataと初期Level、静的Preview画像を独立した資源として同梱しました。インストール済みSDKの内容を変更せずに利用できます。
- Metadataや初期Levelが壊れたTemplateは作成不可とし、正常なTemplateの利用を維持します。画像欠落・破損・未対応Preview Sourceでは代替表示します。

## 整理

- 配布用WinForms Launcherへ統一し、旧C++版Launcherをソースと今後のSDK発行処理から削除しました。既に公開された旧SDKの内容は変更しません。
- C++ / C# / Hybridのコード生成例はSamplesへ分離しました。新規作成画面には表示しません。
- Gameplay以前の起動検証用だったManaged Bootstrap Templateと未使用資源を削除しました。
- 旧Empty / BasicのHealthデモコードは配布対象から除外し、既存回帰テストのFixtureに限定しました。

## 更新方法

既存Launcherの「ランチャーの管理」→「ランチャーのバージョン一覧を取得」から1.3.0を選び、「選択したランチャーをインストール」を押してください。WebサイトのlauncherSetup.exeからも更新できます。

## 互換性と制約

- Launcherのみの更新です。エンジンSDKの版数・内容、既存プロジェクトのEngine関連付けは変更しません。旧LauncherとSDKを保持します。
- Template ID `blank` / `basic-3d`を維持しています。表示名はEmpty / Basic Levelへ変更しました。
- Empty / Basic LevelからGameコードや初期Moduleを生成しません。従来のDemoや型登録例が必要な場合はソース側のSamplesを使用してください。既存プロジェクトのSourceは削除しません。
- EmptyはCameraもありません。単体Runtimeは既存のCamera必須条件を維持するため、実行前にユーザーがCameraを配置してください。
- PreviewはHierarchyとViewportを示す静的な構成図です。リアルタイムRuntime PreviewやEditorによるthumbnail自動生成は今回含みません。
- スクリーンショットによる画像確認は行っていません。生成ファイル、画像デコード、UIコントロール操作、ビルド、破損時の処理で検証しています。全DPI・任意のユーザープロジェクトは未検証です。
