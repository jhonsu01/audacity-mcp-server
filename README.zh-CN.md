<p align="center"><img src="icon.png" width="120" alt="Audacity Bridge 图标"></p>

<h1 align="center">Audacity MCP Server</h1>

<p align="center">
  <a href="README.md">English</a> · <a href="README.es.md">Español</a> · <a href="README.pt-BR.md">Português</a> · <a href="README.fr.md">Français</a> · <a href="README.de.md">Deutsch</a> · <a href="README.ru.md">Русский</a> · <b>简体中文</b> · <a href="README.ja.md">日本語</a> · <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  让 Claude 使用你 Windows 电脑上安装的 <b>Audacity 4</b> 编辑音频——<br>
  分割、裁剪、转换、重采样、添加效果、混音和拼接，另附一个<b>Audacity 内置扩展</b>。
</p>

---

## 为什么

Audacity 4 是一款优秀的开源音频编辑器，但它**没有脚本接口**（没有 `mod-script-pipe`，也不能通过命令行导出）。这个 [Model Context Protocol](https://modelcontextprotocol.io) 服务器让 Claude 直接使用 Audacity 自带的音频引擎，你可以这样提问：

> *“把 `D:\voice` 里的每段录音切成 15 秒的片段，每个文件一个文件夹。”*
> *“把 `D:\podcast\ep1.wav` 转成 48 kHz、24 位的 FLAC。”*
> *“对 `D:\take3.wav` 加 80 Hz 高通、压缩器，并标准化到 -1 dBFS。”*

灵感来自 [VectorMagic-mcp-server](https://github.com/jhonsu01/VectorMagic-mcp-server)。

## 工作原理

| 组件 | 说明 |
| --- | --- |
| **原生引擎**（`aumcp-engine.exe`，C++） | 运行时从安装目录加载 Audacity 4 自带的编解码库（**libsndfile 1.2.2**、**mpg123**），因此读写能力与你的 Audacity 完全一致。长文件流式处理；高质量 windowed-sinc 重采样。 |
| **MCP 服务器**（Node.js） | 为 Claude 提供 10 个工具。可选 FFmpeg，用于导出 MP3/M4A/AAC/WMA 及导入少见格式。 |
| **Audacity 扩展**（“MCP Audio Tools”） | 官方 Audacity 4 扩展（JavaScript + 原生 DLL），在 Audacity 自己的菜单中添加效果。一次调用即可安装。 |

## 工具

| 工具 | 功能 |
| --- | --- |
| `split_audio` | 将一个或多个文件切成 N 秒的片段（默认 15 秒），每个输入一个文件夹，编号为 `<名称>_001.wav`… |
| `convert_audio` | 修改格式、位深、采样率或声道数 |
| `trim_audio` | 保留一段时间范围，可选淡入/淡出 |
| `apply_effects` | 效果链（见下文） |
| `mix_audio` | 叠加混音，每个文件可设增益和起始偏移 |
| `concat_audio` | 依次拼接文件，可用等功率交叉淡化或静音间隔 |
| `get_audio_info` | 格式、采样率、声道、时长、峰值和 RMS 电平 |
| `open_in_audacity` | 在 Audacity 4 编辑器中打开结果 |
| `install_audacity_extension` | 安装 / 更新 / 卸载内置扩展 |
| `get_audacity_status` | Audacity 路径与版本、引擎、FFmpeg、格式、效果 |

## 格式

| | |
| --- | --- |
| **输入（原生）** | wav、aiff、flac、ogg、opus、mp3、mp2、caf、w64、rf64、au、voc、sd2、iff、paf… |
| **输入（经 FFmpeg）** | m4a、aac、mp4、wma、wv、ac3、webm、mkv、ape… |
| **输出（原生）** | wav、flac、ogg（Vorbis）、opus、aiff、caf、w64、rf64、au |
| **输出（经 FFmpeg）** | mp3、m4a、aac、wma |
| **选项** | 位深 `pcm8/16/24/32`、`float32/64` · 任意采样率 · 单声道/立体声 · Vorbis/Opus 质量 · FLAC 压缩 · 码率 |

## 效果

`gain` · `normalize` · `fade_in` · `fade_out`（线性、指数、对数、S 曲线） · `trim` · `pad` · `trim_silence` · `reverse` · `speed` · `invert` · `remove_dc` · `highpass` · `lowpass` · `bandpass` · `notch` · `eq` · `bass` · `treble` · `echo` · `compressor` · `limiter` · `noise_gate` · `mono` · `stereo` · `swap_channels` · `pan`

示例：`[{"type":"highpass","frequency":80},{"type":"compressor","threshold_db":-20,"ratio":3},{"type":"normalize","peak_db":-1}]`

## 在 Audacity 内（扩展）

执行 `install_audacity_extension` 并重启 Audacity 4 后：

| 菜单 | 效果 |
| --- | --- |
| 工具 › 扩展 › audacity-mcp-server | **Split into Files (MCP)**：将选区（或每条选中的音轨）写成 N 秒的文件 |
| | **Export Selection to File (MCP)**：WAV、FLAC、OGG、Opus、AIFF |
| | **Import Audio File as Track (MCP)**：将任意支持的文件（包括 MP3）导入为新音轨 |
| 分析 › 扩展 › audacity-mcp-server | **Labels Every N Seconds (MCP)**：每个间隔一个区域的标签轨 |
| 工具 | **MCP Audio Tools: status**：检查原生库 |

全部支持撤销并显示进度条。

## 系统要求

- Windows 10/11 x64
- **Audacity 4**（已在 **4.0.1** 上测试）
- Node.js ≥ 20（通过 `.mcpb` 安装时由 Claude Desktop 自带）
- 可选：FFmpeg（仅用于导出 MP3/M4A/AAC/WMA 或导入 M4A/AAC/WMA/WavPack）

## 安装

### Claude Desktop（推荐）

1. 从[最新版本](https://github.com/jhonsu01/audacity-mcp-server/releases/latest)下载 `audacity-mcp-server.mcpb`。
2. 双击它（或拖到 Claude Desktop → *设置 → 扩展*），然后点击**安装**。
3. 可选：如果 Audacity 不在 `C:\Program Files\Audacity 4`，请在扩展设置中指定其文件夹；如果 FFmpeg 不在 `PATH` 中，请指定其路径。
4. 可选：让 Claude *“安装 Audacity 扩展”*，然后重启 Audacity。

### Claude Code / 其他 MCP 客户端

```bash
git clone https://github.com/jhonsu01/audacity-mcp-server.git
cd audacity-mcp-server
npm install
npm run build:all
claude mcp add audacity -- node "%CD%\dist\bundle.cjs"
```

`build:all` 会编译原生部分，需要安装带 C++ 工作负载的 Visual Studio。

## 配置

| 变量 | 含义 |
| --- | --- |
| `AUDACITY_DIR` | Audacity 4 安装文件夹（或 `Audacity4.exe`）。默认：`C:\Program Files\Audacity 4` |
| `FFMPEG_PATH` | `ffmpeg.exe` 或其文件夹。默认：从 `PATH` 查找 |
| `AUDACITY_MCP_TIMEOUT` | 每个任务的最长秒数（默认 1800） |

## 安全

- 除非设置 `overwrite: true`，否则**绝不覆盖**文件。
- 所有路径必须是绝对路径；输入文件不会被修改。
- 全部在本地运行，无需联网。

## 开发

```bash
npm run build:native   # aumcp-engine.exe + audacity_mcp_native.dll (MSVC)
npm run build          # TypeScript + 打包
npm run test:e2e       # 通过真实 MCP stdio 进行 25 项端到端检查（合成音频）
npm run pack:mcpb      # audacity-mcp-server.mcpb
```

## 许可证

GPL-3.0-only。扩展的 ABI 头文件来自 Audacity（GPL-3.0）。
Audacity® 是 Muse Group 的商标。本项目与 Audacity 或 Muse Group 无关，也未获其认可。
