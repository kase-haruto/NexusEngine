<h1 align="center">NexusEngine</h1>

<p align="center">
ツール・エディタ、グラフィックス、フィジックスを学習することを目的とした自作ゲームエンジンです。
</p>

## 概要

**NexusEngine** は、ゲームの実行環境だけでなく、エディタや制作支援ツールを含めた開発環境の構築を目的としています。

機能同士の依存関係や責務を明確にし、保守性と拡張性を意識した設計を目指します。

## 開発方針

* 各機能の責務を明確にする
* モジュール間の依存を最小限にする
* 拡張しやすいインターフェースを設計する
* エディタとランタイムの責務を分離する
* 制作中の試行錯誤を効率化する
* 実装理由や設計意図を記録する

## 開発環境

<!-- 使用する環境が確定したら更新 -->

* Language: C++
* Platform: Windows

## ブランチ作成規則

ブランチ名は、変更内容を表す接頭辞と作業内容を組み合わせて作成します。

```text
接頭辞/作業内容
```

作業内容には、camelcaseを使用します。

### `feature/`

新しい機能を追加する場合に使用します。

```text
feature/sceneSystem
feature/modelRenderer
feature/editorWindow
```

### `fix/`

不具合を修正する場合に使用します。

```text
fix/modelLoading
fix/sceneTransition
fix/editorCrash
```

### `refactor/`

既存機能の動作を変更せず、設計やコードを改善する場合に使用します。

```text
refactor/renderSystem
refactor/assetManager
```

### `docs/`

READMEや設計資料など、ドキュメントを変更する場合に使用します。

```text
docs/updateReadme
docs/addRenderingDesign
```

### `chore/`

ビルド設定や依存ライブラリの更新など、機能実装以外の作業に使用します。

```text
chore/updateBuildSettings
chore/addLibrary
```

## コミットメッセージ規則

コミットメッセージには、変更内容を表す接頭辞を付けます。

```text
接頭辞: 変更内容
```

例：

```text
feat: シーン管理機能を追加
fix: モデル読み込み時のクラッシュを修正
refactor: 描画処理の責務を分割
docs: ブランチ作成規則を追加
chore: ビルド設定を更新
```


## ビルド方法

ビルド手順は、開発環境が整い次第追記します。

## ドキュメント

エディタの起動、編集操作、拡張方法、検証と制限は[Editor利用ガイド](Project/Docs/EditorUserGuide.md)を参照してください。

Live++ の導入・操作、Reflection の自動生成と Reinstancing による状態移行は
[ライブコーディング](Project/Docs/LiveCoding.md)を参照してください。

設計方針や各機能の仕様は、`Project/Docs`ディレクトリに記録します。

エディタの構成、問題点、改善優先順位、設計案、実装ロードマップは
[Editor現状調査・設計レビュー](Project/Docs/EditorArchitectureReview.md)を参照してください。

機能を追加する際は、可能な範囲で以下の内容を残します。

* 機能の目的
* 採用した設計
* その設計を採用した理由
* 他の案との比較
* モジュール間の依存関係
* 今後の課題

## ライセンス

ライセンスは現在未設定です。

## レンダリング

Metallic/Roughness PBR、HDR、Compute IBL（Diffuse Irradiance / GGX Prefilter / BRDF LUT）、DDGI（動的照度フィールド）、ベイクGI＋ライトプローブ（比較用）、拡散RTGI（比較用）、Emissive / AO、RT Shadow、鏡・ガラス、Mesh Shaderに対応しています。

通常描画のGIは[DDGI](Project/Docs/DDGI.md)、事前計算方式は[ベイクGI＋ライトプローブ](Project/Docs/BakedLighting.md)を参照してください。RTGIの設定・検証・制約は[拡散GI設計](Project/Docs/GlobalIllumination.md)を参照してください。環境HDRの設定、責務分離、リソース寿命、テストと制約は[IBL設計資料](Project/Docs/IBL.md)、既存の描画基盤は[Graphics設計](Project/Docs/GraphicsREADME.md)を参照してください。
