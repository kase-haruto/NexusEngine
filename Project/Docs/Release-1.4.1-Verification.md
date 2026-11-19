# NexusEngine 1.4.1 検証記録

2026-11-18更新。Engine Windows x64 / Release / MT / v145。SDKに入る検証文書は発行時点のsnapshotであり、以下の公開後結果はサイトと作業treeへ追記する。

## 入力と保持

HEAD `0aa3b048cf8ae7010ba122762938e964f9dd7f2b` からの未commit変更を含む。GitHubのdistributionタグは配布バッチであり、SDK生成時の作業treeを正確に表すsource tagとは扱わない。基準1.4.0は `Generated/Release141Checks/baseline-1.4.0.json` の79ファイル。最終SDK入力は `source-inputs-r2.json` の210ファイルで、発行後の照合不一致0。両者の差分190ファイルと理由は別途実装・配布報告に記録した。

旧1.4.0のmanifest SHA-256は `ade65a28bb6f12ac0397706ecb87d5f156ca41fd2ca226e0a8e66361a933a493`。旧SDK/ZIP・公開済みLauncherを上書きせず、既存Projectの関連付けを変更しない。Samplesの配布copyだけ1.4.1へ関連付ける。

## 配布識別

- SDK: `Generated/EngineReleases/v1.4.1/Engine-r2`
- buildId: `aa6ac5d5-303d-4133-b549-fddbdb856f79`
- manifest SHA-256: `9dc5260f3e128d50f9680cb4d9f3a96d437e5bd7cf497f1636607532799cfe37`
- SDK inventory: 1976ファイル、hash不一致0・余分0。Mono784ファイルは1.4.0と一致。
- ZIP: `nexus-1.4.1-Release.zip`、186798652 bytes
- ZIP SHA-256: `f416058a00697297b60e7896c9c9e5e111c1287e72baa68b7164753e27c53a6f`
- 最終Launcher: 1.3.2、`Generated/LauncherDistribution/Launcher-1.3.2/Setup/launcherSetup.exe`、177588390 bytes
- Setup SHA-256: `0e13aa2cc5c5b8b29965d77db95361984baec588250551758710ec2711fe5a77`
- 同梱ProjectTool: 7312384 bytes、SHA-256 `db9edf272a01232dc065c9ede13d8e9f49fda77886113544c0ba65a8cbe9ae55`

## 実施した検証

ログroot: `Generated/Release141Checks`。途中の失敗は消去せず、修正後の成功を別logへ記録している。

| 検証 | 結果・log |
|---|---|
| 最終Curve/ECS、Layer/Controller/Root Motion/Timeline/weighted Bezier/property | 成功、phase-q-curves.log |
| legacy clip/event/Animator/Timeline gap/永続化 | 成功、phase-q-animation-system.log |
| 旧playback/crossfade/Pause/Level互換 | 成功、phase-p-animation.log |
| EditorTests全体、文書/History/共通node/preview/PIE/Details | 成功、phase-q-editor-tests.log |
| CPU複数clip/morph/Retarget/pose | 成功、phase-o-animation-assets.log |
| GPU Based Validation / RTX3060 Laptop、TRS・skin・morph・65joint・morph+65joint | 5case成功、phase-o-gpu-animation.log。pixel・影・独立個体・repeat・解除復元 |
| CameraEffect/colour chain/FXAA/resize/失敗時保持 | 成功、phase-n-gpu-color.log |
| 実二process IPC、1MiB超chunk、seek/state、切断復帰・再接続 | 成功、phase-o-ipc.log |
| Installed SDKだけでlinkしたprobe→実RuntimeHost IPC | 成功、sdk-host-ipc2.log。pause/seek x=.25、状態返送、Runtime終了0 |
| Installed inventory/旧SDK共存/改変拒否/移行 | 成功、installed-tests.log、sdk-inventory.json |
| 日本語pathのInstalled C++ Game.Health buildとRuntime/Editor GPU | 成功、installed-gpu.log |
| 実typed CSharp / Hybrid template、HostとManaged/GameScripts.dll、CPU check | 成功、installed-typed-managed-hybrid.log |
| SDK sampleのRuntime/Editor CPU/GPU起動 | 成功、sample-*-cpu.log / sample-*-gpu.log |
| 実Editor画面 | sample描画、Source選択、DetailsのController/parameter/clip/再生/Curve/Layer、Curve/Studio画面を表示し終了。全操作網羅ではない |
| Launcher distribution fixture | 69checks成功。最終1.3.2 payload+native Toolは83checks成功、launcher-payload-native132-retry.log |
| Website | 5tests成功。変更履歴のobject拒否を既存testへ追加 |

## 試行中に検出した問題

最初のSDK試行はProjectBuild.cppのinclude不足で停止し、SDKを発行しなかった。legacy Timeline gapの復帰、Rig/Layer保存時cache更新とclock維持を修正後、Phase P/QとEngine-r2で検証した。

最初のLauncher fixture実行は、この実行環境から子Windows PowerShell5へ渡るPSModulePath不足によりGet-FileHashを解決できず失敗。Windows module pathを設定した再実行で成功した。

Launcher1.3.1の公開後検査で変更履歴のJSON object化を検出した。原因はGet-Contentの付加propertyがJSONへ含まれること。ReadAllTextによる純粋文字列へ修正し、Websiteでobjectを拒否する検査を追加した。公開済みSetupは差し替えず1.3.2を生成し、旧版catalogの説明も文字列へ正常化した。初回不一致を単なるcacheとは扱わない。

## 公開後検証

- 公開GitHub batch: [distribution-57972387f1a5](https://github.com/kase-haruto/NexusEngine/releases/tag/distribution-57972387f1a5)
- サイト: https://nexus-engine.nexus-engine-website.workers.dev/
- 配布更新deployment: cdbcc447-de37-4af1-ad8b-5e80fb1949d3。最終文書反映deploymentはWebsite/deployment-status.jsonへ記録する。
- 公開catalogはSDK8版（最新1.4.1）、Launcher履歴6版（最新1.3.2）。すべてのLauncher changesが文字列であることを確認。
- Launcher実コードのSDK catalog読込と最新1.4.1の取得/hash検査成功、launcher-public.log。
- 公開済み旧Launcher1.3.0 assemblyからLauncher6版の一覧・変更履歴の読込と最新1.3.2 Setupの実取得・サイズ/hash検査成功、launcher-update-public132-retry.log。インストーラー自体は実行しない。
- 旧published asset11件のサイズ/hashを保持、旧SDK7版の識別・サイズ/hashも保持。publication-integrity.json。
- 初回1.3.1の形式不備に加え、1.3.2デプロイ直後には配信反映待ちで不一致があった。反映後の内容は生成catalogと全field一致した。

SDK8版と最新Launcher1.3.2 Setupの計9ファイルを公開URLから全量取得し、サイズ・SHA-256の一致を確認した。verify-public132-retry.logは全9件成功。

## 制約と未検証範囲

default ECS一括実行のSky/Camera fixtureは既存blank templateと不整合があるため、一括全成功とは記録しない。今回の対象別suiteは上記のとおり成功した。

Retargetは明示mappingで、Humanoid自動意味対応ではない。圧縮glTF・8influenceは追加対象外。変形DXR BLASは更新せずShadow Mapへfallbackする。Root Motionはframe区間の加重差分で、連続時間の厳密積分・Dynamic Rigidbodyへの力適用ではない。

Curve100000key/32MiB、mesh morph256MiB/4096target、joint65536を入力検証するが、最大モデルの実用性能・任意GPU・全DPI/解像度・全glTF拡張・任意利用者Projectは未検証。CurveIPCは未保存Level全体の同期ではない。
