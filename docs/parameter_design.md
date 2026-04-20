# パラメータ設計メモ

## 目的
- 敵、プレイヤー、剣のパラメータを「読みやすく」「増えても破綻しにくく」する
- 実行時状態と設定値を分離する
- 1つの技・1つの機能に関する値を、できるだけ同じ場所へ寄せる
- 保存形式やデバッグUIも拡張しやすくする

## 結論
一番良い書き方は、`クラスに大量の float を直置きする` 形ではなく、次の4層に分ける形です。

1. `RuntimeState`
   今動いている最中の値だけ
2. `Config`
   調整値だけ
3. `Profile`
   技・行動・武器などのまとまりごとの設定
4. `Preset`
   セーブ/ロード対象のデータ構造

## 設計ルール

### 1. 実行時状態と調整値を分ける
悪い例:
```cpp
float hp_;
float maxHp_;
float smashChargeTime_;
float stateTimer_;
float rushSpeed_;
bool isAttackActive_;
```

良い例:
```cpp
struct EnemyRuntimeState {
    float hp = 0.0f;
    float stateTimer = 0.0f;
    bool isAttackActive = false;
};

struct EnemyConfig {
    float maxHp = 1000.0f;
    AttackProfile smash;
    AttackProfile rush;
};
```

ルール:
- `今だけ変わる値` は `RuntimeState`
- `ゲームデザイン上の設定値` は `Config`
- この2つは混ぜない

### 2. 同じ意味の値は struct にまとめる
悪い例:
```cpp
float smashDamage_;
float smashKnockback_;
float smashChargeTime_;
float smashHoldTimeMin_;
float smashHoldTimeMax_;
float smashFeintChance_;
```

良い例:
```cpp
struct AttackProfile {
    AttackParam attack;
    AttackTimingParam timing;
    float chargeTime = 0.0f;
    RangeF holdTime{0.0f, 0.0f};
    float feintChance = 0.0f;
};
```

ルール:
- 名前に同じ接頭辞が大量につくなら、まとめる候補
- `smash*`, `sweep*`, `rush*` のような値は、技単位 struct に寄せる

### 3. 共通項目と個別項目を分ける
全部を共通化しすぎると逆に読みにくくなります。

良い例:
```cpp
struct AttackProfile {
    AttackParam attack;
    AttackTimingParam timing;
    float chargeTime = 0.0f;
};

struct SmashProfile {
    AttackProfile base;
    float delayChance = 0.0f;
    float delayExtraCharge = 0.0f;
};

struct SweepProfile {
    AttackProfile base;
    float doubleChance = 0.0f;
    float secondDelay = 0.0f;
    float secondChargeScale = 1.0f;
};
```

ルール:
- 共通で使うものは `AttackProfile`
- その技にしかないものだけ個別 profile に足す

### 4. 範囲やセットの概念を型で表す
悪い例:
```cpp
float shotRushMinDistance_;
float shotRushMaxDistance_;
float smashHoldTimeMin_;
float smashHoldTimeMax_;
```

良い例:
```cpp
struct RangeF {
    float min = 0.0f;
    float max = 0.0f;
};
```

ルール:
- `Min/Max` は `RangeF`
- `重みセット` は `Weights`
- `確率セット` は `Chances`

### 5. パラメータは役割ごとに階層化する
敵なら最低でも次の4ブロックに分けるのがおすすめです。

```cpp
struct EnemyConfig {
    EnemyCoreProfile core;
    EnemyAttackSet attacks;
    EnemyAiProfile ai;
    EnemyPresentationProfile presentation;
};
```

意味:
- `core`: HP、距離閾値、移動など
- `attacks`: 技性能
- `ai`: 行動選択重み、派生率
- `presentation`: 演出時間、見た目補正

## 推奨構成

### 共通ユーティリティ
```cpp
struct RangeF {
    float min = 0.0f;
    float max = 0.0f;
};

struct WeightedActionSet {
    int smash = 0;
    int sweep = 0;
    int guard = 0;
    int rush = 0;
    int shot = 0;
    int wave = 0;
    int warp = 0;
};
```

### 敵
```cpp
struct EnemyCoreProfile {
    float maxHp = 1000.0f;
    float phase2HealthRatioThreshold = 0.60f;
    float nearAttackDistance = 4.0f;
    float farAttackDistance = 6.5f;
};

struct AttackProfile {
    AttackParam attack;
    AttackTimingParam timing;
    float chargeTime = 0.0f;
};

struct MeleeAttackProfile : AttackProfile {
    RangeF holdTime{0.0f, 0.0f};
    float feintChance = 0.0f;
};

struct SmashProfile {
    MeleeAttackProfile base;
    float attackForwardOffset = 0.0f;
    float attackHeightOffset = 0.0f;
    float delayChance = 0.0f;
    float delayExtraCharge = 0.0f;
};

struct SweepProfile {
    MeleeAttackProfile base;
    float attackSideOffset = 0.0f;
    float attackHeightOffset = 0.0f;
    float doubleChance = 0.0f;
    float secondDelay = 0.0f;
    float secondChargeScale = 1.0f;
};

struct ShotProfile {
    AttackParam attack;
    float chargeTime = 0.0f;
    float recoveryTime = 0.0f;
    float interval = 0.0f;
    RangeF shotCount{0.0f, 0.0f};
    float bulletSpeed = 0.0f;
    float bulletLifetime = 0.0f;
    float spawnHeightOffset = 0.0f;
};

struct WaveProfile {
    AttackParam attack;
    float chargeTime = 0.0f;
    float recoveryTime = 0.0f;
    float speed = 0.0f;
    float maxDistance = 0.0f;
    float spawnForwardOffset = 0.0f;
    float spawnHeightOffset = 0.0f;
};

struct RushProfile : AttackProfile {
    float moveDuration = 0.0f;
    float speed = 0.0f;
    float attackForwardOffset = 0.0f;
    float attackHeightOffset = 0.0f;
};

struct EnemyAttackSet {
    SmashProfile smash;
    SweepProfile sweep;
    ShotProfile shot;
    WaveProfile wave;
    RushProfile rush;
};

struct EnemyAiProfile {
    WeightedActionSet nearPressure;
    WeightedActionSet midPressure;
    WeightedActionSet counterPunish;
    WeightedActionSet antiGuardNear;
    WeightedActionSet chaseFar;
};

struct EnemyPresentationProfile {
    float smashTellTime = 0.12f;
    float sweepTellTime = 0.10f;
    float warpStartTime = 0.20f;
    float warpMoveTime = 0.10f;
    float warpEndTime = 0.20f;
};

struct EnemyConfig {
    EnemyCoreProfile core;
    EnemyAttackSet attacks;
    EnemyAiProfile ai;
    EnemyPresentationProfile presentation;
};
```

### プレイヤー
```cpp
struct PlayerConfig {
    float maxHp = 100.0f;
    float initialHp = 100.0f;
    float moveSpeed = 5.0f;
    float minTargetDistance = 2.7f;
    float damageTakenScale = 1.0f;
};

struct PlayerRuntimeState {
    float hp = 100.0f;
    float yaw = 0.0f;
    DirectX::XMFLOAT3 velocity = {0, 0, 0};
    DirectX::XMFLOAT3 knockbackVelocity = {0, 0, 0};
    bool isGuarding = false;
};
```

### 剣
```cpp
struct SwordConfig {
    DirectX::XMFLOAT3 size{0.2f, 0.2f, 0.6f};
    DirectX::XMFLOAT3 counterSize{1.0f, 0.8f, 1.6f};
    float counterEarlyThreshold = 0.10f;
    float counterLateThreshold = 0.45f;
};

struct SwordRuntimeState {
    bool isSlashMode = false;
    bool isGuard = false;
    bool isCounterStance = false;
    bool justCountered = false;
    bool justCounterFailed = false;
    bool justCounterEarly = false;
    bool justCounterLate = false;
    float counterStateTimer = 0.0f;
    float recoveryReaction = 0.0f;
};
```

## 命名ルール
- `Profile`
  まとまりのある設定群
- `Config`
  クラス全体の設定
- `RuntimeState`
  実行時状態
- `Preset`
  保存/ロード用の入れ物
- `RangeF`
  min/max
- `Weights`
  重み
- `Chances`
  確率群

避けたいもの:
- `xxxParam`, `xxxData`, `xxxInfo` を無差別に使うこと
- 何が設定で何が現在値かわからない名前

## 保存形式の方針
今の行ベーステキスト保存は、項目追加に弱いです。
おすすめは次のどちらかです。

1. 当面は struct を整理し、I/Oは後回し
2. 将来的に JSON か TOML に移す

理想:
```json
{
  "core": {
    "maxHp": 1000.0,
    "nearAttackDistance": 4.0
  },
  "attacks": {
    "smash": {
      "attack": { "damage": 10.0, "knockback": 4.0 },
      "chargeTime": 0.45
    }
  }
}
```

理由:
- 追加に強い
- 名前付きなので壊れにくい
- デバッグ時に人が読める

## いまのコードからの移行方針

### Phase 1
- `Enemy` の大量の `float` を分類する
- まだロジックは変えない
- `EnemyConfig` と `EnemyRuntimeState` を作る

### Phase 2
- `smash`, `sweep`, `shot`, `wave`, `rush` を `AttackSet` に寄せる
- `EditSmashParam()` のような accessor は新構造へ付け替える

### Phase 3
- `Player` と `Sword` も同じ構造へそろえる

### Phase 4
- `PresetIO` を新構造に合わせて整理する
- 必要なら JSON/TOML に移行する

## このプロジェクトでのおすすめ結論
このゲームでは、次の形が一番バランスが良いです。

- `Enemy`
  - `EnemyConfig`
  - `EnemyRuntimeState`
- `Player`
  - `PlayerConfig`
  - `PlayerRuntimeState`
- `Sword`
  - `SwordConfig`
  - `SwordRuntimeState`

さらに敵だけは、
- `EnemyAttackSet`
- `EnemyAiProfile`
- `EnemyPresentationProfile`

まで切るのがおすすめです。

これなら、
- コードは読みやすい
- 技追加もしやすい
- 仕様書も書きやすい
- パラメータ調整UIも作りやすい

逆に、今のまま `Enemy` に `float` を追加し続ける形は、今後いちばんつらくなります。
