# Live++ 2.11.4 API静的調査

2026-11-10、[既存の取得元記録](LivePP.UPSTREAM.md)にある公式SDKを取得し、ZIPのSHA-256を照合した。
取得元は[公式リリース](https://liveplusplus.tech/releases.html)の2.11.4リンク。バージョン・API・配布物は変更していない。

- ZIP SHA-256: `ACCCFE7C4E4D26D7CEBFBEC7A1DAC690E9F750F53E49F83CBFC9E526439BF921`
- 調査配置: `Generated/DependencyAudit/LivePP-2.11.4/SDK/LivePP`（Repository Root相対）
- version_x64_CPP.txt: Windows C++ 2.11.4、2026-08-11リリースの記載を確認。
- 341件のZIP entryが調査配置外へ出ないことを検証して展開した。
- SDK本体はGenerated配下のGit除外領域。API・Agent・Broker等を起動していない。
- EULAは配布物のEULA/LPP_EULA.pdfにある。ライセンス有効化・認証情報操作は行っていない。

通常のThirdParty/LivePP配置やLivePP.local.propsを変更していないため、従来EXEの自動有効化条件は変わらない。
利用する場合は調査配置の絶対パスを明示的なLivePPDirectory / NexusLivePPDirへ指定する。

## 照合結果

| 利用箇所 | 実SDKで確認した項目 |
|---|---|
| LiveCodingAgent | LppSynchronizedAgent、生成・検証・破棄、EnableModule / DisableModule、ScheduleReload / WantsReload / Reloadの名前と署名 |
| SDK options | MODULES_OPTION_NONE、SYNCHRONIZE_WITH_COMPILATION_AND_RELOAD、WAIT_UNTIL_CHANGES_ARE_APPLIEDの列挙名 |
| NativePatchHooks template | pre / post-patch、Compile Error、Link Errorのhook型・引数と登録macro |
| SDK preferences | preBuild設定、停止中ProcessのCompile / Link / HotReload hookを制御する3つのbool |
| Disable契約 | 戻り値はvoid、filter callbackは完了callbackではない。同期完了・pending patch破棄APIがあると推測しない |

SDK同梱Docs/x64/documentation.htmlは、停止中Processでhookを許可した場合、receiver Threadから呼び、他Threadのmutex保持でdeadlockしうると説明する。
今回Module-only profileで3つの停止中hook設定を明示falseにした。SDKの既定値もfalseである。
通常実行時のhook Threadや解除完了・実パッチ成功を、ヘッダー照合だけで実証したとは扱わない。

| APIファイル（API/x64相対） | SHA-256 |
|---|---|
| LPP_API.h | 2040FFA585124C7CD04C810D64C14DC99ED2059E4A068F78FD2DED30CAF185CF |
| LPP_API_Hooks.h | B027330157B26A47E11321A985CAEDCDC8CD783CBAFEEB22E7D64EEA3D1F1FAB |
| LPP_API_Preferences.h | D78F4C96230A08AA942151718EC18B4778A86FC91CF043E6AD5C97D0D04AC8A2 |
| LPP_API_Options.h | EDF1B46433614007610DA99B2D3EDD84481F516C65439504D3CF468045C37563 |
| LPP_API_Version_x64_CPP.h | 075265C6252F52A3BB674AD5DFFDFB9CBCAC6B9DA33CC57C2849BA9B749760EA |

SDK API実体未確認の制約は解消した。コンパイル・link・ライセンス接続・SDK macro展開・hook Thread・実パッチの未検証は継続する。
