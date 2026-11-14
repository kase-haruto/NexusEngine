# NexusEngine 1.2.0

Windows x64 / Release。エディタの操作・配置と空の環境設定を更新しました。

- ツールバー、Viewport、Detailsのコンポーネント選択をテクスチャアイコンに対応。未登録のアイコンは文字表示を維持します。
- ViewportにTRSツールシェルフとWorld/Local、スナップ、視点、表示オプションを配置。有効状態の色分け、折りたたみ、矢印から開く詳細ポップアップに対応しました。
- ヒエラルキーをエンジンのスタイルに合わせた交互行表示に変更。エンティティ作成を右クリックメニューへ移しました。
- ツールバーのボタン配置を調整・保存でき、アイコンと配置のガイドをエディタから確認できます。
- タイトルバーのNXロゴとNexusEngine名の間にメニューを統合。狭い幅では省略メニューを表示します。
- Sky Atmosphere / Sky Light / Exponential Height Fogで固定時刻の空・環境光・高さフォグを設定可能。Viewportの環境アイコンから選択できます。
- ランチャーのプロジェクト一覧・削除操作と同梱ProjectToolを更新しました。

アイコンの差し替えは配布SDKの `Data/Resources/Editor/Icons` を参照してください。ファイル名と配置のガイドは同フォルダーのREADME、およびエディタのヘルプにあります。

既存1.0.0 / 1.1.0 / 1.1.1は引き続き利用できます。既存プロジェクトの関連付けは自動更新しません。1.2.0へ移行するHost / Game DLLは、対応SDKで再ビルドしてください。環境コンポーネントと描画用データの更新を含みます。

開発環境はWindows x64、DirectX 12対応GPU、Visual Studio C++ v145、Windows SDK、.NET SDK。Monoプロジェクトには公式Mono x64 SDKが必要です。空の時刻は手動設定で、自動昼夜サイクルではありません。ゲームパッドは引き続きXInput最大4台です。振動、Action Mapping、物理Constraint / Character Controller、Audioの3D空間化・ストリーミングは未提供です。

検証結果は公開サイトのRelease-1.2.0-Verificationを参照してください。全GPU・DPI・解像度・コントローラーの実機操作は保証していません。
