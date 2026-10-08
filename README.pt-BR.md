<p align="center"><img src="icon.png" width="120" alt="Ícone do Audacity Bridge"></p>

<h1 align="center">Audacity MCP Server</h1>

<p align="center">
  <a href="README.md">English</a> · <a href="README.es.md">Español</a> · <b>Português</b> · <a href="README.fr.md">Français</a> · <a href="README.de.md">Deutsch</a> · <a href="README.ru.md">Русский</a> · <a href="README.zh-CN.md">简体中文</a> · <a href="README.ja.md">日本語</a> · <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  Deixe o Claude editar áudio com o <b>Audacity 4</b> instalado no seu computador com Windows, macOS ou Linux:<br>
  dividir, cortar, converter, reamostrar, aplicar efeitos, mixar e juntar, além de uma <b>extensão dentro do Audacity</b>.
</p>

---

## Por quê

O Audacity 4 é um ótimo editor de áudio de código aberto, mas **não tem interface de scripting** (sem `mod-script-pipe`, sem exportação por linha de comando). Este servidor [Model Context Protocol](https://modelcontextprotocol.io) dá ao Claude acesso ao próprio motor de áudio do Audacity, para que você possa pedir coisas como:

> *"Divida cada gravação em `D:\voz` em trechos de 15 segundos, uma pasta por arquivo."*
> *"Converta `D:\podcast\ep1.wav` para FLAC 24 bits em 48 kHz."*
> *"Aplique um passa-altas em 80 Hz, um compressor e normalize para -1 dBFS em `D:\take3.wav`."*

Inspirado em [VectorMagic-mcp-server](https://github.com/jhonsu01/VectorMagic-mcp-server).

## Como funciona

| Parte | O que é |
| --- | --- |
| **Motor nativo** (`aumcp-engine.exe`, C++) | Carrega em tempo de execução as bibliotecas de codecs do próprio Audacity 4 (**libsndfile 1.2.2**, **mpg123**) da pasta de instalação, então lê e grava exatamente o que o seu Audacity suporta. Processa arquivos longos em streaming; reamostragem windowed-sinc de alta qualidade. |
| **Servidor MCP** (Node.js) | 10 ferramentas para o Claude. FFmpeg opcional para exportar MP3/M4A/AAC/WMA e importar formatos incomuns. |
| **Extensão do Audacity** ("MCP Audio Tools") | Extensão oficial do Audacity 4 (JavaScript + DLL nativa) que adiciona efeitos aos menus do Audacity. Instalada com uma única chamada. |

## Ferramentas

| Ferramenta | O que faz |
| --- | --- |
| `split_audio` | Divide um ou vários arquivos em trechos de N segundos (padrão 15 s), uma pasta por entrada, numerados `<nome>_001.wav`… |
| `convert_audio` | Muda formato, profundidade de bits, taxa de amostragem ou canais de um ou vários arquivos |
| `trim_audio` | Mantém um intervalo de tempo, com fade in/out opcional |
| `apply_effects` | Cadeia de efeitos (veja abaixo) |
| `mix_audio` | Mixa arquivos sobrepostos com ganho e deslocamento por arquivo |
| `concat_audio` | Junta arquivos com crossfade de potência constante ou um silêncio entre eles |
| `get_audio_info` | Formato, taxa, canais, duração, nível de pico e RMS |
| `open_in_audacity` | Abre os resultados no editor do Audacity 4 |
| `install_audacity_extension` | Instala / atualiza / remove a extensão dentro do Audacity |
| `get_audacity_status` | Caminho e versão do Audacity, motor, FFmpeg, formatos, efeitos |

## Formatos

| | |
| --- | --- |
| **Entrada (nativa)** | wav, aiff, flac, ogg, opus, mp3, mp2, caf, w64, rf64, au, voc, sd2, iff, paf… |
| **Entrada (via FFmpeg)** | m4a, aac, mp4, wma, wv, ac3, webm, mkv, ape… |
| **Saída (nativa)** | wav, flac, ogg (Vorbis), opus, aiff, caf, w64, rf64, au |
| **Saída (via FFmpeg)** | mp3, m4a, aac, wma |
| **Opções** | profundidade `pcm8/16/24/32`, `float32/64` · qualquer taxa · mono/estéreo · qualidade Vorbis/Opus · compressão FLAC · bitrate |

## Efeitos

`gain` · `normalize` · `fade_in` · `fade_out` (linear, exponencial, logarítmica, curva S) · `trim` · `pad` · `trim_silence` · `reverse` · `speed` · `invert` · `remove_dc` · `highpass` · `lowpass` · `bandpass` · `notch` · `eq` · `bass` · `treble` · `echo` · `compressor` · `limiter` · `noise_gate` · `mono` · `stereo` · `swap_channels` · `pan` · **`mouth_declick`** · **`declip`** · **`noise_reduction`** · **`click_removal`**

Exemplo: `[{"type":"highpass","frequency":80},{"type":"compressor","threshold_db":-20,"ratio":3},{"type":"normalize","peak_db":-1}]`

Voice cleanup / limpieza de voz: `[{"type":"declip"},{"type":"mouth_declick","sensitivity":6},{"type":"limiter","ceiling_db":-1}]`

## Dentro do Audacity (extensão)

Depois de `install_audacity_extension` e reiniciar o Audacity 4:

| Menu | Efeito |
| --- | --- |
| Ferramentas › Extensão › audacity-mcp-server | **Split into Files (MCP)**: grava a seleção (ou cada faixa selecionada) em arquivos de N segundos |
| | **Export Selection to File (MCP)**: WAV, FLAC, OGG, Opus, AIFF |
| | **Import Audio File as Track (MCP)**: qualquer arquivo suportado (inclusive MP3) como nova faixa |
| Analisar › Extensão › audacity-mcp-server | **Labels Every N Seconds (MCP)**: uma faixa de rótulos com uma região por intervalo |
| Ferramentas | **MCP Audio Tools: status**: verifica a biblioteca nativa |

Todos suportam desfazer e mostram barra de progresso.

## Plataformas

| | Windows | macOS | Linux |
| --- | --- | --- | --- |
| **Motor nativo** | x64 | universal (Apple Silicon + Intel) | x86_64 |
| **Codecs de áudio** | os do próprio Audacity (`sndfile.dll`, `mpg123.dll`) | os do Audacity.app, ou `libsndfile` + `mpg123` do Homebrew | `libsndfile` + `libmpg123` do sistema (o AppImage guarda os seus dentro da imagem) |
| **Pasta de extensões** | `%LOCALAPPDATA%\Audacity\Audacity4\extensions` | `~/Library/Application Support/Audacity/Audacity4/extensions` | `~/.local/share/Audacity/Audacity4/extensions` · Flatpak: `~/.var/app/org.audacityteam.Audacity/data/Audacity/Audacity4/extensions` |
| **Abrir no Audacity** | `Audacity4.exe` | `open -a Audacity` | AppImage · `audacity` · `flatpak run` |

Linux: `sudo apt install libsndfile1 libmpg123-0` (Debian/Ubuntu) · `sudo dnf install libsndfile mpg123-libs` (Fedora) · `sudo pacman -S libsndfile mpg123` (Arch). macOS, se as bibliotecas do Audacity não puderem ser carregadas sozinhas: `brew install libsndfile mpg123`. `get_audacity_status` mostra quais bibliotecas estão em uso.

## Requisitos

- Windows 10/11 x64, macOS 11+ (Apple Silicon ou Intel) ou Linux x86_64 (glibc 2.35+, ex. Ubuntu 22.04+)
- **Audacity 4** (testado com **4.0.1**)
- Node.js ≥ 20 (incluído no Claude Desktop para instalações `.mcpb`)
- Opcional: FFmpeg (apenas para exportar MP3/M4A/AAC/WMA ou importar M4A/AAC/WMA/WavPack)

## Instalação

### Claude Desktop (recomendado)

1. Baixe `audacity-mcp-server.mcpb` do [último release](https://github.com/jhonsu01/audacity-mcp-server/releases/latest).
2. Dê um duplo clique (ou arraste para o Claude Desktop → *Configurações → Extensões*) e clique em **Instalar**.
3. Opcional: se o Audacity não estiver em `C:\Program Files\Audacity 4`, informe a pasta nas configurações da extensão; informe o caminho do FFmpeg se ele não estiver no `PATH`.
4. Opcional: peça ao Claude *"instale a extensão do Audacity"* e reinicie o Audacity.

### Claude Code / outros clientes MCP

```bash
git clone https://github.com/jhonsu01/audacity-mcp-server.git
cd audacity-mcp-server
npm install
npm run build:all
claude mcp add audacity -- node "$PWD/dist/bundle.cjs"        # macOS / Linux
claude mcp add audacity -- node "%CD%\dist\bundle.cjs"      # Windows (cmd)
```

`build:all` compila as partes nativas e precisa do Visual Studio com a carga de trabalho C++.

## Configuração

| Variável | Significado |
| --- | --- |
| `AUDACITY_DIR` | Pasta de instalação do Audacity 4 (ou `Audacity4.exe`). Padrão: `C:\Program Files\Audacity 4` |
| `FFMPEG_PATH` | `ffmpeg.exe` ou sua pasta. Padrão: do `PATH` |
| `AUDACITY_MCP_TIMEOUT` | Segundos máximos por tarefa (padrão 1800) |

## Segurança

- Os arquivos **nunca são sobrescritos**, a menos que `overwrite: true`.
- Todos os caminhos devem ser absolutos; a entrada nunca é modificada.
- Tudo roda localmente, sem acesso à rede.

## Desenvolvimento

```bash
npm run build:native   # aumcp-engine.exe + audacity_mcp_native.dll (MSVC)
npm run build          # TypeScript + bundle
npm run test:e2e       # 25 verificações end-to-end via stdio MCP real (áudio sintético)
npm run pack:mcpb      # audacity-mcp-server.mcpb
```

## Licença

GPL-3.0-only. O cabeçalho ABI da extensão vem do Audacity (GPL-3.0).
Audacity® é uma marca registrada da Muse Group. Este projeto não é afiliado nem endossado pelo Audacity ou pela Muse Group.
