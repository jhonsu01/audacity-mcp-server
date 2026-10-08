<p align="center"><img src="icon.png" width="120" alt="Значок Audacity Bridge"></p>

<h1 align="center">Audacity MCP Server</h1>

<p align="center">
  <a href="README.md">English</a> · <a href="README.es.md">Español</a> · <a href="README.pt-BR.md">Português</a> · <a href="README.fr.md">Français</a> · <a href="README.de.md">Deutsch</a> · <b>Русский</b> · <a href="README.zh-CN.md">简体中文</a> · <a href="README.ja.md">日本語</a> · <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  Позвольте Claude редактировать звук с помощью <b>Audacity 4</b>, установленного на вашем компьютере с Windows, macOS или Linux:<br>
  нарезка, обрезка, конвертация, передискретизация, эффекты, микширование и склейка, а также <b>расширение внутри Audacity</b>.
</p>

---

## Зачем

Audacity 4 — отличный аудиоредактор с открытым исходным кодом, но у него **нет интерфейса для скриптов** (нет `mod-script-pipe`, нет экспорта из командной строки). Этот сервер [Model Context Protocol](https://modelcontextprotocol.io) даёт Claude доступ к собственному аудиодвижку Audacity, поэтому можно просить, например:

> *«Разрежь каждую запись в `D:\voice` на куски по 15 секунд, по папке на файл».*
> *«Конвертируй `D:\podcast\ep1.wav` в FLAC 24 бит, 48 кГц».*
> *«Примени к `D:\take3.wav` фильтр высоких частот 80 Гц, компрессор и нормализацию до -1 dBFS».*

Вдохновлено проектом [VectorMagic-mcp-server](https://github.com/jhonsu01/VectorMagic-mcp-server).

## Как это работает

| Часть | Описание |
| --- | --- |
| **Нативный движок** (`aumcp-engine.exe`, C++) | Во время работы загружает собственные библиотеки кодеков Audacity 4 (**libsndfile 1.2.2**, **mpg123**) из папки установки, поэтому читает и записывает ровно то, что поддерживает ваш Audacity. Обрабатывает длинные файлы потоково; качественная передискретизация windowed-sinc. |
| **MCP-сервер** (Node.js) | 10 инструментов для Claude. FFmpeg (необязательно) — для экспорта MP3/M4A/AAC/WMA и редких форматов импорта. |
| **Расширение Audacity** («MCP Audio Tools») | Официальное расширение Audacity 4 (JavaScript + нативная DLL), добавляющее эффекты в меню Audacity. Устанавливается одним вызовом. |

## Инструменты

| Инструмент | Что делает |
| --- | --- |
| `split_audio` | Режет один или несколько файлов на куски по N секунд (по умолчанию 15 с), по папке на входной файл, имена `<имя>_001.wav`… |
| `convert_audio` | Меняет формат, разрядность, частоту дискретизации или число каналов |
| `trim_audio` | Оставляет отрезок времени, с необязательным нарастанием/затуханием |
| `apply_effects` | Цепочка эффектов (см. ниже) |
| `mix_audio` | Микширует файлы с уровнем и сдвигом начала для каждого |
| `concat_audio` | Склеивает файлы с кроссфейдом равной мощности или паузой |
| `get_audio_info` | Формат, частота, каналы, длительность, пиковый и RMS-уровень |
| `open_in_audacity` | Открывает результаты в редакторе Audacity 4 |
| `install_audacity_extension` | Устанавливает / обновляет / удаляет расширение |
| `get_audacity_status` | Путь и версия Audacity, движок, FFmpeg, форматы, эффекты |

## Форматы

| | |
| --- | --- |
| **Ввод (нативно)** | wav, aiff, flac, ogg, opus, mp3, mp2, caf, w64, rf64, au, voc, sd2, iff, paf… |
| **Ввод (через FFmpeg)** | m4a, aac, mp4, wma, wv, ac3, webm, mkv, ape… |
| **Вывод (нативно)** | wav, flac, ogg (Vorbis), opus, aiff, caf, w64, rf64, au |
| **Вывод (через FFmpeg)** | mp3, m4a, aac, wma |
| **Параметры** | разрядность `pcm8/16/24/32`, `float32/64` · любая частота · моно/стерео · качество Vorbis/Opus · сжатие FLAC · битрейт |

## Эффекты

`gain` · `normalize` · `fade_in` · `fade_out` (линейная, экспоненциальная, логарифмическая, S-кривая) · `trim` · `pad` · `trim_silence` · `reverse` · `speed` · `invert` · `remove_dc` · `highpass` · `lowpass` · `bandpass` · `notch` · `eq` · `bass` · `treble` · `echo` · `compressor` · `limiter` · `noise_gate` · `mono` · `stereo` · `swap_channels` · `pan` · **`mouth_declick`** · **`declip`** · **`noise_reduction`** · **`click_removal`**

Пример: `[{"type":"highpass","frequency":80},{"type":"compressor","threshold_db":-20,"ratio":3},{"type":"normalize","peak_db":-1}]`

Voice cleanup / limpieza de voz: `[{"type":"declip"},{"type":"mouth_declick","sensitivity":6},{"type":"limiter","ceiling_db":-1}]`

## Внутри Audacity (расширение)

После `install_audacity_extension` и перезапуска Audacity 4:

| Меню | Эффект |
| --- | --- |
| Инструменты › Расширение › audacity-mcp-server | **Split into Files (MCP)**: сохраняет выделение (или каждую выбранную дорожку) в файлы по N секунд |
| | **Export Selection to File (MCP)**: WAV, FLAC, OGG, Opus, AIFF |
| | **Import Audio File as Track (MCP)**: любой поддерживаемый файл (включая MP3) как новая дорожка |
| Анализ › Расширение › audacity-mcp-server | **Labels Every N Seconds (MCP)**: дорожка меток с одной областью на интервал |
| Инструменты | **MCP Audio Tools: status**: проверяет нативную библиотеку |

Все поддерживают отмену и показывают индикатор выполнения.

## Платформы

| | Windows | macOS | Linux |
| --- | --- | --- | --- |
| **Нативный движок** | x64 | universal (Apple Silicon + Intel) | x86_64 |
| **Аудиокодеки** | собственные библиотеки Audacity (`sndfile.dll`, `mpg123.dll`) | из Audacity.app или `libsndfile` + `mpg123` из Homebrew | системные `libsndfile` + `libmpg123` (AppImage хранит свои внутри образа) |
| **Папка расширений** | `%LOCALAPPDATA%\Audacity\Audacity4\extensions` | `~/Library/Application Support/Audacity/Audacity4/extensions` | `~/.local/share/Audacity/Audacity4/extensions` · Flatpak: `~/.var/app/org.audacityteam.Audacity/data/Audacity/Audacity4/extensions` |
| **Открыть в Audacity** | `Audacity4.exe` | `open -a Audacity` | AppImage · `audacity` · `flatpak run` |

Linux: `sudo apt install libsndfile1 libmpg123-0` (Debian/Ubuntu) · `sudo dnf install libsndfile mpg123-libs` (Fedora) · `sudo pacman -S libsndfile mpg123` (Arch). macOS, если библиотеки Audacity нельзя загрузить отдельно: `brew install libsndfile mpg123`. `get_audacity_status` показывает, какие библиотеки используются.

## Требования

- Windows 10/11 x64, macOS 11+ (Apple Silicon или Intel) или Linux x86_64 (glibc 2.35+, например Ubuntu 22.04+)
- **Audacity 4** (проверено на **4.0.1**)
- Node.js ≥ 20 (входит в Claude Desktop при установке `.mcpb`)
- Необязательно: FFmpeg (только для экспорта MP3/M4A/AAC/WMA или импорта M4A/AAC/WMA/WavPack)

## Установка

### Claude Desktop (рекомендуется)

1. Скачайте `audacity-mcp-server.mcpb` из [последнего релиза](https://github.com/jhonsu01/audacity-mcp-server/releases/latest).
2. Дважды щёлкните по файлу (или перетащите его в Claude Desktop → *Настройки → Расширения*) и нажмите **Установить**.
3. Необязательно: если Audacity установлен не в `C:\Program Files\Audacity 4`, укажите папку в настройках расширения; укажите путь к FFmpeg, если его нет в `PATH`.
4. Необязательно: попросите Claude *«установи расширение Audacity»* и перезапустите Audacity.

### Claude Code / другие MCP-клиенты

```bash
git clone https://github.com/jhonsu01/audacity-mcp-server.git
cd audacity-mcp-server
npm install
npm run build:all
claude mcp add audacity -- node "$PWD/dist/bundle.cjs"        # macOS / Linux
claude mcp add audacity -- node "%CD%\dist\bundle.cjs"      # Windows (cmd)
```

`build:all` собирает нативные части и требует Visual Studio с нагрузкой C++.

## Настройка

| Переменная | Значение |
| --- | --- |
| `AUDACITY_DIR` | Папка установки Audacity 4 (или `Audacity4.exe`). По умолчанию: `C:\Program Files\Audacity 4` |
| `FFMPEG_PATH` | `ffmpeg.exe` или его папка. По умолчанию: из `PATH` |
| `AUDACITY_MCP_TIMEOUT` | Максимум секунд на задачу (по умолчанию 1800) |

## Безопасность

- Файлы **никогда не перезаписываются** без `overwrite: true`.
- Все пути должны быть абсолютными; исходный файл не изменяется.
- Всё работает локально, без доступа к сети.

## Разработка

```bash
npm run build:native   # aumcp-engine.exe + audacity_mcp_native.dll (MSVC)
npm run build          # TypeScript + бандл
npm run test:e2e       # 25 сквозных проверок через настоящий MCP stdio (синтетический звук)
npm run pack:mcpb      # audacity-mcp-server.mcpb
```

## Лицензия

GPL-3.0-only. Заголовок ABI расширения взят из Audacity (GPL-3.0).
Audacity® — товарный знак Muse Group. Проект не связан с Audacity или Muse Group и не одобрен ими.
