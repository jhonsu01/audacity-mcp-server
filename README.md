<p align="center"><img src="icon.png" width="120" alt="Audacity Bridge icon"></p>

# Audacity MCP Server

Let Claude edit audio with the **Audacity 4** installed on your Windows PC: split recordings into N-second pieces, trim, convert, resample, apply effects, mix and join — plus an **in-app Audacity extension** (MCP Audio Tools).

Audacity 4 has no scripting interface, so this project uses Audacity's own codec libraries (libsndfile 1.2.2, mpg123) through a small native engine, and installs an official Audacity 4 extension (JS + native DLL) that adds effects to Audacity's menus.

## Tools
| Tool | What it does |
| --- | --- |
| `split_audio` | Cut files into N-second pieces (default 15 s), one folder per input, streaming |
| `convert_audio` | wav, flac, ogg, opus, aiff, caf, w64, rf64, au (+ mp3/m4a/aac/wma via FFmpeg), bit depth, sample rate, channels |
| `trim_audio` | Keep a time range with optional fades |
| `apply_effects` | gain, normalize, fades, high/low/band-pass, notch, EQ, bass/treble, echo, compressor, limiter, noise gate, reverse, speed, pan, trim silence… |
| `mix_audio` / `concat_audio` | Mix with gains/offsets, or join with crossfade/gap |
| `get_audio_info` | Format, duration, peak and RMS |
| `open_in_audacity` | Open results in the Audacity editor |
| `install_audacity_extension` | Adds *Split into Files*, *Export Selection*, *Import Audio File as Track* (Tools › Extension) and *Labels Every N Seconds* (Analyze › Extension) inside Audacity 4 |
| `get_audacity_status` | Installation, engine, formats, effects |

## Install
1. Download `audacity-mcp-server.mcpb` from the [latest release](https://github.com/jhonsu01/audacity-mcp-server/releases/latest) and open it with Claude Desktop.
2. Optional: ask Claude to run `install_audacity_extension`, then restart Audacity 4.

Requirements: Windows 10/11 x64, Audacity 4 (tested 4.0.1), Node ≥ 20 (bundled with Claude Desktop). FFmpeg optional.

## Build
```bash
npm install
npm run build:all
npm run test:e2e
npm run pack:mcpb
```

## License
GPL-3.0-only (the extension ABI header comes from Audacity, GPL-3.0). Audacity is a trademark of Muse Group; this project is not affiliated with Audacity or Muse Group.
