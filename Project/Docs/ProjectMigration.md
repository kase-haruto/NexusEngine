# 明示的なProject Engine変更と復旧

## 目的と責務

Projectが要求するEngineを、レビューした計画に基づいて変更する。解決失敗から自動実行せず、既定Engineも流用しない。`ProjectMigration`が準備・適用・復旧を担当し、`ProjectMigrationPlan`はレビュー時の前提と確認事項だけを保持する。Launcherの表示状態は`LauncherMigrationPanel`へ分離し、ファイル処理をUIに持ち込まない。

今回の移行は**DescriptorのEngine要求の変更**である。Asset・Level・Sourceの変換やC++ API修正は行わない。汎用コンバータを先に導入すると、まだ定義されていない形式変更を成功したように扱う危険があるため、現在は対応形式の事前確認、明示適用、再ビルドと復旧を選んだ。将来の形式コンバータは、この責務を分けたまま追加する。

## 互換性情報

選択EngineのSDKは`Build/Nexus.Compatibility.json`を提供する。Installedではこのファイルもinventoryで照合する。古いSDKに情報がなければ移行を拒否し、そのSDKを自動修正しない。通常のProject起動にこの情報を後付け必須にはしていない。

| 項目 | 現在の宣言と検証 |
| --- | --- |
| Descriptor | schema 1との一致 |
| Level | NEXUS_LEVEL 1〜12のheader。起動Levelに加えてLevels内の`.level`/`.json`を確認 |
| Asset | loose-files-v1。ファイルを維持し、個々のimporter互換性は移行後に確認 |
| C++ | rebuild-required。API・Game component codecの互換性は保証せず、新版でビルドしてロードを確認 |

Header範囲が合うことだけでGame固有Componentや描画の互換性を証明したとは扱わない。非対応Descriptor/Asset宣言、範囲外Level、リンクされたLevelエントリ、不正配置は適用前に拒否する。

## レビューと適用

PrepareはProjectを変更せず、変更前後のEngine、Descriptor SHA-256、Target manifestと互換性情報のhash、Level集合と各内容hashを記録する。旧Engineは未配置でもよく、移行先は実際に検証できる配置が必要。

ApplyはDescriptorと同じファイルmutexで直列化し、現在の配置と保存形式を再確認する。Descriptor・Level・Target Engineがレビュー時から変わっていれば新しいレビューを要求する。復旧用ファイルをすべて書いてから、Engine要求だけを原子的に置換する。既存のLevel/Asset/Source/Configには書き込まない。

バックアップは通常`Build/Migrations/<uuid>`へ保存する。Buildを共有データに使う場合は、Intermediate、Cache、Generated、.vsの順で、Descriptorの共有パスと衝突しない成果物Directoryを選ぶ。Root外のjunction等へは逃がさない。新しいuuid Directoryには次を保持する。

```text
Original.nxproject   # 元ファイルの正確なbytes
Proposed.nxproject   # 新しいEngine要求のDescriptor
Recovery.json       # Project UUIDと変更前後のhash
```

途中失敗では旧Descriptorを保持する。残った復旧ファイルは自動削除しない。適用中にProjectが開かれていることを前提にしないため、レビュー画面でEditor/Runtimeを閉じることと再ビルド・互換性確認を明示する。

## Launcherでの操作

Recentの`Change Engine...`から移行先を明示選択し、`Review compatibility`で確認する。変更前後と制約を読み、Projectを閉じて再ビルドすることを確認してから`Apply reviewed Engine change`を押す。適用だけで新版のビルド成功を表示しない。RecentのOpenで選択した版のGameホストをビルド・起動する。

復旧記録を画面に表示する。`Browse recovery...`から以前のRecovery.jsonも選べる。`Restore original Engine requirement`はEngine要求を元へ戻すが、後から編集されたDescriptorは上書きしない。復旧後は旧Engineで再ビルドする必要がある。

`--review-project <file> --review-target <manifest>`はレビュー画面を開く起動方法。targetは登録済みの有効配置に限り、別Engineへ置き換えない。この引数で適用は行わない。

## CLIでの操作

```powershell
$tool = './Generated/Modules/x64/Debug/NexusProjectTool/NexusProjectTool.exe'
& $tool migration-plan 'D:/Games/MyGame/MyGame.nxproject' 'D:/Engines/0.1.3/NexusEngine.nxengine' 'D:/Games/MyGame/Build/Review.json'
# 表示された変更前後・確認事項とplanの内容をレビューする。
& $tool migration-apply 'D:/Games/MyGame/Build/Review.json' --accept-api-risk
# 出力されたRecovery.jsonを指定する。
& $tool migration-restore 'D:/Games/MyGame/MyGame.nxproject' 'D:/Games/MyGame/Build/Migrations/<uuid>/Recovery.json'
```

計画の保存先の親Directoryは事前に作成する。計画はマシン固有の配置を含む作業用成果物なのでBuild等のGit除外先へ保存する。共有DescriptorへEngineの絶対パスは追加しない。

## 復旧と移動

復旧はProject UUID、Originalのhash、適用後Descriptorのhashを照合する。適用後に別の編集があれば拒否する。旧EngineやLevelがなくてもDescriptorを復元できる。Projectを移動した場合は移動先のDescriptorと、その中の移動済みRecovery.jsonを指定する。記録は絶対Project Rootを含まない。すでに元の正確な内容なら再復旧は無変更で成功する。

復旧はAssetやC++コードの後の編集を巻き戻す機能ではない。複数回のEngine変更は、それぞれの記録を新しいものから復旧する。復旧を終えたからといって既存実行ファイルを旧版用として認めず、Build stamp照合と再ビルドを維持する。

## 検証と拡張

2026-11-01 Debug/x64で次を確認した。

- `ProjectMigration.Tests.ps1`：計画で無変更、明示確認なしの拒否、外部Descriptor/Level編集の拒否、範囲外Level、旧Engine未配置、新版でのGameビルド、復旧後の旧版ビルド、後の編集・壊れたバックアップ・Root外復旧の拒否、移動後の正確かつ冪等な復旧、共有パスとバックアップの分離。
- `InstalledEngine.Tests.ps1`：inventoryが正しくてもDescriptor/Asset/Levelの契約が非対応ならレビューを拒否する。専用SDKコピーだけを変更して検証する。
- `LauncherInteraction.Tests.ps1`：96 DPIの専用非表示WindowへWin32入力を送り、Game作成・Recent登録、レビュー画面の確認、Engine変更、復旧を実行し、実際のファイル内容で検証する。

非表示検証では実デスクトップのcursorによるWM_MOUSELEAVEだけを抑制し、合成した入力位置が無効にならないようにする。通常の表示Windowの離脱処理は変更しない。pixel座標の操作テストは現在の96 DPIレイアウト用であり、別DPIや全クリック操作の成功を証明しない。

将来はEngineごとの形式変換器、API変更の診断、Game Moduleのビルド検証、Source Controlとの連携を追加できる。変換が必要な場合にはDescriptorだけの復旧を流用せず、変更する各データのバックアップとトランザクションを別途設計する。
