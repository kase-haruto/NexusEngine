# NexusEngine 1.4.2

2026-11-18 / Windows x64 / Release / MT / v145。

## 追加・改善

- Add Componentを専用のComponent Browserへ変更。名前・型名・説明・カテゴリ・検索語で検索し、カテゴリ別のアイコン付きカードから追加できます。Engine / Native / C# / Pluginの登録情報を同じRegistryから列挙します。
- Componentの表示MetadataとC# ComponentEditor Attribute。お気に入りは個人設定へ保存し、Registryの変更世代で一覧キャッシュを更新します。
- Detailsの「Componentを解除」をEngine・Native・C#共通に拡張。複数Entityからの解除、C#のinstance指定、Undo/Redo、失敗時の一括復元に対応し、Name / Transformなどの必須型は保護します。
- Content Browserの空白・フォルダ・ソースの右クリックから、C# Script / C++ Behaviourを作成できます。既存Moduleの生成・登録経路を使い、成功時にSource一覧を更新し、コードエディタで開けます。
- Input Map。操作名へキーボード・マウス・XInputの複数入力を割り当て、Button / Axis1D / Axis2DをC++ / C#で読み取れます。EditorのInput Map画面、Project設定の保存、Gameplay / Menu Context、focus・切断時の解除に対応します。
- UV繰り返し倍率・オフセットをMaterialのTextures欄へ追加。通常描画・Mesh Shader・DXR・ベイク照明へ反映し、UndoとLevel保存に対応します。Transformの拡大に合わせて倍率を手動調整できます。
- Componentの追加・アイコンPNGの配置／変更・解除の開発ガイドを追加しました。Source作成とInput Mapの使用方法もSDK文書に含めます。

- 低い太陽角度で物体表面に出るShadow Mapの縞を補正。遮蔽物の影を維持するHardware / WARP回帰テストを通過しました。

## 互換性・移行

Project host / Game Module / GameScriptsを1.4.2で再ビルドしてください。Runtime TypeDescriptor、描画定数配置、公開Input Facadeの実装が更新されています。標準Nexus.GameModule.propsがPublic/Nexus/GameInputActions.cppをGame DLLへ組み込みます。独自ビルドではこのcppもコンパイルしてください。

UV保存により、新しく保存するLevelはNEXUS_LEVEL 15です。既存v1〜14を読み込み、UVは既定の倍率1,1・offset0,0で維持します。v15のLevelは旧エンジンへ戻して読み込めません。移行前のLevelを保持してください。既存asset・Projectのengine関連付けは自動変更しません。

Input MapはProjectのConfig/InputMap.settingsへ保存します。設定がないProjectは既定の割り当てを使います。Play中の定義編集は無効で、保存後の次回Playから反映します。Context切替は次の入力frameで反映し、切替前から保持していたボタンを新操作のPressedとして再発火しません。

## 制約

Input Mapは最大32操作・各32割り当てです。プレイヤー別の再割り当て保存、複数Contextの優先順位・入力消費、長押し／ダブルタップ、相対マウスロック、振動は今回の対象外です。ゲームパッドは従来と同じXInputの最大4台です。

ソース作成には対応する言語のGame Moduleが必要です。C#はBuild / Reload、C++の新規型はBuild / Editor再起動が必要です。UV倍率のTransformへの自動追従やworld-space投影は追加していません。

PNGアイコンはEditor起動時に読み込みます。Live++ SDKを使った実接続は今回の環境では未検証です。任意GPU・全解像度・全利用者Projectの網羅検証は行っていません。検証結果と配布識別はRelease-1.4.2-Verification.mdへ記録します。
