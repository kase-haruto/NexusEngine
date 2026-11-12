# Mono Runtime

- 公式 Windows x64 SDK: Mono 6.12.0.206 / SGen。
- 配布元: https://download.mono-project.com/archive/6.12.0/windows-installer/mono-6.12.0.206-x64-0.msi
- 確認した MSI SHA256: `4125F57D97CFA88257915EDC969E913DE198CD8E22396A29849037479A0AC368`。
- LICENSE.txt / PATENTS.txt は https://github.com/mono/mono/tree/mono-6.12.0.206 の原文。
- EngineはSDKのbinaryを変更しない。`Deploy-MonoRuntime.ps1` が指定SDKからRuntimeを配置し、本ディレクトリの通知を `Mono/Notices` へコピーする。
- SDKのインストールやPATH変更をEngineは自動実行しない。SDKの取得・更新は開発環境の責務とする。
- Embedding API: https://www.mono-project.com/docs/advanced/embedding/

Mono本体以外のNuGet依存は各package固有のライセンスに従う。Mono SDKのProfile全体を配置するため、再配布時には本通知とLICENSE全文を保持する。
