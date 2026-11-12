# 複数Scriptの実行確認 — 2026-11-12

ユーザーの「そちらで確認を行って」という指示により、それまでの静的確認から実Build/実テストへ進めた。今回の実行でコード修正は発生していない。以前のREADMEの「未実行」は実装時点の記録であり、現在の確認範囲は本書で区別する。

## 結果

| 対象 | 結果 |
|---|---|
| Native Runtime / NexusMonoGameplayTests Debug x64 | Build exit 0、警告0、エラー0 |
| Nexus.Managed / Bootstrap、Mono net48 | Build exit 0、警告0、エラー0 |
| Mono fixture V1 / V2 | 両Build exit 0、警告0、エラー0。別の中間/出力directoryで生成 |
| 実Mono統合テスト | exit 0、最終PASSを確認 |
| 既存CoreCLR Gameplay回帰 | Build exit 0、警告0、エラー0。実行もexit 0、最終PASSを確認 |
| 共通EditorTests | ユーザーから共有された最終PASSとexit 0を確認。本検証でエージェントは再実行していない |

環境はWindows x64、MSVC 14.51.36231/v145、.NET SDK 10.0.402。Mono実体はGenerated/MonoMigration/SDK/Monoを使用し、`mono.exe --version`で6.12.0/amd64/SGenを確認した。CoreCLRは既存host pack 10.0.13のnative directoryをBuild propertyで明示した。

## 実行した確認

MonoのNativeテスト本体、最新API/Bootstrap、V1/V2のGame Assemblyを現在のsourceからBuildして実行した。古いfixtureを新機能の証明として利用していない。

- 同じEntityへ主Scriptと第二Scriptを配置し、最初のUpdateでX=3、Y=4になる。
- 第二Scriptの公開Lift=4がOnCreateより先に復元される。
- C#のGetScriptsが二件を返し、同じ型へのAddScriptが二重登録しない。
- 第二ScriptのEnabled=falseが主Scriptを無効化しない。
- 各Scriptのinstance IDが異なり、Domain Reload後も両方を新IDで再生成する。
- Reload後も主ScriptのSpeed、第二ScriptのLift/無効状態を維持し、V2更新でX=9、Y=4になる。
- 不在AssemblyへのReloadは失敗し、現行コードが次のUpdateでX=15まで動作する。
- StopでScriptSystemを解除し、同じProcess/Worldで再Start/Stopできる。
- Mono child Domainの生成・解放、誤thread/別Assembly entryの拒否を既存テストで確認する。

Mono実行の最終行：

```text
PASS: actual Mono hosting, Domain unload/restart, shared ECS, lifecycle, reload and state restoration.
```

CoreCLRは既存NexusManagedGameplayTestsを現在のAPI/Bootstrap/fixtureでBuildし、共通ECS、Lifecycle、Transform順序、Pause、解除/再アタッチ、自己破棄、生成、例外分離、遅延Stop、編集/Play分離、共通Serialization、既存Reload負例を含む回帰経路を実行した。これはMonoの複数Script試験とは別の確認で、CoreCLRで同じ二Script fixtureを実行したという意味ではない。

```text
PASS: common ECS, lifecycle, Transform order, pause, detach/reattach, self-destroy, spawn, fault isolation, deferred Stop, edit/Play isolation and common serialization.
```

## ログと再現手順

ログRootは`Generated/ManualGameplay-Agent-6eba399b19204031a21f35ec2ef93cd3`。ユーザーの手順にある[test guide](GameplayTestingREADME.md)と同じProject/SDK設定を用いた。MSBuildはVisual Studio 18 CommunityのMSBuild.exeを絶対指定した。

| 記録 | 内容 |
|---|---|
| [MonoTests.build.log](../../Generated/ManualGameplay-Agent-6eba399b19204031a21f35ec2ef93cd3/MonoTests.build.log) | Native Runtime/テストのBuild |
| [MonoBootstrap.build.log](../../Generated/ManualGameplay-Agent-6eba399b19204031a21f35ec2ef93cd3/MonoBootstrap.build.log) | Mono API/BootstrapのBuild |
| [MonoFixtureV1.build.log](../../Generated/ManualGameplay-Agent-6eba399b19204031a21f35ec2ef93cd3/MonoFixtureV1.build.log) / [V2](../../Generated/ManualGameplay-Agent-6eba399b19204031a21f35ec2ef93cd3/MonoFixtureV2.build.log) | Reload前後のAssembly Build |
| [MonoTests.run.log](../../Generated/ManualGameplay-Agent-6eba399b19204031a21f35ec2ef93cd3/MonoTests.run.log) | 実Monoの実行結果 |
| [CoreClrTests.build.log](../../Generated/ManualGameplay-Agent-6eba399b19204031a21f35ec2ef93cd3/CoreClrTests.build.log) | 既存CoreCLR経路のBuild |
| [CoreClrTests.run.log](../../Generated/ManualGameplay-Agent-6eba399b19204031a21f35ec2ef93cd3/CoreClrTests.run.log) | CoreCLR回帰の実行結果 |
| [Verification.json](../../Generated/ManualGameplay-Agent-6eba399b19204031a21f35ec2ef93cd3/Verification.json) | 終了コード、環境、Mono成果物4件のSHA-256、未確認範囲 |

Generated内のログと成果物はローカル検証記録であり、Source管理/別PCへの同梱を保証しない。再実行時はテスト手順の新規directoryを使う。

## 未確認範囲

実Editor上での各ボタン/drop/ソースを開く/保存/Reloadの全操作、Release、クリーンPC配布、長時間/大量Entity性能は今回確認していない。共通EditorTestsの成功はユーザーから提供された証拠として扱い、実GUI操作の証明と混同しない。任意Native state移行、Input等の未実装Backend、同型Scriptの複数instanceは対象外。
