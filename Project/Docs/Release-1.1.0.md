# NexusEngine 1.1.0

## 機能

- 共通ECS/Level/Reflection/Detailsを使用するC++/C#/Hybrid開発。Managed Backendの標準はMono/SGen、既存CoreCLR構成も維持。
- Window由来のInput snapshot、押下/解放edge、focus喪失とGame View入力分離。C++/C#から同じ値を取得。
- Windows XAudio2によるPCM/float WAV再生、Stop/Volume/IsPlaying/Pause、client所有Voiceと終了時解放。
- Jolt Physics 5.6.0。RigidBody/独立Collider、Box/Sphere/Capsule/Triangle Mesh/Convex、Raycast/Force/Impulse、CCD設定、接触snapshotと購読Event。
- 同じScript型の複数instance。永続slot ID、個別Property編集、有効状態、保存、Undo/Redo、Managed Reload。
- Native Moduleが明示実装する保存/復元export。Engine所有の最大1 MiBの値bufferへ保存し、安全なパッチ境界で復元。
- Project Rootを検査するAssetExists、既存LevelSerializerへ接続するLevel切替要求、同じModelAssetLibraryからの描画/物理モデル解決。

## 依存・寿命

Game Public API/Nexus.Managed → GameServices → 既存Level/Scene/Reflection → Engine Backend。Editorは.NET Runtimeを所有しない。言語別ECS/Asset Manager/Inspectorは追加しない。Entityは既存世代付きHandle、Scriptは永続slot IDと実行時token、Voiceとサービスはclient寿命で検査する。

Level生成 → hostのRoot/model source設定 → Native登録/Managed開始 → Simulation/Physics/Transform → Runtime停止/Script解除 → Native API/System登録解除 → solver/Audio/Scene破棄。ReloadはBackend別に処理し、古いManaged参照やNative関数を直接保持しない。

## 互換性・制約

旧LevelのScript slot ID未保存やRigidBody形状は読込互換を維持する。既存1.0.0とProjectのEngine関連付けを自動置換しない。Engine内Component/SDK ABIの変更を含むため、1.1.0へ移行するGame DLL/Hostは対応SDKで再ビルドする。

Physicsの親付きBody、Constraint、Character Controller、1 Entity内の複数Collider、レイヤー編集は未提供。Inputのgamepad/action mapping、Audioの3D spatialization/streamingも未提供。Nativeはpointer/GPU資源の自動状態移行を保証せず、Live++の対応範囲と固定root layout契約を守る。任意の実行状態を自動rollbackしない。

長いProject pathではnet48/MSBuildの生成configなどにMAX_PATH制約が残るため、開発Projectは短いRootを推奨する。MonoのGACコピー自体は拡張長pathに対応した。

開発にはVisual Studio C++ v145、Windows SDK、.NET SDKが必要。Mono Projectは公式Mono x64 SDKをNEXUS_MONO_ROOTまたはNexusMonoRootへ指定する。配布Launcherは自己完結型。今回のカタログはこのPC上のローカル配布であり、HTTPS公開サーバーへのアップロードとは区別する。

## 確認記録

Debug EditorTestsでInput/実XAudio2/Jolt基本物理・Mesh・接触/共通Property編集・保存・Undo・Native明示状態移行/Level切替を検証し、exit 0。実Mono統合で同型複数instance、公開値・Enabled、Domain Reload、Input/Collider/Raycast/Audioを検証しexit 0。

Release SDK発行とInstalled HostのCPU起動検査は成功。配布版RuntimeはRTX 3060 Laptop GPUで10 frame描画して正常終了。実EditorでRigidBody追加/共通Details/保存/Play/Stopを確認した。既存CoreCLR統合回帰もexit 0。Installed SDKからHybridテンプレートを生成し、Native DLL/Mono GameScripts/HostのReleaseビルド、CPU起動、30 GPU frameの描画と正常終了を確認した。Launcher一覧/インストールの結果は配布先の検証記録へ分けて記載する。検証ログは`Generated/GameplayCompletion-1.1.0-13d4bcbd47824bd884f0331df9d7f703`。

APIの使い方は[GameApiReference](GameApiReference.md)、[RuntimeServices](RuntimeServicesREADME.md)、[Jolt Physics](JoltPhysicsREADME.md)、[Native状態](NativeGameStateREADME.md)、[複数Script](MultipleScriptsREADME.md)を参照。
