<p align="center"><img src="icon.png" width="120" alt="Audacity Bridge アイコン"></p>

<h1 align="center">Audacity MCP Server</h1>

<p align="center">
  <a href="README.md">English</a> · <a href="README.es.md">Español</a> · <a href="README.pt-BR.md">Português</a> · <a href="README.fr.md">Français</a> · <a href="README.de.md">Deutsch</a> · <a href="README.ru.md">Русский</a> · <a href="README.zh-CN.md">简体中文</a> · <b>日本語</b> · <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  Windows PC にインストールされた <b>Audacity 4</b> で Claude にオーディオを編集させましょう——<br>
  分割、トリミング、変換、リサンプリング、エフェクト、ミックス、結合、さらに <b>Audacity 内蔵の拡張機能</b>。
</p>

---

## なぜ

Audacity 4 は優れたオープンソースのオーディオエディターですが、**スクリプトインターフェースがありません**（`mod-script-pipe` もコマンドラインでのエクスポートもありません）。この [Model Context Protocol](https://modelcontextprotocol.io) サーバーは Claude に Audacity 自身のオーディオエンジンを使わせるので、次のように頼めます：

> *「`D:\voice` の録音をすべて 15 秒ごとに分割して、ファイルごとにフォルダーを作って」*
> *「`D:\podcast\ep1.wav` を 48 kHz・24 ビットの FLAC に変換して」*
> *「`D:\take3.wav` に 80 Hz のハイパス、コンプレッサーをかけて -1 dBFS にノーマライズして」*

[VectorMagic-mcp-server](https://github.com/jhonsu01/VectorMagic-mcp-server) にインスパイアされています。

## 仕組み

| 構成要素 | 内容 |
| --- | --- |
| **ネイティブエンジン**（`aumcp-engine.exe`、C++） | 実行時に Audacity 4 自身のコーデックライブラリ（**libsndfile 1.2.2**、**mpg123**）をインストールフォルダーから読み込むため、お使いの Audacity と同じ形式を読み書きできます。長いファイルはストリーミング処理、高品質な windowed-sinc リサンプリング。 |
| **MCP サーバー**（Node.js） | Claude 用の 10 個のツール。MP3/M4A/AAC/WMA の書き出しや珍しい形式の読み込みには FFmpeg（任意）。 |
| **Audacity 拡張機能**（「MCP Audio Tools」） | Audacity 4 公式の拡張機能（JavaScript + ネイティブ DLL）で、Audacity のメニューにエフェクトを追加します。1 回の呼び出しでインストール。 |

## ツール

| ツール | 機能 |
| --- | --- |
| `split_audio` | 1 つまたは複数のファイルを N 秒ごと（既定 15 秒）に分割。入力ごとにフォルダー、`<名前>_001.wav`… と連番 |
| `convert_audio` | 形式、ビット深度、サンプルレート、チャンネル数を変更 |
| `trim_audio` | 指定した時間範囲を残す（フェードイン/アウト可） |
| `apply_effects` | エフェクトチェーン（下記参照） |
| `mix_audio` | ファイルごとのゲインと開始オフセットで重ねてミックス |
| `concat_audio` | 等パワーのクロスフェードまたは無音を挟んで結合 |
| `get_audio_info` | 形式、レート、チャンネル、長さ、ピークと RMS レベル |
| `open_in_audacity` | 結果を Audacity 4 エディターで開く |
| `install_audacity_extension` | 内蔵拡張機能のインストール / 更新 / 削除 |
| `get_audacity_status` | Audacity のパスとバージョン、エンジン、FFmpeg、形式、エフェクト |

## 形式

| | |
| --- | --- |
| **入力（ネイティブ）** | wav, aiff, flac, ogg, opus, mp3, mp2, caf, w64, rf64, au, voc, sd2, iff, paf… |
| **入力（FFmpeg 経由）** | m4a, aac, mp4, wma, wv, ac3, webm, mkv, ape… |
| **出力（ネイティブ）** | wav, flac, ogg (Vorbis), opus, aiff, caf, w64, rf64, au |
| **出力（FFmpeg 経由）** | mp3, m4a, aac, wma |
| **オプション** | ビット深度 `pcm8/16/24/32`、`float32/64` · 任意のサンプルレート · モノラル/ステレオ · Vorbis/Opus 品質 · FLAC 圧縮 · ビットレート |

## エフェクト

`gain` · `normalize` · `fade_in` · `fade_out`（リニア、指数、対数、S カーブ） · `trim` · `pad` · `trim_silence` · `reverse` · `speed` · `invert` · `remove_dc` · `highpass` · `lowpass` · `bandpass` · `notch` · `eq` · `bass` · `treble` · `echo` · `compressor` · `limiter` · `noise_gate` · `mono` · `stereo` · `swap_channels` · `pan` · **`mouth_declick`** · **`declip`** · **`noise_reduction`** · **`click_removal`**

例：`[{"type":"highpass","frequency":80},{"type":"compressor","threshold_db":-20,"ratio":3},{"type":"normalize","peak_db":-1}]`

Voice cleanup / limpieza de voz: `[{"type":"declip"},{"type":"mouth_declick","sensitivity":6},{"type":"limiter","ceiling_db":-1}]`

## Audacity 内（拡張機能）

`install_audacity_extension` を実行して Audacity 4 を再起動すると：

| メニュー | エフェクト |
| --- | --- |
| ツール › 拡張機能 › audacity-mcp-server | **Split into Files (MCP)**：選択範囲（または選択した各トラック）を N 秒ごとのファイルに書き出し |
| | **Export Selection to File (MCP)**：WAV、FLAC、OGG、Opus、AIFF |
| | **Import Audio File as Track (MCP)**：対応する任意のファイル（MP3 を含む）を新しいトラックとして追加 |
| 解析 › 拡張機能 › audacity-mcp-server | **Labels Every N Seconds (MCP)**：間隔ごとに 1 つの範囲を持つラベルトラック |
| ツール | **MCP Audio Tools: status**：ネイティブライブラリを確認 |

すべて元に戻すに対応し、進捗バーを表示します。

## 動作要件

- Windows 10/11 x64
- **Audacity 4**（**4.0.1** で動作確認）
- Node.js ≥ 20（`.mcpb` でインストールする場合は Claude Desktop に同梱）
- 任意：FFmpeg（MP3/M4A/AAC/WMA の書き出し、または M4A/AAC/WMA/WavPack の読み込みのみ）

## インストール

### Claude Desktop（推奨）

1. [最新リリース](https://github.com/jhonsu01/audacity-mcp-server/releases/latest)から `audacity-mcp-server.mcpb` をダウンロード。
2. ダブルクリック（または Claude Desktop →「設定 → 拡張機能」にドラッグ）して**インストール**をクリック。
3. 任意：Audacity が `C:\Program Files\Audacity 4` にない場合は拡張機能の設定でフォルダーを指定。FFmpeg が `PATH` にない場合はパスを指定。
4. 任意：Claude に「Audacity 拡張機能をインストールして」と頼み、Audacity を再起動。

### Claude Code / その他の MCP クライアント

```bash
git clone https://github.com/jhonsu01/audacity-mcp-server.git
cd audacity-mcp-server
npm install
npm run build:all
claude mcp add audacity -- node "%CD%\dist\bundle.cjs"
```

`build:all` はネイティブ部分をビルドするため、C++ ワークロード付きの Visual Studio が必要です。

## 設定

| 変数 | 意味 |
| --- | --- |
| `AUDACITY_DIR` | Audacity 4 のインストールフォルダー（または `Audacity4.exe`）。既定：`C:\Program Files\Audacity 4` |
| `FFMPEG_PATH` | `ffmpeg.exe` またはそのフォルダー。既定：`PATH` から検索 |
| `AUDACITY_MCP_TIMEOUT` | 1 ジョブの最大秒数（既定 1800） |

## 安全性

- `overwrite: true` を指定しない限り、ファイルは**決して上書きされません**。
- パスはすべて絶対パスで指定。入力ファイルは変更されません。
- すべてローカルで実行され、ネットワークにはアクセスしません。

## 開発

```bash
npm run build:native   # aumcp-engine.exe + audacity_mcp_native.dll (MSVC)
npm run build          # TypeScript + バンドル
npm run test:e2e       # 実際の MCP stdio で 25 項目のエンドツーエンド検査（合成音声）
npm run pack:mcpb      # audacity-mcp-server.mcpb
```

## ライセンス

GPL-3.0-only。拡張機能の ABI ヘッダーは Audacity（GPL-3.0）由来です。
Audacity® は Muse Group の商標です。本プロジェクトは Audacity および Muse Group とは無関係であり、承認も受けていません。
