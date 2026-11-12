# Native Moduleの明示状態保存・復元

## 契約

`Public/Nexus/NativeGameState.h`のSave/Restore exportを対で実装したModuleだけが明示値snapshotを使う。既存Moduleで両方未実装なら、従来の固定layout state保持契約を維持する。一方だけのexportは準備失敗とする。

Saveは所有threadの更新停止境界で純粋な値採取を行う。容量0で必要byte数と非0のschemaを返し、次の呼出しで同じschema/長さの内容をEngine所有bufferへコピーする。上限は1 MiB。生のC++ object、pointer、allocator、GPU資源、delegateをdumpしない。

Restoreはパッチ適用後のexportを再解決して呼ぶ。Moduleが自分で定義したschemaを検査し、明示したゲーム値だけを復元する。固定Module/System rootのアドレス・layout契約や、Live++のSource/header互換性検査は緩めない。任意C++ layout変更やGPU pointer graphの自動移行を保証するAPIではない。

## 順序とSystem所有権

1. 既存GameRuntimeがframe境界を取得し、Native/Managed更新を止める。
2. Source、固定root契約、Component Metadataの互換性を確認する。
3. ModuleのSaveで値だけをコピーする。
4. 既存NativeGameSystemScopeがSystem/Bindingを停止する。
5. Live++ Backendが適用結果を確定する。
6. 固定rootとModule identityを再検査する。
7. 既存ScopeがBinding停止を保ったままRestoreを呼ぶ。このstack内だけ、Moduleは既存Systemのfind/accessで保存値を戻せる。System登録変更や通常Updateは許可しない。
8. Bindingを再登録・世代更新し、全成功後に実行を再開する。

System実体の所有・更新は既存Schedulerである。Snapshot bufferだけをLoaderが所有し、呼出し終了後にDLLが借用pointerを保持してはならない。新しいSystem Manager、TypeRegistry、DLL交換Backendは追加しない。

Compile非適用ではsnapshotを捨て、Restoreを呼ばず現行stateを再開する。Save失敗もパッチ適用前に拒否する。適用後のRestore失敗は実行停止を維持し、Consoleへ通知する。失敗したRestoreは破棄可能なstateを残す契約であり、自動rollbackは約束しない。固定layout違反は従来のDLL pinと再起動要求を維持する。

## 例と検証

Native Moduleテンプレートは`updateCount`だけを明示保存する例を持つ。API関数表・World token・System scopeを丸ごと保存しない。LevelのECS値は既存Serialization/Scene snapshotの責務のままである。

Native fixtureは呼出回数のゲーム値を保存し、Restore中のSystem同期借用で戻す。NativePatchProtocolに、非適用時はRestoreしないこと、成功時の保存/復元回数、Save失敗時の現行実行保持を追加した。実テストとLive++実パッチの最新結果はReleaseの実行記録で報告する。

Level遷移は別の操作で、Game stateを新しく作り直す。コードだけを短いleaseで保持してから同じDLLへ新Worldを接続する。Live++の関数パッチとDLLのUnload/Reloadを同じ状態移行として扱わない。
