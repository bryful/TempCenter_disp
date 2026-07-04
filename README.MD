# TempCenter_disp

XIAO ESP32S3 + ST7789B ディスプレイで動作する、温湿度表示端末です。
自機の AHT20 / BMP280 を読み取りつつ、別ノード（TempCenter）から取得した複数センサーデータを一覧表示します。

センサーアプリ [TempCenter_](https://github.com/bryful/TempCenter) と対として使用します。

## 概要

- 対象ボード: Seeed Studio XIAO ESP32S3
- 画面: ST7789 240x320（SPI, LovyanGFX）
- 自機センサー:
	- AHT20（温湿度）
	- BMP280（気圧）
- 通信:
	- Wi-Fi 固定 IP
	- NTP 同期（JST）
	- TempCenter へ TCP 接続（port 5000）

## 主な機能

- 自機の温度・湿度・気圧を周期取得して表示
- TempCenter から最大 6 台分のセンサーデータを取得して一覧表示
- ボタン操作または無操作タイムアウトでスクリーンセーバー（バックライト OFF）
- Wi-Fi 断時は自動再接続を試行

## 対応環境

- PlatformIO
- Arduino Framework (ESP32)

## 依存ライブラリ

`platformio.ini` の `lib_deps`:

- lovyan03/LovyanGFX@^1.2.7
- bblanchon/ArduinoJson@^7.4.1
- adafruit/Adafruit AHTX0@^2.0.5
- adafruit/Adafruit BMP280 Library@^2.6.8

## ディレクトリ構成

```text
.
├─ platformio.ini
├─ include/
│  ├─ secrets.h
│  └─ secrets.h.example
├─ src/
│  ├─ main.cpp
│  └─ LGFX_XIAO_ESP32S3_SPI_ST7789B.hpp
└─ lib/
```

## 配線（コード設定ベース）

### ST7789（SPI）

`src/LGFX_XIAO_ESP32S3_SPI_ST7789B.hpp` の設定値:

- SCLK: GPIO7
- MOSI: GPIO9
- DC: GPIO2
- CS: GPIO4
- RST: GPIO1
- BL: GPIO3

### その他

- ボタン入力: GPIO43（`INPUT_PULLUP`）
- LED: GPIO21
- I2C センサー:
	- AHT20: 0x38
	- BMP280: 0x77（見つからない場合 0x76）

## 設定手順

1. `include/secrets.h.example` を参考に `include/secrets.h` を作成
2. Wi-Fi とネットワーク情報を設定
3. TempCenter の IP（`SECRET_SERVER_IP`）を設定

`include/secrets.h.example` の設定例:

```cpp
#define SECRET_SSID     "YOUR_WIFI_SSID"
#define SECRET_PASSWORD "YOUR_WIFI_PASSWORD"

#define SECRET_LOCAL_IP   192, 168, 1, 150
#define SECRET_GATEWAY    192, 168, 1, 1
#define SECRET_SUBNET     255, 255, 255, 0
#define SECRET_DNS_1      192, 168, 1, 1
#define SECRET_DNS_2      8, 8, 8, 8

#define SECRET_SERVER_IP 192, 168, 1, 151
```

## ビルド・書き込み

プロジェクトルートで実行:

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

## 子機通信仕様（TempCenter 取得）

本機は TempCenter（`SERVER_PORT=5000`）へ TCP 接続し、以下を送信します。

```text
GET_DATA\n
```

受信は JSON を想定し、`sensors` 配列を読みます。各要素で主に利用するキー:

- `status`（1 で有効）
- `aht_temp`（温度）
- `humidity`（湿度）

想定レスポンス例:

```json
{
	"sensors": [
		{ "status": 1, "aht_temp": 24.3, "humidity": 55.1 },
		{ "status": 1, "aht_temp": 23.8, "humidity": 58.4 }
	]
}
```

## 表示仕様（概要）

- ヘッダー:
	- タイトル
	- 時刻
	- 自機センサ値（温度・湿度・気圧）
- ボディ:
	- 1〜6 の各子機データ（温度・湿度）
	- `status != 1` は `--.-` 表示
- フッター:
	- 動作インジケータ
	- 自機 IP

## スクリーンセーバー

- 自動: 60 秒無操作でバックライト OFF
- 手動: ボタン押下で ON/OFF 切り替え

## トラブルシュート

- Wi-Fi 接続できない:
	- `include/secrets.h` の SSID/PASSWORD と固定 IP 設定を再確認
	- ネットワーク内で IP 重複がないか確認
- センサーが読めない:
	- I2C 配線と電源確認
	- BMP280 のアドレス（0x76/0x77）確認
- TempCenter 取得失敗:
	- `SECRET_SERVER_IP` が正しいか確認
	- TempCenter 側が port 5000 で待受中か確認

## メモ

- フォントは `src/` 配下の `lgfxJapanGothic_*` を利用
- `platformio.ini` は XIAO ESP32S3 向け設定済み

