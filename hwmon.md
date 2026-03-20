# hwmon プロジェクト

LibreHardwareMonitor の HTTP API からセンサーデータを取得し、`top` 風にターミナル表示するツール。

## 概要

- **データソース**: `http://localhost:8085/data.json`（LibreHardwareMonitor の Web API）
- **実行方法**: `watch -n 2 --color ./hwmon`

## ファイル構成

```
hwmon.sh   # メインスクリプト
```

## 環境変数

| 変数 | デフォルト | 説明 |
|---|---|---|
| `HWMON_URL` | `http://localhost:8085/data.json` | LHM API の URL |
| `COLUMNS` | （自動） | `watch` が設定するターミナル幅 |

---

## 全体の処理フロー

```
1. 環境変数 HWMON_URL を読み込む（未設定時はデフォルト URL）
2. ターミナル幅を取得（COLUMNS 環境変数 or システム API。デフォルト 80）
3. LHM API へ HTTP GET リクエスト（タイムアウト: 3 秒）
   - 失敗時: エラーメッセージを赤字で表示して終了
4. レスポンス JSON をパース
5. ルートの第 1 子ノードからマシン名（Text）を取得
6. ツリーを再帰的に走査し hardware_map を構築（下記参照）
7. ヘッダー行を出力（区切り線 + タイトル + 区切り線）
8. 各セクションを順に出力:
   CPU → GPU → Memory → NVMe → Battery → Network → Claude
9. フッター行を出力（区切り線）
```

---

## データモデル

### LHM JSON のノード種別

LHM の JSON はノードの木構造。ノードには 3 種類ある：

| 種別 | 判別条件 | 主なフィールド |
|---|---|---|
| ハードウェアノード | `HardwareId` が非空 | `Text`（名称）、`HardwareId`、`ImageURL`、`Children` |
| センサーグループノード | `HardwareId` 空 かつ `SensorId` 空 | `Text`（"Temperatures" 等）、`ImageURL`、`Children` |
| センサーノード | `SensorId` が非空 | `Text`、`SensorId`、`Value`（文字列）、`Type` |

### ハードウェア種別の判定

ハードウェアノードの `ImageURL` 末尾ファイル名で種別を判定する：

| ImageURL 末尾 | 種別 | 備考 |
|---|---|---|
| `cpu.png` | `cpu` | |
| `ati.png` | `gpu` | AMD GPU |
| `nvidia.png` | `gpu` | NVIDIA GPU |
| `hdd.png` | `nvme` | NVMe / HDD |
| `battery.png` | `battery` | |
| `nic.png` | `nic` | |
| `ram.png` | `ram` / `vram` | `HardwareId` が `/vram` で始まる場合は `vram`、それ以外は `ram` |
| `mainboard.png` 等 | 無視 | センサーを収集しない |

### hardware_map の構造

ツリー走査で以下の構造を構築する：

```
hardware_map:
  "cpu"     → [ {name, hw_id, sensors}, ... ]
  "gpu"     → [ {name, hw_id, sensors}, ... ]
  "nvme"    → [ {name, hw_id, sensors}, ... ]
  "battery" → [ {name, hw_id, sensors}, ... ]
  "nic"     → [ {name, hw_id, sensors}, ... ]
  "ram"     → [ {name, hw_id, sensors} ]
  "vram"    → [ {name, hw_id, sensors} ]
```

- 同種のハードウェアが複数ある場合（NVMe × 2 等）はリストに複数エントリが入る
- `name`: ハードウェアノードの `Text`（LHM が返す名称）
- `hw_id`: ハードウェアノードの `HardwareId`（例: `/amdcpu/0`）
- `sensors`: センサーの相対パス → 値文字列 の辞書

### センサーの相対パス

`SensorId` から `HardwareId` のプレフィックスを除いたパスをキーとして使う：

```
SensorId  : /amdcpu/0/temperature/2
HardwareId: /amdcpu/0
相対パス  : /temperature/2
```

これにより、センサー参照がベンダー名（`amdcpu` / `intelcpu` 等）に依存しなくなる。

### ツリー走査アルゴリズム

```
walk(node, parent_hw=None):
  if node.HardwareId が非空:
    htype = ImageURL から種別を判定
    if htype が認識済み:
      hw = {name, hw_id, sensors:{}} を hardware_map[htype] に追加
      子を walk(child, parent_hw=hw) で再帰
    else:
      子を walk(child, parent_hw=parent_hw) で再帰（parent_hw を引き継ぐ）
  elif node.SensorId が非空 かつ parent_hw が存在:
    parent_hw.sensors[相対パス] = node.Value
    子を walk(child, parent_hw=parent_hw) で再帰
  else:  # グループノードまたはルート
    子を walk(child, parent_hw=parent_hw) で再帰（parent_hw を引き継ぐ）
```

---

## 共通仕様

### バーグラフ

```
バー幅 = max(8, ターミナル幅 - プレフィックス長 - サフィックス長)
塗り幅 = min(floor(値% / 100 × バー幅), バー幅)
表示  = [塗りブロック × 塗り幅] + [空きブロック × (バー幅 - 塗り幅)]
```

- プレフィックス長・サフィックス長は **ANSI エスケープを除いた表示桁数** で計算する
- `watch` が `COLUMNS` を設定するためターミナルリサイズに自動追従する

### センサー値の型変換

LHM はセンサー値を `"1.23 °C"` のような文字列で返す。
先頭の数値部分を float に変換して使う。変換できない場合は `0.0` とする。
スループット等、文字列のまま表示する項目はそのまま使う。

### 色の閾値ルール

値が warn 未満 → 緑、warn 以上 crit 未満 → 黄、crit 以上 → 赤（通常方向）。
`reverse=true` のときは warn 以上 → 緑、crit 以上 warn 未満 → 黄、crit 未満 → 赤（バッテリー残量などに使用）。

---

## 表示セクション詳細仕様

> センサーはすべて相対パスで表記する。

### CPU

**ヘッダー行**: `▌ CPU  {cpu.name}`（名称は `hardware_map["cpu"][0].name` から取得）

**センサー**

| 項目 | 相対パス | 単位 |
|---|---|---|
| CPU 温度 | `/temperature/2` | °C |
| 総消費電力 | `/power/0` | W |
| 総負荷 | `/load/0` | % |
| コア最大負荷 | `/load/1` | % |
| 平均クロック | `/clock/1` | MHz |
| 実効クロック | `/clock/2` | MHz |
| コア別負荷 (#1〜#8) | `/load/2` 〜 `/load/9` | % |
| コア別クロック (#1〜#8) | `/clock/3`, `5`, `7`, `9`, `11`, `13`, `15`, `17`（2飛び） | MHz |

**色閾値**

| 項目 | warn | crit |
|---|---|---|
| 温度 | 70°C | 85°C |
| 総負荷 | 50% | 80% |
| 電力 | 15W | 25W |
| コア別負荷 | 60% | 85% |

**出力行**

```
  Temp  XX.X°C   Power  XX.XW
  Total [████░░░░░░░░░░░░░░░░░]  XX.X%  CoreMax  XX.X%
  Clock avg XXXX MHz  eff XXXX MHz
  Cores #1 XX%  #2 XX%  ...  #8 XX%
```

- Total バーのプレフィックス長: 8（`"  Total "`）
- サフィックス: `"  XX.X%  CoreMax  XX.X%"` の表示桁数

---

### GPU

**ヘッダー行**: `▌ GPU  {gpu.name}`

**センサー**

| 項目 | 相対パス | 単位 |
|---|---|---|
| GPU 温度 | `/temperature/4` | °C |
| GPU 負荷 | `/load/0` | % |
| 消費電力 | `/power/0` | W |
| GPU クロック | `/clock/0` | MHz |
| VRAM 使用量 | `/smalldata/0` | MB |
| VRAM 合計 | `/smalldata/2` | MB |

VRAM 使用率 = 使用量 ÷ 合計 × 100（合計が 0 の場合は 0%）

**色閾値**

| 項目 | warn | crit |
|---|---|---|
| 温度 | 70°C | 85°C |
| GPU 負荷 | 50% | 80% |
| VRAM 使用率 | 70% | 90% |

**出力行**

```
  Temp  XX.X°C   Power  XX.XW   Clock XXXX MHz
  Load  [████░░░░░░░░░░░░░░░░░]  XX.X%
  VRAM  [████░░░░░░░░░░░░░░░░░]  XX.X%  XXX/XXX MB
```

---

### Memory

**ヘッダー行**: `▌ Memory`

**センサー**（`ram` = `hardware_map["ram"][0]`、`vram` = `hardware_map["vram"][0]`）

| 項目 | hw | 相対パス | 単位 |
|---|---|---|---|
| RAM 使用率 | ram | `/load/0` | % |
| RAM 使用量 | ram | `/data/0` | GB |
| RAM 空き容量 | ram | `/data/1` | GB |
| 仮想メモリ使用率 | vram | `/load/1` | % |
| 仮想メモリ使用量 | vram | `/data/2` | GB |
| 仮想メモリ空き容量 | vram | `/data/3` | GB |

RAM / 仮想メモリ合計 = 使用量 + 空き容量

**色閾値**（RAM・仮想メモリ共通）: warn=70%、crit=90%

**出力行**

```
  RAM   [████░░░░░░░░░░░░░░░░░]  XX.X%  XX.X/XX.X GB
  Virt  [████░░░░░░░░░░░░░░░░░]  XX.X%  XX.X/XX.X GB
```

---

### NVMe

**ヘッダー行**: `▌ NVMe  {nvme.name}`

**センサー**

| 項目 | 相対パス | 単位 |
|---|---|---|
| 温度 | `/temperature/0` | °C |
| 使用率 | `/load/30` | % |
| 読み取り速度 | `/throughput/54` | 文字列（単位込み） |
| 書き込み速度 | `/throughput/55` | 文字列（単位込み） |
| 読み取りアクティビティ | `/load/51` | % |
| 書き込みアクティビティ | `/load/52` | % |
| 空き容量 | `/data/31` | GB |
| 残寿命 | `/level/20` | % |

スループット値は LHM が返す文字列をそのまま右揃え 12 桁で表示する。

**色閾値**

| 項目 | warn | crit |
|---|---|---|
| 温度 | 55°C | 70°C |
| 使用率 | 70% | 90% |

残寿命は常に緑で表示。

**出力行**

```
  Temp  XX.X°C   Life XX%   Free XX.X GB
  Used  [████░░░░░░░░░░░░░░░░░]  XX.X%
  Read       XXX.X MB/s  (XX.X% active)
  Write      XXX.X MB/s  (XX.X% active)
```

---

### Battery

**ヘッダー行**: `▌ Battery  {battery.name}`

バッテリーの `HardwareId`（例: `/battery/PABAS0241231_1`）は LHM 再起動で末尾数字が変わるが、
ツリー走査時に `ImageURL == "battery.png"` で自動識別するため、呼び出し側での特別処理は不要。

**センサー**

| 項目 | 相対パス | 単位 |
|---|---|---|
| 充電レベル | `/level/0` | % |
| 劣化率 | `/level/1` | % |
| 電圧 | `/voltage/0` | V |
| 電流 | `/current/0` | A |
| 電力 | `/power/0` | W |
| 残容量 | `/energy/2` | mWh |
| 満充電容量 | `/energy/1` | mWh |

**充電状態の判定**

| 電流値 | 表示 |
|---|---|
| > 0 | `Charging +XX.XW`（緑） |
| < 0 | `Discharging -XX.XW`（黄） |
| = 0 | `AC (idle)`（DIM） |

**色閾値**（reverse モード）

| レベル | 色 |
|---|---|
| 30% 以上 | 緑 |
| 15% 以上 30% 未満 | 黄 |
| 15% 未満 | 赤 |

劣化率は常に黄で表示。

**出力行**

```
  [████░░░░░░░░░░░░░░░░░]  XX%  XXXXX/XXXXX mWh
  Charging +XX.XW   X.XXXV   Degradation XX.X%
```

---

### Network

**ヘッダー行**: `▌ Network`

`hardware_map["nic"]` リストを順に走査し、累積上り（`/data/2`）と累積下り（`/data/3`）がともに 0 の NIC はスキップする（一度も通信が行われていない未使用 NIC とみなす）。NIC 名は LHM の `Text`（OS の NIC 名）をそのまま使う。特定 NIC の絞り込みや固定ラベルへの置き換えは行わない。

**センサー**（各 NIC エントリに対して）

| 項目 | 相対パス | 単位 |
|---|---|---|
| 上り速度 | `/throughput/7` | 文字列（単位込み） |
| 下り速度 | `/throughput/8` | 文字列（単位込み） |
| 累積上り | `/data/2` | GB |
| 累積下り | `/data/3` | GB |

上り速度 + 下り速度の数値合計 > 0 → 矢印を緑、= 0 → DIM（暗色）。

**出力行**（NIC ごとに 1 行）

```
  {name:<14}  ↑  XXX.X KB/s   ↓  XXX.X KB/s  (total ↑X.XXG ↓X.XXG)
```

速度文字列は右揃え 12 桁。累積値は DIM 色。

---

### Claude

**ヘッダー行**: `▌ Claude  claude-sonnet-4-6  (today / local stats)`

**データソース**

`~/.claude/projects/**/*.jsonl` を再帰的に走査（API 呼び出しなし）。

**集計対象の条件**（1 行 = 1 JSON オブジェクト）

- `type` が `"assistant"` であること
- `timestamp` が今日の ISO 日付（`YYYY-MM-DD`）で始まること
- `message.usage` オブジェクトが存在すること

**集計項目**

| 項目 | JSON パス |
|---|---|
| 入力トークン | `message.usage.input_tokens` |
| 出力トークン | `message.usage.output_tokens` |
| キャッシュ読み取りトークン | `message.usage.cache_read_input_tokens` |
| キャッシュ作成トークン | `message.usage.cache_creation_input_tokens` |
| セッション数 | `sessionId` の一意な値の数 |
| メッセージ数 | 条件を満たした行の数 |

**推定コスト計算**（Sonnet 4.6 料金、USD/MTok）

| トークン種別 | 単価 |
|---|---|
| 入力 | $3.00 |
| 出力 | $15.00 |
| キャッシュ読み取り | $0.30 |
| キャッシュ作成 | $3.75 |

コスト = (各トークン数 × 単価の合計) ÷ 1,000,000

**トークン数の表示形式**

- 100 万未満: `X.Xk`（千単位、小数点以下 1 桁）
- 100 万以上: `X.XXM`（百万単位、小数点以下 2 桁）

**出力行**

```
  Sessions X   Messages X
  Tokens   in X.Xk  out X.Xk  cache-read X.Xk  cache-create X.Xk
  Est. cost  $X.XXXX USD
```
