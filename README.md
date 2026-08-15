# spectra6-photo-converter

写真を **Waveshare 7.3inch e-Paper HAT (E)** 向けの6色画像へ変換し、
**Arduino Nano ESP32** から表示するためのサンプルプロジェクトです。

対象パネルは 800 × 480 pixel の **E Ink Spectra 6**。黒・白・黄・赤・青・緑の
6色（4 bpp、2 pixel/byte）へ変換します。写真らしさを保つため、CIEDE2000による
知覚色差、Stucki error diffusion、エッジ適応、軽い肌色補正を組み合わせています。

## 特徴

- 800 × 480へアスペクト比を保った中央クロップ
- CIEDE2000でSpectra 6の近似パレットへ量子化
- Stucki誤差拡散でFloyd–Steinbergより広く誤差を分散
- 輪郭ではディザリングを少し弱めるエッジ適応処理
- 肌領域では赤・黄をわずかに優遇する連続的な補正
- 192,000 byteのCヘッダーを生成
- 変換後プレビュー、前処理画像、エッジマップを保存
- PlatformIO用Arduino Nano ESP32表示プログラムを同梱
- スマートフォンのブラウザから任意の写真を選択・表示
- Nano ESP32が作るローカルWi-Fiへ直接接続（クラウド不要）

## 必要なもの

- Waveshare 7.3inch e-Paper HAT (E)
- Arduino Nano ESP32
- ジャンパ線
- Python 3.10以降
- PlatformIO

## 配線

| HAT (E) | Arduino Nano ESP32 |
|---|---|
| VCC | **3V3** |
| GND | GND |
| DIN | D11 (MOSI) |
| SCLK | D13 (SCK) |
| CS | D10 |
| DC | D9 |
| RST | D8 |
| BUSY | D7 |

HATの `SPI Select` は `0`（4-line SPI）にします。

> [!CAUTION]
> この実機では3.3V給電で動作確認しています。5V給電時に電源周辺が発熱したため、
> 本プロジェクトでは5V給電を推奨しません。通電中にFPCを抜き差ししないでください。

> [!IMPORTANT]
> FPCの向きが逆だとBUSY timeoutになり、表示されません。電源を外してから、接点面、
> 挿入深さ、左右の平行、コネクタのロックを確認してください。

## セットアップ

```bash
git clone https://github.com/YOUR_NAME/spectra6-photo-converter.git
cd spectra6-photo-converter
python -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
```

macOSで `python` がない場合は `python3` を使用してください。

## 画像変換

```bash
python convert_epaper.py photo.jpg arduino/src/image_data.h
```

次のファイルが生成されます。

- `arduino/src/image_data.h`: Nano ESP32へ組み込む192,000 byteの画像
- `epaper_preview.png`: 6色変換結果のプレビュー
- `epaper_source_processed.png`: リサイズ・前処理後の画像
- `epaper_edges.png`: エッジ適応に使用したマップ

入力はJPEG、PNGなどPillowが読める形式を利用できます。横長でない画像は中央が残るよう
トリミングされます。

## スマートフォンから表示

このブランチのファームウェアは、Nano ESP32を設定用Wi-Fiアクセスポイントとして動作させ、
必要に応じて自宅Wi-Fiにも接続します。
写真のリサイズ、中央クロップ、6色変換、誤差拡散はスマートフォンのブラウザ内で行い、
完成した192,000 byteのデータだけをNano ESP32へ送信します。

1. PlatformIOでファームウェアを書き込みます。

```bash
cd arduino
pio run -t upload
pio device monitor
```

2. スマートフォンのWi-Fi設定から次へ接続します。

| 項目 | 値 |
|---|---|
| SSID | `Spectra6-Frame` |
| パスワード | `spectra6` |

3. ブラウザで `http://192.168.4.1/` を開きます。
4. 写真を選び、6色プレビューを確認します。
5. 「e-Paperに表示」を押します。

### 自宅Wi-Fiへ接続

画面下部の「自宅Wi-Fi設定」にSSIDとパスワードを入力して保存すると、本体が再起動して
自宅Wi-Fiへ接続します。以後は同じネットワーク上のスマートフォンから次のいずれかで開けます。

- `http://spectra6-frame.local/`
- シリアルモニターに表示されるIPアドレス（例：`http://192.168.1.50/`）

SSIDとパスワードはNano ESP32の不揮発領域へ保存されます。自宅Wi-Fiへの接続に失敗しても、
設定用SSID `Spectra6-Frame` は残るため、`http://192.168.4.1/`から再設定できます。

スマートフォンに「インターネット接続なし」と表示されても正常です。この通信はNano ESP32と
スマートフォンの間だけで完結します。アップロード後、パネル更新が完了するまで約25秒かかります。

> [!NOTE]
> スマートフォン版は処理時間と互換性を優先し、ブラウザ上のFloyd–Steinberg誤差拡散を使います。
> PC版`convert_epaper.py`はCIEDE2000とStuckiを使うため、画質を最優先する場合はPC版が適しています。

## PlatformIOで書き込み

```bash
cd arduino
pio run -t upload
pio device monitor
```

シリアルモニターは115200 baudです。起動後にSSIDとURLが表示されます。画像受信後は転送の
進捗、BUSY解除、更新完了が表示されます。受信した画像はLittleFSへ保存されます。

## データ形式

1 pixelを4 bitで表し、左のpixelを上位ニブル、右のpixelを下位ニブルへ格納します。

| 色 | コード |
|---|---:|
| 黒 | `0x0` |
| 白 | `0x1` |
| 黄 | `0x2` |
| 赤 | `0x3` |
| 青 | `0x5` |
| 緑 | `0x6` |

`800 × 480 ÷ 2 = 192,000 byte` です。Arduino側はサイズが一致しない場合、更新を中止します。

## 画質調整

`convert_epaper.py`冒頭の値で調整できます。

- `DITHER_STRENGTH`: 大きいほど階調が増え、小さいほど面がまとまる
- `EDGE_DITHER_MIN`: 輪郭部分に残すディザリング量
- `CONTRAST` / `SATURATION` / `SHARPNESS`: 前処理
- `SKIN_BIAS_STRENGTH`: 肌色への軽い補正量

パレットRGBは実機の公式測色値ではなく、変換とプレビュー用の近似値です。照明や個体差に
合わせて調整すると、さらに改善できます。

## 注意事項

- 電子ペーパーは動画用途には向きません。
- 更新中の点滅は正常です。
- 更新完了後はDeep Sleepコマンドを送ります。
- 異常発熱、異臭、BUSY timeoutが続く場合は直ちに電源を外してください。
- 本コードはWaveshareの公開シーケンスを参考にした独立実装で、Waveshare公式サポート品ではありません。
- スマートフォン版では`image_data.h`は使用しません。PC変換版の参考ファイルは残しています。
- アクセスポイントのパスワードは公開値です。必要に応じて`main.cpp`の`AP_PASSWORD`を変更してください。
- 現在のファームウェアは操作性を優先してWi-Fiを常時有効にします。乾電池で長期間動作させる
  最終版では、設定ボタンを押したときだけWi-Fiを起動する省電力モードの追加を推奨します。

## ライセンス

MIT License。詳細は [LICENSE](LICENSE) を参照してください。
