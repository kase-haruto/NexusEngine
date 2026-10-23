// 選択メッシュだけの二値マスク。SceneのDepth/Lightingは使わない。
float4 main() : SV_TARGET { return float4(1, 1, 1, 1); }
