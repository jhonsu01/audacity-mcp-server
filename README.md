<p align="center"><img src="icon.png" width="120" alt="Audacity Bridge icon"></p>

<h1 align="center">Audacity MCP Server</h1>

<p align="center">
  <b>English</b> · <a href="README.es.md">Español</a> · <a href="README.pt-BR.md">Português</a> · <a href="README.fr.md">Français</a> · <a href="README.de.md">Deutsch</a> · <a href="README.ru.md">Русский</a> · <a href="README.zh-CN.md">简体中文</a> · <a href="README.ja.md">日本語</a> · <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  Let Claude edit audio with the <b>Audacity 4</b> installed on your Windows PC —<br>
  split, trim, convert, resample, apply effects, mix and join, plus an <b>in-app Audacity extension</b>.
</p>

---

## Why

Audacity 4 is a great open-source audio editor, but it has **no scripting interface** (no `mod-script-pipe`, no command-line export). This [Model Context Protocol](https://modelcontextprotocol.io) server gives Claude access to Audacity's own audio engine, so you can ask things like:

> *"Split every recording in `D:\voice` into 15-second pieces, one folder per file."*
> *"Convert `D:\podcast\ep1.wav` to FLAC 24-bit at 48 kHz."*
> *"Apply a high-pass at 80 Hz, a compressor and normalize to -1 dBFS on `D:\take3.wav`."*

Inspired by [VectorMagic-mcp-server](https://github.com/jhonsu01/VectorMagic-mcp-server).

## How it works

| Part | What it is |
| --- | --- |
| **Native engine** (`aumcp-engine.exe`, C++) | Loads Audacity 4's own codec libraries at runtime (**libsndfile 1.2.2**, **mpg123**) from its install folder, so it reads and writes exactly what your Audacity supports. Streams long files; high-quality windowed-sinc resampling. |
| **MCP server** (Node.js) | 10 tools for Claude. Optional FFmpeg for MP3/M4A/AAC/WMA export and exotic imports. |
| **Audacity extension** ("MCP Audio Tools") | An official Audacity 4 extension (JavaScript + native DLL) that adds effects to Audacity's own menus. Installed with one tool call. |

## Tools

| Tool | What it does |
| --- | --- |
| `split_audio` | Cut one or many files into N-second pieces (default 15 s), one folder per input, numbered `<name>_001.wav`… |
| `convert_audio` | Change format, bit depth, sample rate or channels of one or many files |
| `trim_audio` | Keep a time range, with optional fade in/out |
| `apply_effects` | Chain of effects (see below) |
| `mix_audio` | Mix files on top of each other with per-file gain and start offset |
| `concat_audio` | Join files with an equal-power crossfade or a silent gap |
| `get_audio_info` | Format, rate, channels, duration, peak and RMS level |
| `open_in_audacity` | Open results in the Audacity 4 editor |
| `install_audacity_extension` | Install / update / remove the in-app extension |
| `get_audacity_status` | Audacity path and version, engine, FFmpeg, formats, effects |

## Formats

| | |
| --- | --- |
| **Input (native)** | wav, aiff, flac, ogg, opus, mp3, mp2, caf, w64, rf64, au, voc, sd2, iff, paf… |
| **Input (via FFmpeg)** | m4a, aac, mp4, wma, wv, ac3, webm, mkv, ape… |
| **Output (native)** | wav, flac, ogg (Vorbis), opus, aiff, caf, w64, rf64, au |
| **Output (via FFmpeg)** | mp3, m4a, aac, wma |
| **Options** | bit depth `pcm8/16/24/32`, `float32/64` · any sample rate · mono/stereo · Vorbis/Opus quality · FLAC compression · bitrate |

## Effects

`gain` · `normalize` · `fade_in` · `fade_out` (linear, exponential, logarithmic, S-curve) · `trim` · `pad` · `trim_silence` · `reverse` · `speed` · `invert` · `remove_dc` · `highpass` · `lowpass` · `bandpass` · `notch` · `eq` · `bass` · `treble` · `echo` · `compressor` · `limiter` · `noise_gate` · `mono` · `stereo` · `swap_channels` · `pan`

Example: `[{"type":"highpass","frequency":80},{"type":"compressor","threshold_db":-20,"ratio":3},{"type":"normalize","peak_db":-1}]`

## Inside Audacity (extension)

After `install_audacity_extension` and restarting Audacity 4:

| Menu | Effect |
| --- | --- |
| Tools › Extension › audacity-mcp-server | **Split into Files (MCP)**: writes the selection (or each selected track) as N-second files |
| | **Export Selection to File (MCP)**: WAV, FLAC, OGG, Opus, AIFF |
| | **Import Audio File as Track (MCP)**: any supported file (including MP3) as a new track |
| Analyze › Extension › audacity-mcp-server | **Labels Every N Seconds (MCP)**: a label track with one region per interval |
| Tools | **MCP Audio Tools: status**: checks the native library |

All of them support undo and show a progress bar.

## Requirements

- Windows 10/11 x64
- **Audacity 4** (tested with **4.0.1**)
- Node.js ≥ 20 (bundled with Claude Desktop for `.mcpb` installs)
- Optional: FFmpeg (only for MP3/M4A/AAC/WMA export or M4A/AAC/WMA/WavPack import)

## Installation

### Claude Desktop (recommended)

1. Download `audacity-mcp-server.mcpb` from the [latest release](https://github.com/jhonsu01/audacity-mcp-server/releases/latest).
2. Double-click it (or drag it onto Claude Desktop → *Settings → Extensions*) and click **Install**.
3. Optional: if Audacity is not in `C:\Program Files\Audacity 4`, set its folder in the extension settings; set the FFmpeg path if it is not on `PATH`.
4. Optional: ask Claude *"install the Audacity extension"*, then restart Audacity.

### Claude Code / other MCP clients

```bash
git clone https://github.com/jhonsu01/audacity-mcp-server.git
cd audacity-mcp-server
npm install
npm run build:all
claude mcp add audacity -- node "%CD%\dist\bundle.cjs"
```

`build:all` compiles the native parts and needs Visual Studio with the C++ workload.

## Configuration

| Variable | Meaning |
| --- | --- |
| `AUDACITY_DIR` | Audacity 4 install folder (or `Audacity4.exe`). Default: `C:\Program Files\Audacity 4` |
| `FFMPEG_PATH` | `ffmpeg.exe` or its folder. Default: from `PATH` |
| `AUDACITY_MCP_TIMEOUT` | Max seconds per job (default 1800) |

## Safety

- Files are **never overwritten** unless `overwrite: true`.
- All paths must be absolute; the input is never modified.
- Everything runs locally, with no network access.

## Development

```bash
npm run build:native   # aumcp-engine.exe + audacity_mcp_native.dll (MSVC)
npm run build          # TypeScript + bundle
npm run test:e2e       # 25 end-to-end checks over real MCP stdio (synthetic audio)
npm run pack:mcpb      # audacity-mcp-server.mcpb
```

## License

GPL-3.0-only. The extension ABI header comes from Audacity (GPL-3.0).
Audacity® is a trademark of Muse Group. This project is not affiliated with or endorsed by Audacity or Muse Group.
