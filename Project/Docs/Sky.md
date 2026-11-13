# 空と固定時刻の環境設定

Unreal EngineのSky Atmosphere / Sky Light / Exponential Height Fogの役割分担を参考にした環境設定です。時刻はレベル制作時に手動指定する固定値です。実時間・OSの時計との同期、自動進行、昼夜サイクル、ゲーム中の時刻操作UIはありません。PIEやゲームのUpdateでも保存した時刻を維持します。

## 設定方法

1. 作成メニューの「空を作成」を選びます。大気、環境光、高さフォグをまとめて追加します。新規Blank / Basic3Dレベルにも設定済みです。
2. Propertiesの **Sky Atmosphere（大気）** で「時刻（0～24時）」を設定します。6時が日の出、12時が正午、18時が日没、0時と24時が深夜です。17.5は17時30分です。
3. 「太陽の方位角」で太陽の軌道をY軸まわりに回転し、「太陽光の強度」「空の強度」で直接光と背景を別々に調整します。
4. **Sky Light（環境光）** の強度と **Exponential Height Fog（高さフォグ）** の密度・高さ減衰を調整し、レベルを保存します。編集はUndo/Redo、保存・復元、PIEの複製に対応します。

ビューポートには大気（惑星と大気層）、Sky Light（ドームと下向きの光）、高さフォグ（霧の帯）のアイコンを表示します。Camera / Lightと同じくクリックでEntityを選択、Ctrlクリックで複数選択できます。無効なコンポーネントは灰色で表示し、引き続き選択できます。同じEntityに複数の環境コンポーネントが付いている場合は代表アイコンを1つ表示し、選択後に全コンポーネントをPropertiesで編集します。旧Sky Renderer / Distance Fogにもアイコンを表示します。ガイド非表示時とプレイ中はアイコンの表示・ピッキングを行いません。

既存レベルには任意のEntityのコンポーネント追加から個別に追加できます。新しい環境コンポーネントは各種類1個を配置します。同種の重複追加はエディターで拒否し、抽出時にも競合を検出します。

## 各コンポーネント

| コンポーネント | 役割 |
| --- | --- |
| `SkyAtmosphereComponent` | 固定時刻から太陽方向を求め、Rayleigh / Mie散乱を近似して空・太陽円盤・夕焼けを描画。夜の明るさも設定可能。 |
| `SkyLightComponent` | 大気を拡散環境光と反射の光源に使用。直接光・背景とは独立した強度と有効状態。「大気を環境光に使用」をオフにするとLevelのHDR環境画像を使用。 |
| `ExponentialHeightFogComponent` | 低い場所で濃く、高い場所で薄くなる霧。大気光の寄与で固定時刻の明るさ・日没色を反映。 |

「時刻で太陽光を制御」が有効なら、描画スナップショットのDirectional Lightを固定時刻の方向・減衰色・強度へ置き換え、影も一致させます。EntityのDirectional LightやTransformの保存値は変更しません。この項目をオフにすると既存Directional Lightのワールド方向で空を照らします。Directional Lightがなければ固定時刻の方向を使います。

大気単独では環境光を追加しません。空の光を物体へ当てるにはSky Lightを配置します。Sky Lightの無効化で古いフォールバック環境光が再出現することはありません。GI有効時は既存の遮蔽・間接光経路を使用します。GIなしのSky Lightは材質AOによる近似なので、室内の遮蔽が必要ならGIを併用してください。

## 描画と互換性

- Y-up、線形Rec.709のHDRで計算し、既存の露出・Bloom・トーンマッピングを通します。空は最遠深度で描画し、深度を書き換えず、カメラの平行移動で動きません。
- 空背景、Sky Light、RTの環境参照、CPUライトベイクで同じ大気近似を使用。環境の変更はベイク署名にも反映します。既存の静的ベイクを使用している場合は環境設定後に再ベイクしてください。
- 既存の`SkyRendererComponent`と`FogComponent`の保存形式を維持。大気があれば旧空背景より優先、高さフォグがあれば旧距離フォグより優先。追加がなければ従来の描画を維持します。「空を作成」では旧空レンダラーも互換用に残しますが、新しい背景はSky Atmosphereの空の強度で調整します。
- 地上視点の軽量な散乱近似です。Unrealの惑星規模LUT、宇宙への遷移、多重散乱、雲、月・星、ボリューメトリックフォグ、シーン全体のキューブマップキャプチャは含みません。粗い鏡面反射は半球の拡散色へ近似します。

## 検証

- `Generated/Outputs/EcsTests/NexusEcsTests.exe`: 固定時刻、保存・読み込み、複製、不正値、削除後の状態リセット。
- `Generated/Outputs/EditorTests/NexusEditorTests.exe`: 作成、編集、Undo/Redo、PIE中の固定時刻。
- `Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --sky`: GPU読み戻しによる昼・夕・夜、平行移動不変、HDR、深度。画像は`Generated/SkyPreview/`。
- 同実行ファイル `--atmosphere`: 大気、CPU/GPU一致、Sky Light、高さフォグ、旧距離フォグのVS/Mesh Shader経路をまとめて検証。
- 同実行ファイル `--ibl`: CPU/GPUの大気色の一致、固定時刻によるSky Lightの変化、既存HDR IBLの回帰。
- 同実行ファイル `--fog`: 高さ・距離フォグのGPU読み戻し、最大不透明度・開始距離・打ち切り距離、VS/Mesh Shader・RT経路。

2026-11-13の検証では本体ビルド、ECS、Editor、`--atmosphere`が成功。`--fog`の既存ガラス判定は `IOR=17 / absorption=6` で閾値に届かず失敗します。変更前のHEADを別フォルダーでビルドしても同じ数値・同じ失敗になることを確認しています。既存の閾値は変更していません。

参考: [Epic: Sky Atmosphere](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine)、[Sky Lights](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-lights-in-unreal-engine)、[Exponential Height Fog](https://dev.epicgames.com/documentation/en-us/unreal-engine/exponential-height-fog-in-unreal-engine)。
