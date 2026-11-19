# NexusEngine Launcher 1.3.1

2026-11-17 / Windows x64。Engine 1.4.1と併せて、同梱ProjectToolを更新しました。

ProjectToolはStandalone起動時のCurve preview named pipe指定をHostへ引き継ぎます。Engine 1.4.1のCurve Editorから別プロセスのPlay / Pause / Seekと未保存Curveを確認できます。転送対象はCurveであり、未保存Level全体の同期ではありません。

本体は自己完結型で、本番HTTPSカタログを使用します。既存のSDK・旧LauncherとProjectの関連付けを自動で置換しません。Launcherの管理画面から更新するか、サイトのsetupを取得してください。

検証結果は[Engine 1.4.1検証記録](Release-1.4.1-Verification.md)を参照してください。

変更履歴JSON形式の不備が公開後検証で判明しました。更新一覧の形式を修正した[Launcher 1.3.2](Launcher-Release-1.3.2.md)を使用してください。
