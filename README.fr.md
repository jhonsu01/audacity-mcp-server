<p align="center"><img src="icon.png" width="120" alt="Icône d'Audacity Bridge"></p>

<h1 align="center">Audacity MCP Server</h1>

<p align="center">
  <a href="README.md">English</a> · <a href="README.es.md">Español</a> · <a href="README.pt-BR.md">Português</a> · <b>Français</b> · <a href="README.de.md">Deutsch</a> · <a href="README.ru.md">Русский</a> · <a href="README.zh-CN.md">简体中文</a> · <a href="README.ja.md">日本語</a> · <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  Laissez Claude éditer de l'audio avec l'<b>Audacity 4</b> installé sur votre ordinateur Windows, macOS ou Linux :<br>
  découper, rogner, convertir, rééchantillonner, appliquer des effets, mixer et assembler, plus une <b>extension intégrée à Audacity</b>.
</p>

---

## Pourquoi

Audacity 4 est un excellent éditeur audio open source, mais il **n'a pas d'interface de script** (pas de `mod-script-pipe`, pas d'export en ligne de commande). Ce serveur [Model Context Protocol](https://modelcontextprotocol.io) donne à Claude l'accès au moteur audio d'Audacity lui-même, pour demander par exemple :

> *« Découpe chaque enregistrement de `D:\voix` en morceaux de 15 secondes, un dossier par fichier. »*
> *« Convertis `D:\podcast\ep1.wav` en FLAC 24 bits à 48 kHz. »*
> *« Applique un passe-haut à 80 Hz, un compresseur et normalise à -1 dBFS sur `D:\prise3.wav`. »*

Inspiré de [VectorMagic-mcp-server](https://github.com/jhonsu01/VectorMagic-mcp-server).

## Fonctionnement

| Élément | Description |
| --- | --- |
| **Moteur natif** (`aumcp-engine.exe`, C++) | Charge à l'exécution les bibliothèques de codecs d'Audacity 4 (**libsndfile 1.2.2**, **mpg123**) depuis son dossier d'installation : il lit et écrit exactement ce que votre Audacity prend en charge. Traitement en flux des longs fichiers ; rééchantillonnage windowed-sinc de haute qualité. |
| **Serveur MCP** (Node.js) | 10 outils pour Claude. FFmpeg optionnel pour l'export MP3/M4A/AAC/WMA et les imports exotiques. |
| **Extension Audacity** (« MCP Audio Tools ») | Extension officielle d'Audacity 4 (JavaScript + DLL native) qui ajoute des effets aux menus d'Audacity. Installée en un seul appel. |

## Outils

| Outil | Rôle |
| --- | --- |
| `split_audio` | Découpe un ou plusieurs fichiers en morceaux de N secondes (15 s par défaut), un dossier par entrée, numérotés `<nom>_001.wav`… |
| `convert_audio` | Change le format, la résolution, la fréquence d'échantillonnage ou les canaux |
| `trim_audio` | Garde une plage de temps, avec fondu d'entrée/sortie optionnel |
| `apply_effects` | Chaîne d'effets (voir ci-dessous) |
| `mix_audio` | Mixe des fichiers superposés avec gain et décalage par fichier |
| `concat_audio` | Assemble des fichiers avec un fondu enchaîné à puissance constante ou un silence |
| `get_audio_info` | Format, fréquence, canaux, durée, niveau crête et RMS |
| `open_in_audacity` | Ouvre les résultats dans l'éditeur Audacity 4 |
| `install_audacity_extension` | Installe / met à jour / supprime l'extension intégrée |
| `get_audacity_status` | Chemin et version d'Audacity, moteur, FFmpeg, formats, effets |

## Formats

| | |
| --- | --- |
| **Entrée (native)** | wav, aiff, flac, ogg, opus, mp3, mp2, caf, w64, rf64, au, voc, sd2, iff, paf… |
| **Entrée (via FFmpeg)** | m4a, aac, mp4, wma, wv, ac3, webm, mkv, ape… |
| **Sortie (native)** | wav, flac, ogg (Vorbis), opus, aiff, caf, w64, rf64, au |
| **Sortie (via FFmpeg)** | mp3, m4a, aac, wma |
| **Options** | résolution `pcm8/16/24/32`, `float32/64` · toute fréquence · mono/stéréo · qualité Vorbis/Opus · compression FLAC · débit |

## Effets

`gain` · `normalize` · `fade_in` · `fade_out` (linéaire, exponentiel, logarithmique, courbe en S) · `trim` · `pad` · `trim_silence` · `reverse` · `speed` · `invert` · `remove_dc` · `highpass` · `lowpass` · `bandpass` · `notch` · `eq` · `bass` · `treble` · `echo` · `compressor` · `limiter` · `noise_gate` · `mono` · `stereo` · `swap_channels` · `pan` · **`mouth_declick`** · **`declip`** · **`noise_reduction`** · **`click_removal`**

Exemple : `[{"type":"highpass","frequency":80},{"type":"compressor","threshold_db":-20,"ratio":3},{"type":"normalize","peak_db":-1}]`

Voice cleanup / limpieza de voz: `[{"type":"declip"},{"type":"mouth_declick","sensitivity":6},{"type":"limiter","ceiling_db":-1}]`

## Dans Audacity (extension)

Après `install_audacity_extension` et un redémarrage d'Audacity 4 :

| Menu | Effet |
| --- | --- |
| Outils › Extension › audacity-mcp-server | **Split into Files (MCP)** : écrit la sélection (ou chaque piste sélectionnée) en fichiers de N secondes |
| | **Export Selection to File (MCP)** : WAV, FLAC, OGG, Opus, AIFF |
| | **Import Audio File as Track (MCP)** : tout fichier pris en charge (MP3 compris) comme nouvelle piste |
| Analyse › Extension › audacity-mcp-server | **Labels Every N Seconds (MCP)** : une piste de marqueurs avec une région par intervalle |
| Outils | **MCP Audio Tools: status** : vérifie la bibliothèque native |

Tous prennent en charge l'annulation et affichent une barre de progression.

## Plateformes

| | Windows | macOS | Linux |
| --- | --- | --- | --- |
| **Moteur natif** | x64 | universel (Apple Silicon + Intel) | x86_64 |
| **Codecs audio** | ceux d'Audacity (`sndfile.dll`, `mpg123.dll`) | ceux d'Audacity.app, ou `libsndfile` + `mpg123` de Homebrew | `libsndfile` + `libmpg123` du système (l'AppImage garde les siens dans l'image) |
| **Dossier des extensions** | `%LOCALAPPDATA%\Audacity\Audacity4\extensions` | `~/Library/Application Support/Audacity/Audacity4/extensions` | `~/.local/share/Audacity/Audacity4/extensions` · Flatpak : `~/.var/app/org.audacityteam.Audacity/data/Audacity/Audacity4/extensions` |
| **Ouvrir dans Audacity** | `Audacity4.exe` | `open -a Audacity` | AppImage · `audacity` · `flatpak run` |

Linux : `sudo apt install libsndfile1 libmpg123-0` (Debian/Ubuntu) · `sudo dnf install libsndfile mpg123-libs` (Fedora) · `sudo pacman -S libsndfile mpg123` (Arch). macOS, si les bibliothèques d'Audacity ne peuvent pas être chargées seules : `brew install libsndfile mpg123`. `get_audacity_status` indique les bibliothèques utilisées.

## Prérequis

- Windows 10/11 x64, macOS 11+ (Apple Silicon ou Intel) ou Linux x86_64 (glibc 2.35+, ex. Ubuntu 22.04+)
- **Audacity 4** (testé avec **4.0.1**)
- Node.js ≥ 20 (inclus dans Claude Desktop pour les installations `.mcpb`)
- Optionnel : FFmpeg (uniquement pour exporter en MP3/M4A/AAC/WMA ou importer M4A/AAC/WMA/WavPack)

## Installation

### Claude Desktop (recommandé)

1. Téléchargez `audacity-mcp-server.mcpb` depuis la [dernière version](https://github.com/jhonsu01/audacity-mcp-server/releases/latest).
2. Double-cliquez dessus (ou glissez-le dans Claude Desktop → *Paramètres → Extensions*) et cliquez sur **Installer**.
3. Optionnel : si Audacity n'est pas dans `C:\Program Files\Audacity 4`, indiquez son dossier dans les paramètres de l'extension ; indiquez le chemin de FFmpeg s'il n'est pas dans le `PATH`.
4. Optionnel : demandez à Claude *« installe l'extension Audacity »*, puis redémarrez Audacity.

### Claude Code / autres clients MCP

```bash
git clone https://github.com/jhonsu01/audacity-mcp-server.git
cd audacity-mcp-server
npm install
npm run build:all
claude mcp add audacity -- node "$PWD/dist/bundle.cjs"        # macOS / Linux
claude mcp add audacity -- node "%CD%\dist\bundle.cjs"      # Windows (cmd)
```

`build:all` compile les parties natives et nécessite Visual Studio avec la charge de travail C++.

## Configuration

| Variable | Signification |
| --- | --- |
| `AUDACITY_DIR` | Dossier d'installation d'Audacity 4 (ou `Audacity4.exe`). Par défaut : `C:\Program Files\Audacity 4` |
| `FFMPEG_PATH` | `ffmpeg.exe` ou son dossier. Par défaut : depuis le `PATH` |
| `AUDACITY_MCP_TIMEOUT` | Durée maximale par tâche en secondes (1800 par défaut) |

## Sécurité

- Les fichiers ne sont **jamais écrasés** sauf avec `overwrite: true`.
- Tous les chemins doivent être absolus ; l'entrée n'est jamais modifiée.
- Tout s'exécute en local, sans accès réseau.

## Développement

```bash
npm run build:native   # aumcp-engine.exe + audacity_mcp_native.dll (MSVC)
npm run build          # TypeScript + bundle
npm run test:e2e       # 25 vérifications de bout en bout via stdio MCP réel (audio synthétique)
npm run pack:mcpb      # audacity-mcp-server.mcpb
```

## Licence

GPL-3.0-only. L'en-tête ABI de l'extension provient d'Audacity (GPL-3.0).
Audacity® est une marque déposée de Muse Group. Ce projet n'est ni affilié ni approuvé par Audacity ou Muse Group.
