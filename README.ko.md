<p align="center"><img src="icon.png" width="120" alt="Audacity Bridge 아이콘"></p>

<h1 align="center">Audacity MCP Server</h1>

<p align="center">
  <a href="README.md">English</a> · <a href="README.es.md">Español</a> · <a href="README.pt-BR.md">Português</a> · <a href="README.fr.md">Français</a> · <a href="README.de.md">Deutsch</a> · <a href="README.ru.md">Русский</a> · <a href="README.zh-CN.md">简体中文</a> · <a href="README.ja.md">日本語</a> · <b>한국어</b>
</p>

<p align="center">
  Windows, macOS, Linux에 설치된 <b>Audacity 4</b>로 Claude가 오디오를 편집하게 하세요 —<br>
  분할, 자르기, 변환, 리샘플링, 효과 적용, 믹스, 이어붙이기, 그리고 <b>Audacity 내장 확장 기능</b>까지.
</p>

---

## 왜 필요한가

Audacity 4는 훌륭한 오픈 소스 오디오 편집기지만 **스크립트 인터페이스가 없습니다**(`mod-script-pipe`도, 명령줄 내보내기도 없음). 이 [Model Context Protocol](https://modelcontextprotocol.io) 서버는 Claude가 Audacity 자체의 오디오 엔진을 사용할 수 있게 해 주므로 다음과 같이 요청할 수 있습니다:

> *"`D:\voice`의 모든 녹음을 15초 조각으로 나누고, 파일마다 폴더를 하나씩 만들어 줘."*
> *"`D:\podcast\ep1.wav`를 48 kHz, 24비트 FLAC으로 변환해 줘."*
> *"`D:\take3.wav`에 80 Hz 하이패스와 컴프레서를 적용하고 -1 dBFS로 노멀라이즈해 줘."*

[VectorMagic-mcp-server](https://github.com/jhonsu01/VectorMagic-mcp-server)에서 영감을 받았습니다.

## 작동 방식

| 구성 요소 | 설명 |
| --- | --- |
| **네이티브 엔진** (`aumcp-engine.exe`, C++) | 실행 시 설치 폴더에서 Audacity 4 자체의 코덱 라이브러리(**libsndfile 1.2.2**, **mpg123**)를 불러오므로, 사용 중인 Audacity가 지원하는 형식을 그대로 읽고 씁니다. 긴 파일은 스트리밍 처리, 고품질 windowed-sinc 리샘플링. |
| **MCP 서버** (Node.js) | Claude용 도구 10개. MP3/M4A/AAC/WMA 내보내기와 드문 형식 가져오기에는 FFmpeg(선택). |
| **Audacity 확장 기능** ("MCP Audio Tools") | Audacity 4 공식 확장 기능(JavaScript + 네이티브 DLL)으로 Audacity 메뉴에 효과를 추가합니다. 호출 한 번으로 설치. |

## 도구

| 도구 | 기능 |
| --- | --- |
| `split_audio` | 하나 이상의 파일을 N초(기본 15초) 조각으로 분할, 입력마다 폴더, `<이름>_001.wav`… 번호 부여 |
| `convert_audio` | 형식, 비트 깊이, 샘플레이트, 채널 수 변경 |
| `trim_audio` | 지정한 시간 구간만 남김(페이드 인/아웃 선택) |
| `apply_effects` | 효과 체인(아래 참조) |
| `mix_audio` | 파일별 게인과 시작 오프셋으로 겹쳐서 믹스 |
| `concat_audio` | 등전력 크로스페이드 또는 무음 간격으로 이어붙이기 |
| `get_audio_info` | 형식, 레이트, 채널, 길이, 피크 및 RMS 레벨 |
| `open_in_audacity` | 결과를 Audacity 4 편집기에서 열기 |
| `install_audacity_extension` | 내장 확장 기능 설치 / 업데이트 / 제거 |
| `get_audacity_status` | Audacity 경로와 버전, 엔진, FFmpeg, 형식, 효과 |

## 형식

| | |
| --- | --- |
| **입력(네이티브)** | wav, aiff, flac, ogg, opus, mp3, mp2, caf, w64, rf64, au, voc, sd2, iff, paf… |
| **입력(FFmpeg 경유)** | m4a, aac, mp4, wma, wv, ac3, webm, mkv, ape… |
| **출력(네이티브)** | wav, flac, ogg (Vorbis), opus, aiff, caf, w64, rf64, au |
| **출력(FFmpeg 경유)** | mp3, m4a, aac, wma |
| **옵션** | 비트 깊이 `pcm8/16/24/32`, `float32/64` · 임의 샘플레이트 · 모노/스테레오 · Vorbis/Opus 품질 · FLAC 압축 · 비트레이트 |

## 효과

`gain` · `normalize` · `fade_in` · `fade_out`(선형, 지수, 로그, S 곡선) · `trim` · `pad` · `trim_silence` · `reverse` · `speed` · `invert` · `remove_dc` · `highpass` · `lowpass` · `bandpass` · `notch` · `eq` · `bass` · `treble` · `echo` · `compressor` · `limiter` · `noise_gate` · `mono` · `stereo` · `swap_channels` · `pan` · **`mouth_declick`** · **`declip`** · **`noise_reduction`** · **`click_removal`**

예시: `[{"type":"highpass","frequency":80},{"type":"compressor","threshold_db":-20,"ratio":3},{"type":"normalize","peak_db":-1}]`

Voice cleanup / limpieza de voz: `[{"type":"declip"},{"type":"mouth_declick","sensitivity":6},{"type":"limiter","ceiling_db":-1}]`

## Audacity 안에서 (확장 기능)

`install_audacity_extension` 실행 후 Audacity 4를 다시 시작하면:

| 메뉴 | 효과 |
| --- | --- |
| 도구 › 확장 › audacity-mcp-server | **Split into Files (MCP)**: 선택 영역(또는 선택한 각 트랙)을 N초 파일로 저장 |
| | **Export Selection to File (MCP)**: WAV, FLAC, OGG, Opus, AIFF |
| | **Import Audio File as Track (MCP)**: 지원되는 모든 파일(MP3 포함)을 새 트랙으로 추가 |
| 분석 › 확장 › audacity-mcp-server | **Labels Every N Seconds (MCP)**: 간격마다 영역 하나씩 있는 레이블 트랙 |
| 도구 | **MCP Audio Tools: status**: 네이티브 라이브러리 확인 |

모두 실행 취소를 지원하고 진행률 표시줄을 보여 줍니다.

## 플랫폼

| | Windows | macOS | Linux |
| --- | --- | --- | --- |
| **네이티브 엔진** | x64 | 유니버설(Apple Silicon + Intel) | x86_64 |
| **오디오 코덱** | Audacity 자체 라이브러리(`sndfile.dll`, `mpg123.dll`) | Audacity.app 내장 또는 Homebrew의 `libsndfile` + `mpg123` | 시스템 `libsndfile` + `libmpg123`(AppImage는 이미지 안에 자체 라이브러리 보관) |
| **확장 기능 폴더** | `%LOCALAPPDATA%\Audacity\Audacity4\extensions` | `~/Library/Application Support/Audacity/Audacity4/extensions` | `~/.local/share/Audacity/Audacity4/extensions` · Flatpak: `~/.var/app/org.audacityteam.Audacity/data/Audacity/Audacity4/extensions` |
| **Audacity에서 열기** | `Audacity4.exe` | `open -a Audacity` | AppImage · `audacity` · `flatpak run` |

Linux: `sudo apt install libsndfile1 libmpg123-0`(Debian/Ubuntu) · `sudo dnf install libsndfile mpg123-libs`(Fedora) · `sudo pacman -S libsndfile mpg123`(Arch). macOS에서 Audacity 내장 라이브러리를 단독으로 불러올 수 없으면: `brew install libsndfile mpg123`. 사용 중인 라이브러리는 `get_audacity_status`에서 확인할 수 있습니다.

## 요구 사항

- Windows 10/11 x64, macOS 11+(Apple Silicon 또는 Intel) 또는 Linux x86_64(glibc 2.35+, 예: Ubuntu 22.04+)
- **Audacity 4**(**4.0.1**에서 테스트)
- Node.js ≥ 20(`.mcpb` 설치 시 Claude Desktop에 포함)
- 선택: FFmpeg(MP3/M4A/AAC/WMA 내보내기 또는 M4A/AAC/WMA/WavPack 가져오기에만 필요)

## 설치

### Claude Desktop (권장)

1. [최신 릴리스](https://github.com/jhonsu01/audacity-mcp-server/releases/latest)에서 `audacity-mcp-server.mcpb`를 다운로드합니다.
2. 더블클릭(또는 Claude Desktop → *설정 → 확장 프로그램*으로 끌어다 놓기)한 뒤 **설치**를 클릭합니다.
3. 선택: Audacity가 `C:\Program Files\Audacity 4`에 없다면 확장 설정에서 폴더를 지정하고, FFmpeg가 `PATH`에 없다면 경로를 지정합니다.
4. 선택: Claude에게 *"Audacity 확장 기능을 설치해 줘"*라고 요청한 뒤 Audacity를 다시 시작합니다.

### Claude Code / 기타 MCP 클라이언트

```bash
git clone https://github.com/jhonsu01/audacity-mcp-server.git
cd audacity-mcp-server
npm install
npm run build:all
claude mcp add audacity -- node "$PWD/dist/bundle.cjs"        # macOS / Linux
claude mcp add audacity -- node "%CD%\dist\bundle.cjs"      # Windows (cmd)
```

`build:all`은 네이티브 부분을 빌드하므로 C++ 워크로드가 포함된 Visual Studio가 필요합니다.

## 설정

| 변수 | 의미 |
| --- | --- |
| `AUDACITY_DIR` | Audacity 4 설치 폴더(또는 `Audacity4.exe`). 기본값: `C:\Program Files\Audacity 4` |
| `FFMPEG_PATH` | `ffmpeg.exe` 또는 그 폴더. 기본값: `PATH`에서 검색 |
| `AUDACITY_MCP_TIMEOUT` | 작업당 최대 초(기본 1800) |

## 안전

- `overwrite: true`가 아니면 파일을 **절대 덮어쓰지 않습니다**.
- 모든 경로는 절대 경로여야 하며, 입력 파일은 수정되지 않습니다.
- 모든 작업은 네트워크 접근 없이 로컬에서 실행됩니다.

## 개발

```bash
npm run build:native   # aumcp-engine.exe + audacity_mcp_native.dll (MSVC)
npm run build          # TypeScript + 번들
npm run test:e2e       # 실제 MCP stdio로 25개 엔드투엔드 검사(합성 오디오)
npm run pack:mcpb      # audacity-mcp-server.mcpb
```

## 라이선스

GPL-3.0-only. 확장 기능의 ABI 헤더는 Audacity(GPL-3.0)에서 가져왔습니다.
Audacity®는 Muse Group의 상표입니다. 이 프로젝트는 Audacity 또는 Muse Group과 관련이 없으며 승인을 받지 않았습니다.
