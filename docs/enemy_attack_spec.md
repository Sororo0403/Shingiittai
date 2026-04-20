# 敵技仕様書

## 概要
- 本書は、現在の実装から読み取れる敵行動仕様を整理したものです。
- 対象コードは `include/app/enemy/Enemy.h` と `src/app/enemy/*.cpp` です。
- 数値は現状のデフォルト値を記載しています。チューニング変更時は本書も更新してください。
- 本書では「攻撃技」に加えて、攻撃に直結する補助行動（ワープ、ガード、ストーク）も含めます。

## フェーズ・距離基準
- Phase2移行条件: HP比率 `60%以下`
- 近距離判定: `4.0m以下`
- 遠距離判定: `6.5m超`
- Phase2の中距離圧力範囲: `4.0m超 - 7.7m以下`

## 技一覧
| 技ID | 種別 | 概要 | 主な値 |
| --- | --- | --- | --- |
| Smash | 近接 | 単発の縦系叩きつけ | ダメージ `10` / KB `4.0` / HitBox `1.5,1.8,1.5` |
| DelaySmash | 近接 | 溜め延長付きのSmash派生 | 追加溜め `+0.30s` |
| Sweep | 近接 | 横薙ぎ | ダメージ `10` / KB `4.0` / HitBox `3.2,1.2,1.4` |
| DoubleSweep | 近接 | Sweepの2段派生 | 2段目開始遅延 `0.18s` / 2段目溜め倍率 `0.55` |
| Shot | 飛び道具 | 右手から弾を連射 | ダメージ `5` / KB `2.5` / 弾速 `6.0` |
| Wave | 飛び道具 | 正面に衝撃波を1発発生 | ダメージ `8` / KB `3.0` / 速度 `4.0` |
| Rush | 突進 | 追尾しながら踏み込む突進 | ダメージ `12` / KB `5.0` / 速度 `8.5` |
| WarpApproach | 補助 | プレイヤー近傍へ接近ワープ | 追撃は `Smash / Sweep / Rush` |
| WarpEscape | 補助 | プレイヤーから離脱ワープ | 追撃は `Shot / Wave` |
| Guard | 防御 | 顔・左右胴のいずれかをガード | 移行 `0.25s` / 維持 `0.7s` |
| Stalk | 補助移動 | 周囲を回りつつ間合い調整 | 維持 `0.45 - 1.10s` / 移動速度 `2.2` |

## 個別仕様

### 1. Smash
- 基本溜め時間: `0.45s`
- トラッキング終了: `0.28s`
- 攻撃タイミング: 開始 `0.04s` / 終了 `0.10s` / Recovery開始 `0.18s` / 全体 `0.88s`
- 攻撃判定オフセット: 前方 `1.4` / 高さ `0.8`
- Tell演出時間: `0.12s`
- フェイント溜め移行率: 基本 `45%`
- Hold時間: `0.12 - 0.40s`
- Fake Commit率: 基本 `45%`
- Fake Commit時間: `0.10s`
- Freeze Hold時間: `0.10 - 0.18s`
- Hold中の分岐候補: そのまま発動 / Warp / Guard / Rush
- DelaySmash選択率: 基本 `35%`
  - `CounterBait` 中、またはプレイヤーがカウンター構え中は `+20%`
  - Phase2ではさらに `+22%`
- DelaySmashは追加溜め `+0.30s`
- DelaySmashが空振りし、ヒットもガードもされなかった場合は隙を追加
  - 追加Recovery `+0.34s`
  - 向き直り速度も低下

### 2. Sweep
- 基本溜め時間: `0.65s`
- トラッキング終了: `0.36s`
- 攻撃タイミング: 開始 `0.12s` / 終了 `0.24s` / Recovery開始 `0.32s` / 全体 `1.27s`
- 攻撃判定オフセット: 横 `0.2` / 高さ `0.8`
- Tell演出時間: `0.10s`
- フェイント溜め移行率: 基本 `28%`
- Hold時間: `0.10 - 0.30s`
- Fake Commit率: 基本 `32%`
- Fake Commit時間: `0.08s`
- Freeze Hold時間: `0.08 - 0.14s`
- Hold中の分岐候補: そのまま発動 / Warp / Guard / Rush
- DoubleSweep選択率: 基本 `30%`
  - `CounterPunish` 中、またはプレイヤーのカウンター失敗を観測した場合は `+20%`
- DoubleSweepは1段目Recovery中 `0.18s` 経過で2段目へ移行
- DoubleSweepの2段目溜め時間は通常Sweepの `55%`

### 3. Shot
- 溜め時間: `0.60s`
- Recovery: `0.80s`
- 発射間隔: `0.20s`
- 発射数: `3 - 5発`
- 発生位置: 右手基準、高さオフセット `+0.2`
- 弾性能: ダメージ `5` / KB `2.5` / HitBox `0.4,0.4,0.4`
- 弾速: `6.0`
- 寿命: `2.0s`
- 弾はプレイヤー位置へ向けて都度生成

### 4. Wave
- 溜め時間: `0.60s`
- Recovery: `0.80s`
- 発生数: `1発`
- 発生位置: 胴体前方 `1.5`、高さオフセット `0.0`
- 発射方向: 溜め終了時にロックした正面方向
- 性能: ダメージ `8` / KB `3.0` / HitBox `1.2,0.6,1.6`
- 移動速度: `4.0`
- 最大到達距離: `8.0`

### 5. Rush
- 溜め時間: `0.28s`
  - プレイヤーガード中は `+0.10s`
- トラッキング終了: `0.20s`
- 攻撃タイミング: 開始 `0.08s` / 終了 `0.28s` / Recovery開始 `0.38s` / 全体 `0.75s`
- 突進移動時間: `0.26s`
- 突進速度: `8.5`
- 攻撃判定オフセット: 前方 `1.1` / 高さ `0.8`
- HitBox: `1.2,1.4,2.2`
- Charge中は `35%` 経過以降じわっと前進
- 突進は「カーブ開始 -> ホーミング -> ブレーキ」の3段階で旋回率と速度倍率が変化
- Phase2で距離 `1.6 - 4.6m` のとき、RecoveryからSweep派生を判定
  - 基本率 `34%`
  - Phase2補正 `+18%`
  - Shot始動コンボ経由なら `+28%`
  - プレイヤーガード中は `+8%`
- 派生しないまま空振りした場合は隙を追加
  - 追加Recovery `+0.22s`
  - 向き直り速度も低下

### 6. WarpApproach
- 開始 `0.20s` / 移動 `0.10s` / 終了 `0.20s`
- 接近先は以下から抽選
  - FrontLeft: 前方 `2.3` + 左右 `2.1`
  - FrontRight: 前方 `2.3` + 左右 `2.1`
  - LongFront: 前方 `4.0`
- スロット比率
  - FrontLeft `38%`
  - FrontRight `38%`
  - LongFront `24%`
- 接近ワープ後の追撃
  - 側面寄り着地時: Sweep寄り
  - 正面遠め着地時: Smash / Rush寄り
- ワープ中は不可視化し、当たり判定を無効化

### 7. WarpEscape
- 開始 `0.20s` / 移動 `0.10s` / 終了 `0.20s`
- 離脱先はプレイヤー中心の半径 `5.0 - 7.5` のランダム位置
- Escape Warpのクールダウン: `7.0s`
- 離脱ワープ後の追撃: `Shot` または `Wave`

### 8. Guard
- 対象部位は `Face / BodyLeft / BodyRight` の3択ランダム
- Move `0.25s` -> Hold `0.70s` -> Recovery `0.35s`
- Hold中はガード有効

### 9. Stalk
- 維持時間: `0.45 - 1.10s`
- 移動速度: `2.2`
- 移動内容: 左右ストレーフ主体、前後補正を少量加える
- 同一行動の連続上限: `2回`

## 攻撃選択ロジック

### Pressure
- 近距離では `Smash / Sweep / Guard / Rush` を重み抽選
  - 基本重み: Smash `30`, Sweep `25`, Guard `5`, Rush `40`
  - Phase2補正: Smash `+8`, Sweep `+14`, Guard `-2`, Rush `+6`
- Phase2の中距離圧力では `Shot / Warp / Rush / Wave` を重み抽選
  - 基本値ベース: Rush `45`, Shot `25`, Wave `30`, Warpは遠距離値 `25` を流用
- 近距離/中距離ともに `Stalk` へ入ることがある

### CounterBait
- カウンター構え・カウンター成功傾向を見て `Smash or Sweep` と `Guard` を選ぶ
- 読み方向
  - プレイヤーが横カウンター寄りなら `Smash`
  - プレイヤーが縦カウンター寄りなら `Sweep`

### CounterPunish
- 近中距離では `Smash / Sweep / Rush`
  - 基本重み: Smash `30`, Sweep `20`, Rush `50`
- 遠距離では `Warp` 優先、失敗時は `Rush`

### AntiGuard
- プレイヤーガード中は `Rush / Wave / Shot` や `Shot / Wave / Warp` を選びやすい
- 基本補正
  - Wave `+20`
  - Shot `+8`
  - Rush `+10`

### Chase / Reset
- 遠距離では `Shot / Warp / Wave` を選択
- 距離停滞時はWarp重み増加
- カウンター成功を連続で受けると次回 `Escape Warp` を強制しやすい

## 派生・コンボ仕様
- Smash/SweepのHold中は `Warp / Guard / Rush` 分岐を行うことがある
- Hold中の早出し
  - プレイヤーのカウンター早出し観測時は、Holdの `30%` 経過で解放しやすい
  - カウンター遅れ観測時は `75%` まで引っ張りやすい
- Shot後の派生
  - 距離 `3.0 - 8.5` なら `Rush` 派生判定
  - 距離 `4.0 - 9.0` なら `WarpApproach` 派生判定
  - Phase2、ガード中、カウンター構え中で確率補正あり
- Smash/Sweep/Rush後のRecovery派生
  - Recommit: 別の近接技へ再攻撃
  - DelayedSecond: 同系統の遅らせ2段目
  - EscapeFakeout: 後方ワープから `Shot / Wave`
- DelaySmashがPhase2でヒットまたはガードされた場合
  - ほぼ専用コンボとして `Sweep` に派生
- Sweep後
  - 距離 `5.0m以下` なら `WarpApproach -> Smash` コンボ判定
- Wave後
  - 距離 `4.5m以上` なら `WarpApproach -> Smash` コンボ判定
- WarpApproachチェイン
  - `Smash -> Sweep`
  - Phase2では `Rush -> Sweep`
- WarpEscapeチェイン
  - `Shot -> Rush`
  - `Wave -> Rush`
- バックワープ派生
  - Smash後 `35%`
  - Sweep後 `30%`
  - Wave後 `20%`
  - 追撃は `Shot` または `Wave`

## 実装上の注意
- `AdvanceSweep` と `BackWarpFollowup::Rush` の列挙はありますが、現状デフォルト挙動では主要ルートとして使われていません。
- ワープは演出停止中 (`suspendWarpForPresentation_`) は無効化され、代替行動に置き換わります。
- Smash/Sweepの実効溜め時間は、プレイヤーのカウンター傾向に応じて若干増減します。
  - 加算上限 `+0.28s`
  - 減算下限 `-0.08s`
