# NexusEngine 1.2.1

Windows x64 / Release。1.2.0で新規C# / Hybridプロジェクトのビルドが `Set NexusMonoRoot or NEXUS_MONO_ROOT` で停止する配布上の不具合を修正しました。

- 公式Mono 6.12.0.206 x64 / SGenのRuntime、BCL、GAC、設定とライセンスをInstalled SDKへ同梱します。
- プロジェクトの初回ビルド時にEngine同梱Monoを検出して配置します。利用者によるMono SDKの別途インストールや環境変数の設定は不要です。
- Monoの同梱漏れはSDK発行時と配布ZIPの生成時に検出します。
- 明示した `NexusMonoRoot` / `NEXUS_MONO_ROOT` と、既存プロジェクト出力に配置済みのMonoを使う経路は維持します。

1.2.0のエディタ、アイコン、Viewportツール、空・環境光・高さフォグ機能を引き継ぎます。既存の公開版とプロジェクトの関連付けは保持します。ランチャーで1.2.1をインストールして新規プロジェクトの使用エンジンに選択してください。既存プロジェクトを移行する場合は、1.2.1のSDKでHost / Game DLL / C#スクリプトを再ビルドしてください。

開発環境はWindows x64、DirectX 12対応GPU、Visual Studio C++ v145、Windows SDK、.NET SDKと.NET Framework 4.8参照アセンブリです。Monoの同梱はC#コンパイラや.NET SDKの代わりにはなりません。従来のCoreCLRプロジェクトの依存環境は変わりません。

ゲームパッドはXInput最大4台です。振動、Action Mapping、物理Constraint / Character Controller、Audioの3D空間化・ストリーミングは未提供です。全GPU・DPI・解像度・周辺機器での実機操作は未検証です。今回の確認結果は [Release-1.2.1-Verification](Release-1.2.1-Verification.md) を参照してください。
