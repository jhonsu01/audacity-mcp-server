<p align="center"><img src="icon.png" width="120" alt="Icono de Audacity Bridge"></p>

<h1 align="center">Audacity MCP Server</h1>

<p align="center">
  <a href="README.md">English</a> · <b>Español</b> · <a href="README.pt-BR.md">Português</a> · <a href="README.fr.md">Français</a> · <a href="README.de.md">Deutsch</a> · <a href="README.ru.md">Русский</a> · <a href="README.zh-CN.md">简体中文</a> · <a href="README.ja.md">日本語</a> · <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  Deja que Claude edite audio con el <b>Audacity 4</b> instalado en tu equipo con Windows, macOS o Linux:<br>
  cortar, recortar, convertir, remuestrear, aplicar efectos, mezclar y unir, además de una <b>extensión dentro de Audacity</b>.
</p>

---

## Por qué

Audacity 4 es un gran editor de audio de código abierto, pero **no tiene interfaz de scripting** (ni `mod-script-pipe` ni exportación por línea de comandos). Este servidor [Model Context Protocol](https://modelcontextprotocol.io) le da a Claude acceso al propio motor de audio de Audacity, para que puedas pedir cosas como:

> *"Corta cada grabación de `D:\voz` en trozos de 15 segundos, una carpeta por archivo."*
> *"Convierte `D:\podcast\ep1.wav` a FLAC de 24 bits a 48 kHz."*
> *"Aplica un paso alto a 80 Hz, un compresor y normaliza a -1 dBFS en `D:\toma3.wav`."*

Inspirado en [VectorMagic-mcp-server](https://github.com/jhonsu01/VectorMagic-mcp-server).

## Cómo funciona

| Parte | Qué es |
| --- | --- |
| **Motor nativo** (`aumcp-engine.exe`, C++) | Carga en tiempo de ejecución las librerías de códecs del propio Audacity 4 (**libsndfile 1.2.2**, **mpg123**) desde su carpeta de instalación, así lee y escribe exactamente lo que tu Audacity soporta. Procesa archivos largos por streaming; remuestreo windowed-sinc de alta calidad. |
| **Servidor MCP** (Node.js) | 10 herramientas para Claude. FFmpeg opcional para exportar MP3/M4A/AAC/WMA e importar formatos poco comunes. |
| **Extensión de Audacity** ("MCP Audio Tools") | Extensión oficial de Audacity 4 (JavaScript + DLL nativa) que añade efectos a los menús de Audacity. Se instala con una sola llamada. |

## Herramientas

| Herramienta | Qué hace |
| --- | --- |
| `split_audio` | Corta uno o varios archivos en trozos de N segundos (15 s por defecto), una carpeta por entrada, numerados `<nombre>_001.wav`… |
| `convert_audio` | Cambia formato, profundidad de bits, frecuencia de muestreo o canales de uno o varios archivos |
| `trim_audio` | Conserva un rango de tiempo, con fundido de entrada/salida opcional |
| `apply_effects` | Cadena de efectos (ver abajo) |
| `mix_audio` | Mezcla archivos superpuestos con ganancia y desplazamiento por archivo |
| `concat_audio` | Une archivos con crossfade de potencia constante o un silencio entre ellos |
| `get_audio_info` | Formato, frecuencia, canales, duración, nivel de pico y RMS |
| `open_in_audacity` | Abre los resultados en el editor de Audacity 4 |
| `install_audacity_extension` | Instala / actualiza / elimina la extensión dentro de Audacity |
| `get_audacity_status` | Ruta y versión de Audacity, motor, FFmpeg, formatos, efectos |

## Formatos

| | |
| --- | --- |
| **Entrada (nativa)** | wav, aiff, flac, ogg, opus, mp3, mp2, caf, w64, rf64, au, voc, sd2, iff, paf… |
| **Entrada (vía FFmpeg)** | m4a, aac, mp4, wma, wv, ac3, webm, mkv, ape… |
| **Salida (nativa)** | wav, flac, ogg (Vorbis), opus, aiff, caf, w64, rf64, au |
| **Salida (vía FFmpeg)** | mp3, m4a, aac, wma |
| **Opciones** | profundidad `pcm8/16/24/32`, `float32/64` · cualquier frecuencia · mono/estéreo · calidad Vorbis/Opus · compresión FLAC · bitrate |

## Efectos

`gain` · `normalize` · `fade_in` · `fade_out` (lineal, exponencial, logarítmica, curva S) · `trim` · `pad` · `trim_silence` · `reverse` · `speed` · `invert` · `remove_dc` · `highpass` · `lowpass` · `bandpass` · `notch` · `eq` · `bass` · `treble` · `echo` · `compressor` · `limiter` · `noise_gate` · `mono` · `stereo` · `swap_channels` · `pan` · **`mouth_declick`** · **`declip`** · **`noise_reduction`** · **`click_removal`**

Ejemplo: `[{"type":"highpass","frequency":80},{"type":"compressor","threshold_db":-20,"ratio":3},{"type":"normalize","peak_db":-1}]`

Voice cleanup / limpieza de voz: `[{"type":"declip"},{"type":"mouth_declick","sensitivity":6},{"type":"limiter","ceiling_db":-1}]`

## Dentro de Audacity (extensión)

Tras `install_audacity_extension` y reiniciar Audacity 4:

| Menú | Efecto |
| --- | --- |
| Herramientas › Extensión › audacity-mcp-server | **Split into Files (MCP)**: guarda la selección (o cada pista seleccionada) en archivos de N segundos |
| | **Export Selection to File (MCP)**: WAV, FLAC, OGG, Opus, AIFF |
| | **Import Audio File as Track (MCP)**: cualquier archivo compatible (incluido MP3) como pista nueva |
| Analizar › Extensión › audacity-mcp-server | **Labels Every N Seconds (MCP)**: una pista de etiquetas con una región por intervalo |
| Herramientas | **MCP Audio Tools: status**: comprueba la librería nativa |

Todos admiten deshacer y muestran barra de progreso.

## Plataformas

| | Windows | macOS | Linux |
| --- | --- | --- | --- |
| **Motor nativo** | x64 | universal (Apple Silicon + Intel) | x86_64 |
| **Códecs de audio** | los del propio Audacity (`sndfile.dll`, `mpg123.dll`) | los de Audacity.app, o `libsndfile` + `mpg123` de Homebrew | `libsndfile` + `libmpg123` del sistema (la AppImage guarda los suyos dentro de la imagen) |
| **Carpeta de extensiones** | `%LOCALAPPDATA%\Audacity\Audacity4\extensions` | `~/Library/Application Support/Audacity/Audacity4/extensions` | `~/.local/share/Audacity/Audacity4/extensions` · Flatpak: `~/.var/app/org.audacityteam.Audacity/data/Audacity/Audacity4/extensions` |
| **Abrir en Audacity** | `Audacity4.exe` | `open -a Audacity` | AppImage · `audacity` · `flatpak run` |

Linux: `sudo apt install libsndfile1 libmpg123-0` (Debian/Ubuntu) · `sudo dnf install libsndfile mpg123-libs` (Fedora) · `sudo pacman -S libsndfile mpg123` (Arch). macOS, si las librerías de Audacity no se pueden cargar por separado: `brew install libsndfile mpg123`. `get_audacity_status` muestra qué librerías se están usando.

## Requisitos

- Windows 10/11 x64, macOS 11+ (Apple Silicon o Intel) o Linux x86_64 (glibc 2.35+, p. ej. Ubuntu 22.04+)
- **Audacity 4** (probado con **4.0.1**)
- Node.js ≥ 20 (incluido en Claude Desktop para instalaciones `.mcpb`)
- Opcional: FFmpeg (solo para exportar MP3/M4A/AAC/WMA o importar M4A/AAC/WMA/WavPack)

## Instalación

### Claude Desktop (recomendado)

1. Descarga `audacity-mcp-server.mcpb` del [último release](https://github.com/jhonsu01/audacity-mcp-server/releases/latest).
2. Haz doble clic (o arrástralo a Claude Desktop → *Configuración → Extensiones*) y pulsa **Instalar**.
3. Opcional: si Audacity no está en `C:\Program Files\Audacity 4`, indica su carpeta en la configuración de la extensión; indica la ruta de FFmpeg si no está en el `PATH`.
4. Opcional: pídele a Claude *"instala la extensión de Audacity"* y reinicia Audacity.

### Claude Code / otros clientes MCP

```bash
git clone https://github.com/jhonsu01/audacity-mcp-server.git
cd audacity-mcp-server
npm install
npm run build:all
claude mcp add audacity -- node "$PWD/dist/bundle.cjs"        # macOS / Linux
claude mcp add audacity -- node "%CD%\dist\bundle.cjs"      # Windows (cmd)
```

`build:all` compila las partes nativas y necesita Visual Studio con la carga de trabajo de C++.

## Configuración

| Variable | Significado |
| --- | --- |
| `AUDACITY_DIR` | Carpeta de instalación de Audacity 4 (o `Audacity4.exe`). Por defecto: `C:\Program Files\Audacity 4` |
| `FFMPEG_PATH` | `ffmpeg.exe` o su carpeta. Por defecto: desde el `PATH` |
| `AUDACITY_MCP_TIMEOUT` | Segundos máximos por trabajo (1800 por defecto) |

## Seguridad

- Los archivos **nunca se sobrescriben** salvo con `overwrite: true`.
- Todas las rutas deben ser absolutas; la entrada nunca se modifica.
- Todo se ejecuta en local, sin acceso a la red.

## Desarrollo

```bash
npm run build:native   # aumcp-engine.exe + audacity_mcp_native.dll (MSVC)
npm run build          # TypeScript + bundle
npm run test:e2e       # 25 comprobaciones end-to-end por stdio MCP real (audio sintético)
npm run pack:mcpb      # audacity-mcp-server.mcpb
```

## Licencia

GPL-3.0-only. La cabecera ABI de la extensión proviene de Audacity (GPL-3.0).
Audacity® es una marca registrada de Muse Group. Este proyecto no está afiliado ni respaldado por Audacity ni Muse Group.
