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

## PlatformIOで表示

```bash
cd arduino
pio run -t upload
pio device monitor
```

シリアルモニターは115200 baudです。画面更新には数十秒かかります。正常時は画像転送の
進捗、BUSY解除、更新完了が表示されます。

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
- `image_data.h`は大きいため `.gitignore` 対象です。作例として公開する場合だけ明示的に追加してください。

## ライセンス

MIT License。詳細は [LICENSE](LICENSE) を参照してください。
