# NexusEngine — 汎用カーブエディタ仕様書

> 対象: NexusEngine / Editor / Runtime  
> ステータス: 実装設計案（既存コードとの整合確認前）  
> 初期ユースケース: アニメーションの再生速度制御（Speed Curve）

## 1. 目的

NexusEngineに、**汎用Floatカーブを直接操作・保存・適用できるCurve Editor**を実装する。

- グラフ上でキーと接線を直感的に編集する。
- カーブを独立したアセットとして保存し、他の機能から再利用する。
- 使用先へカーブをアタッチし、編集結果をリアルタイムプレビューする。
- 初期ユースケースはアニメーションの**再生速度倍率**の制御とする。
- カーブの保存データは用途非依存とし、将来はカメラ・ライト・パーティクル・音量等にも使用可能にする。

## 2. 不変の設計原則

1. **Curve Editorはアニメーション専用ではない。**
2. `.curve` は数値カーブの定義だけを保持する。Transform、Color、AnimationClip、コンポーネント名、プロパティ名、アタッチ先等は保存しない。
3. 使用先が入力値の意味（秒、正規化時刻等）・出力値の意味（再生速度倍率等）・範囲制約を決める。
4. Editor UI、汎用Curve評価Runtime、アセット管理、用途別Bindingを分離する。
5. Runtimeのカーブ評価はImGuiやEditorの型に依存しない。
6. 変更を保存する前でも、アタッチ先のプレビューに反映できる。
7. 本仕様のメイン機能は**Speed Curve**であり、Time Remappingカーブへの置き換えは行わない。
8. 既存のNexusEngineの命名・Asset ID・Serializer・Undo/Redo・Reflection等が存在する場合、重複実装せず再利用する。

## 3. 用語

| 用語 | 意味 |
| --- | --- |
| Curve | スカラー入力xからスカラー出力yを評価するデータ |
| Keyframe / Key | `(x, y)` と補間・接線情報を持つ制御点 |
| Tangent | キー前後のカーブ形状を指定する情報 |
| Curve Asset | `.curve` に永続化した独立リソース |
| Binding / Attachment | カーブを使用する側が持つCurve Assetへの参照 |
| Speed Curve | xを再生経過時間等、yを再生速度倍率として使用する利用形態 |
| Preview | 編集中のCurveを実際の使用先で評価して動作を確認する機能 |

## 4. UX/UI仕様

### 4.1 ウィンドウ構成

- 独立したドッキング可能な `Curve Editor` ウィンドウ。
- タイトル／編集中のCurve Asset名／未保存状態の表示。
- カーブ描画領域（グリッド、X/Y軸、現在座標、キー、線、接線ハンドル）。
- 選択キーの `X`, `Y` と補間・接線モードの編集UI。
- ズーム、パン、全体表示、選択範囲へフォーカス。
- アセット保存・別名保存・Undo/Redo。
- 使用先から開いた場合には、使用先プレビューとの連動状態と現在の評価位置を表示。
- X軸/Y軸は**汎用値**として表示し、Curve Asset自体に `Speed` や `Transform` の意味を固定しない。使用先のUIは補足ラベル（Playback Time / Speed Multiplier等）を表示してよい。

### 4.2 マウス・キーボード操作

| 操作 | 挙動 |
| --- | --- |
| 左クリック（キー） | キーを選択 |
| 左ドラッグ（キー） | キーのX/Yを編集し、逐次プレビュー更新 |
| 左ドラッグ（接線ハンドル） | 接線を編集 |
| 左クリック（空白） | 選択解除 |
| ダブルクリック（カーブ上） | クリック位置にキーを追加。可能な限り既存の曲線形状を維持 |
| 右クリック（キー） | キー用コンテキストメニューを開く |
| 右クリック（空白） | グラフ用コンテキストメニューを開く |
| Delete | 選択キー削除 |
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| ホイール | カーソル中心のズーム |
| 中ボタンドラッグ | パン |
| F | 全キーを表示領域に収める |

追加予定: Shift/Ctrlによる複数キー選択、矩形選択、軸固定移動、コピー＆ペースト。

### 4.3 右クリックメニュー

**キー上:**
- Delete Key
- Copy Key / Paste Key
- Set X / Set Y（数値入力）
- Interpolation: Constant / Linear / Cubic
- Tangent Mode: Auto / Clamped Auto / Free / Aligned / Broken（サポートする方式に合わせる）

**空白上:**
- Add Key Here
- Paste Keys
- Frame All
- Reset View

**曲線上:**
- Add Key on Curve（評価値でキーを追加）

操作はUndo/Redo対象とし、右クリックで不要なキー移動を発生させない。

### 4.4 接線・補間

最低限:
- Constant（段階変化）
- Linear（直線補間）
- Cubic（接線による滑らかな補間）

接線はグラフ上に可視化し、ドラッグで直接調整可能とする。Auto、Clamped Auto等の自動接線と手動接線を追加しやすいデータ構造にする。

**注意:** Cubic Hermiteの接線（傾き）とBezierのハンドル位置・ウェイトは同じ表現ではない。どの曲線方式を採用するかを先に確定し、保存形式とUIを一致させること。初期版はHermiteベースでもよいが、独立したBezierハンドルの時間方向ウェイトを扱うなら保存フィールドを増やす。

### 4.5 編集品質

- X座標順序を保証し、同一Xでの扱い（拒否・置換等）を明示する。
- マウス位置からカーブ座標へ正確に変換する。
- キーのドラッグ中は画面内をリアルタイム更新する。
- 操作開始から終了までの連続ドラッグは原則1つのUndoコマンドにまとめる。
- アセットに未保存変更がある場合、Dirtyを表示する。
- 選択対象、ズーム位置、数値の精度、スナップ設定はUIとして扱い、Curve Asset本体に用途情報を混ぜない。

## 5. データモデルと保存

### 5.1 拡張子

- 初期候補: **`.curve`**。
- `.anim` / `.animCurv` は使用しない。
- `.nxease` はイージング専用と誤解されやすいため、本汎用用途では優先しない。
- 実際の拡張子の衝突・エンジンAsset Registryへの登録は実装前に確認する。

### 5.2 保存するもの・しないもの

**保存する:**
- フォーマットバージョン
- キーのX・Y
- 区間ごとの補間形式
- in/out接線・接線モード等、曲線評価と編集復元に必要な情報
- 必要な場合のみ外挿／境界評価設定

**保存しない:**
- Transform / Color / AnimationClip等のパラメータ定義
- アタッチ先のEntity・Component・Property
- 「速度倍率」「秒」などの用途固有の単位
- 使用先だけに意味のある倍率・Clamp・Loop設定

### 5.3 概念的な保存例

以下は**確定スキーマではない**。既存Serializerの設計に合わせる。

```json
{
  "version": 1,
  "keys": [
    {
      "x": 0.0,
      "y": 0.0,
      "interpolation": "Cubic",
      "outTangent": 0.0
    },
    {
      "x": 0.5,
      "y": 2.0,
      "interpolation": "Cubic",
      "inTangent": 0.0,
      "outTangent": 0.0
    },
    {
      "x": 1.0,
      "y": 1.0,
      "interpolation": "Cubic",
      "inTangent": 0.0
    }
  ]
}
```

### 5.4 C++ API例（概念）

```cpp
struct CurveKey {
    float x = 0.0f;
    float y = 0.0f;
    float inTangent = 0.0f;
    float outTangent = 0.0f;
    // 補間方式、接線モード等を追加
};

class FloatCurve {
public:
    float Evaluate(float x) const;
    // 追加・削除・編集・検証用API
private:
    std::vector<CurveKey> keys_;
};
```

- 入力順ソート・範囲外評価・0件/1件キー・重複X・NaN等の扱いを規定する。
- 評価結果は同じ入力と同じカーブなら決定的であること。
- 永続化は既存のAsset GUID/Asset Registry/Serializerと整合させる。

## 6. カーブのアタッチと利用

1. カーブは `.curve` として作成・保存する。
2. 使用先のコンポーネントやシステムがCurve Asset参照を保持する。
3. 使用先が入力Xを構築し、`Evaluate(x)` の結果を対象のパラメータへ適用する。
4. Curve Asset自体は使用先に依存しない。
5. 複数の使用先が同一のCurve Assetを共有できるようにする。

**例:**

```text
SpeedProfile.curve
  ├─ Animation Playback Speed にアタッチ
  ├─ Particle Emission 値にアタッチ（将来）
  └─ Camera Effect 強度にアタッチ（将来）
```

使用先のInspector/Detailsには、Curve Assetの選択・解除・編集画面を開く操作を設ける。既存のPropertyHandle/Reflection/Asset Pickerがあれば活用する。

## 7. 初期用途: Animation Speed Curve

### 7.1 意味付け

- Curveの入力X: **再生速度カーブ用の経過時間**（初期推奨は正規化時間0～1）。
- Curveの出力Y: **アニメーション再生速度倍率**。
- `0`: 停止、`1`: 等速、`2`: 2倍速。
- 負値は逆再生に相当するが、初期版で許容するかはAnimation Playback側で明示的に決める。
- Curve AssetにX/Yの意味を保存しない。この解釈はAnimation Playbackが持つ。

### 7.2 時間更新

**重要: Speed CurveはTime Remappingではない。** 速度を時間積分してアニメーション再生位置を求める。

```text
speed(t) = curve.Evaluate(normalize(t))
animationTime(t) = initialAnimationTime + Integral[0..t] speed(s) ds
```

ここで`t`は速度によって変化しない**独立した経過時間**。`animationTime`を次回のSpeed Curve評価Xへ流用しない。

リアルタイム更新の概念例:

```cpp
float speed = curve.Evaluate(playbackElapsedTime / speedCurveDuration);
animationTime += deltaTime * speed;
playbackElapsedTime += deltaTime;
```

これは単純な逐次更新例であり、**精密なシークを保証しない**。スクラブや任意時刻へのジャンプでは、カーブを積分して目的時刻の`animationTime`を再計算する、または積分用キャッシュを利用する。フレームレートに依存しない再現性のある結果を目指す。

### 7.3 決定するべき再生規則

- 速度カーブにおけるXの範囲（正規化0～1か秒単位か）。
- 再生速度カーブを適用する時間区間（元AnimationClip長との関係）。
- 速度0／負値／最大倍率／範囲外値の扱い。
- クリップ終端での終了／停止／ループの扱い。
- 正規化Xの進行が終了した後の速度保持・停止等の扱い。
- 速度カーブによってアニメーションが短縮・延長したときの再生終了条件。
- Eventの飛び越し、シーク、逆再生、ループ時のイベント発火ルール。

これらは**`.curve` ではなく使用先のAnimation Playback設定**で決める。

## 8. リアルタイムプレビュー

### 8.1 操作フロー

1. Animation Editorでアニメーションを選ぶ。
2. Playback Speedに `.curve` をアタッチする。
3. Curve Editorを開く。
4. キーまたは接線をドラッグする。
5. **未保存の変更も**即座にPreviewへ反映される。
6. 再生・一時停止・シークしながら結果を確認する。
7. 保存でCurve Assetへ変更を反映する。

### 8.2 技術的な前提

- Editorで変更したカーブ内容をPreview側の評価へ反映する通知または共有状態を設ける。
- EditorとRuntimeが別プロセスの場合は、既存の通信経路を調査し、プレビュー用のカーブ更新と再生位置更新を送れるようにする。
- 編集セッションとディスク上の確定アセットを区別し、キャンセル／再読込／Undo/Redoで矛盾が生じないようにする。
- カーブが更新された際には速度積分のキャッシュを無効化し、現在のプレビュー位置を再計算する。
- 複数利用箇所で共有するCurve Asset編集が波及することをUI上で分かるようにする。

## 9. 推奨モジュール境界

| モジュール例 | 責務 |
| --- | --- |
| `FloatCurve` | キー保持、評価、検証 |
| `CurveSerializer` | `.curve` 読込・保存・バージョン管理 |
| `CurveAsset` | 既存Assetシステムとの統合・識別 |
| `CurveEditor` | グラフ描画、キー／接線操作、右クリックメニュー |
| `CurveEditService` | 編集操作、Dirty、Undo/Redo、変更通知 |
| `CurveBinding`（必要に応じて） | 使用先側のCurve参照・評価接続 |
| `AnimationPlayback` | Speed Curveの解釈、時間積分、再生・シーク・ループ |

名前・クラス分割は既存コードを調査して調整する。専用の巨大マネージャーを増やすより、既存のAsset/Reflection/Editorフレームワークに統合する。

## 10. 実装優先順位

### P0 — 最初の完成条件

- [ ] 既存構造の調査と責務整理
- [ ] FloatCurveの定義、評価、境界処理、基本テスト
- [ ] `.curve` 保存・読込とアセット参照
- [ ] グラフ、軸、キー、曲線の描画
- [ ] キーの追加・選択・移動・削除
- [ ] 右クリックメニュー
- [ ] Linear/Cubic補間と接線の直接操作
- [ ] Animation Playback Speedへのアタッチ
- [ ] 未保存編集の再生プレビュー反映
- [ ] 再生／停止／シークと更新後の再評価

### P1 — 実用編集品質

- [ ] Undo/Redo
- [ ] ズーム、パン、全体表示
- [ ] 複数キー選択と編集
- [ ] 数値入力
- [ ] Auto/Clamped接線、適切なオーバーシュート抑制
- [ ] 保存確認・再読込・共有アセット編集時の状態管理

### P2 — 拡張

- [ ] 複数カーブの重ね合わせ表示
- [ ] カーブのプリセット
- [ ] キーのコピー＆ペースト
- [ ] スナップ・範囲選択・軸制限移動
- [ ] 他のシステムへのアタッチUI

## 11. 受け入れ基準

1. Curve Editor単体で新規 `.curve` を作成し、保存・再読込後に同じ曲線を再現できる。
2. マウスでキーを追加・移動・削除でき、右クリックメニューから同じ操作を実行できる。
3. 接線操作によってグラフ形状がリアルタイムに変わる。
4. Curve AssetにはTransform/Color等の用途情報もアタッチ先情報も含まれない。
5. アニメーションのSpeed Curveへアタッチでき、速度変化がプレビューに反映される。
6. 未保存編集中の変更がプレビューへ反映される。
7. 同じカーブと指定時刻に対して、シーク後の再生位置が直前の更新履歴に依存しない。
8. Editor/ImGuiをリンクしないRuntimeでもカーブ評価が実行できる。
9. キー0件、1件、X重複、範囲外入力、極端な接線、負値などの挙動が規定・検証されている。
10. 既存のNexusEngineアセット管理・エディタ設計との重複や不整合を避けている。

## 12. AI実装担当者への指示

1. 実装前にNexusEngineの現行コードを調査し、Animation Editor、Asset管理、Serializer、Editorウィンドウ、ImGui操作、Undo/Redo、Preview通信の既存実装を特定する。
2. 既存コードに類似機能が存在する場合は再利用・統合する。仕様書内の例示クラス名・JSON構造を無条件に新設しない。
3. `.curve` が用途非依存であることを最優先の設計制約とする。
4. 初期用途はSpeed Curveであり、Time Remappingとして置き換えない。シーク時の積分再評価に注意する。
5. UIはグラフへの直接操作を中心に実装し、コンテキストメニューを必須とする。
6. 実装後はテストと実際のプレビュー動作を確認し、対応範囲、未対応項目、設計上の決定事項を報告する。
7. 未決定事項については既存実装との整合を優先して明確な判断を行い、判断理由を記録する。

---

**目標:** 用途非依存の `.curve` アセットを視覚的に編集し、まずアニメーション再生速度にアタッチして、実際の動作を見ながら調整できる状態を実現する。
