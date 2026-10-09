# 色空間・トーンマッピング・ポストプロセス

シーンの作業色空間は **線形Rec.709 / D65** です。描画先はRGBA16_FLOATで、1を超える光を保持します。処理順は、線形HDR描画 → Bloom → EV露出 → ホワイトバランス → 色調整 → トーンマッピング → sRGB符号化 → FXAA → Editor UIです。

## 入力と出力

| 入力 | 扱い |
|---|---|
| Base Color画像 | TextureColorSpace::Srgb。sRGB SRVによりGPUサンプリング時に線形化 |
| 法線・Metallic/Roughness画像 | TextureColorSpace::Linear。色変換なし |
| glTFのBase Color係数・頂点色 | 仕様どおり線形RGB。追加のsRGB変換をしない |
| ライト色・Material Tint・Clear Color | エンジンAPI・Levelファイルでは線形RGB |
| EditorのLight Color | sRGBで入力・表示し、保存するコンポーネント値を線形化 |
| Alpha | 色の伝達関数を適用しない |

CPUでは Graphics/Color/ColorSpace.h、HLSLでは Shaders/Common/ColorSpace.hlsli の伝達関数を利用します。CreateTexture2DFromEncodedDataの色空間引数を明示してください。引数を省略した既存APIはsRGBのままです。

トーンマッピングの出力は線形Rec.709の0～1です。その後に一度だけsRGB符号化し、UNORM RTVへ書き込みます。sRGB RTVでさらに符号化しないでください。SwapChainにはfull-range SDR Rec.709を明示します。現在の出力はSDRであり、HDR10/PQや広色域ディスプレイへの出力は含みません。Editor UIは処理後のSDR画像に描画します。

## 操作

Editorの **Post Processing → Tone Mapping** で切り替えます。

| モード | 用途 |
|---|---|
| PBR Neutral（既定） | 材質色を比較しやすい表示。色相を維持しながらハイライトを圧縮し、強い光を白へ近づける |
| ACES Fitted | 既存のStephen HillによるSDR Rec.709近似。映画的な表示。完全なACES参照変換ではない |
| Linear Clamp | HDR値を0～1へクランプする診断表示。sRGB出力変換は引き続き行う |

Reset Post ProcessingはPBR Neutralと中立の色調整へ戻します。ExposureはEV単位で、+1は光量2倍です。White Balance RGBは線形RGBの倍率で、{1,1,1}が中立です。Kelvin指定ではありません。Saturation / Contrast / Grading Gamma / Gainは1、Offsetは0が中立です。Grading Gammaは表示用sRGB変換とは別の色調整です。

APIはPostProcessSettings::toneMapperとGraphicsSystem::SetPostProcessSettingsを使用します。不明なモード、非有限値、範囲外の設定は拒否します。既存のfilmicEnabled=falseは互換性のためLinear Clampとして扱います。

色空間を正しく扱っても、Base Colorと画面上のRGBが常に一致するわけではありません。照明、材質の反射、露出、トーンマッピングが表示色を決めます。色の比較時はBloomと色調整を中立にし、同じ照明と固定露出で比較してください。

## Bloom・FXAA・資源

Bloomは露出前の線形HDRからソフト閾値で抽出し、1/2～1/32解像度の5段のtentフィルター結果を合成します。強度0または無効時はBloomパスを省略します。

FXAAは既定で有効です。Anti-aliasing (FXAA)で切り替えられます。トーンマッピング後のsRGB画像に適用し、Editor UIには適用しません。無効時はFXAA用中間画像への描画とフィルターパスを省略します。細部が柔らかくなる場合があり、時間方向のちらつきへの対策は含みません。

PostProcessBackendがHDR画像6枚とFXAA用のフル解像度SDR画像1枚を所有します。CPU/HLSLの設定レイアウトは24DWORDです。各フレームの終端で内部画像をSRV状態に戻し、Resize・ShutdownはGPU idle後に行います。

## 検証

GPU Validation付きのテストで、色画像のsRGBデコード、データ画像の線形デコード、sRGB出力の二重適用防止、EV露出、PBR Neutralの基準グレー・色相・単調なHDR応答、ACESの切り替え、FXAA、Bloom、Resize、旧API互換を確認します。参照色を変えてクリアするために発生するfast-clear性能通知のみ除外し、その他のValidation警告・エラーは失敗扱いです。

実行: Generated/Outputs/GraphicsTests/Debug/NexusGraphicsTests.exe --color

参照: [Khronos PBR Neutral仕様](https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral)。ACES近似の出典・MITライセンスは Shaders/PostProcess/ACES-LICENSE.txt に同梱しています。
