# Changelog

## [1.1.0] - 2026-10-06
### Added
- `mouth_declick` effect: removes mouth clicks, lip smacks and saliva ticks from voice recordings.
  Detection on the AR prediction residual (Godsill & Rayner) with a robust per-block threshold; voice
  pulses (periodic glottal transients) and consonant bursts (t, k, p) are left alone. Repair rebuilds only
  the high band (above ~1.5 kHz) by least-squares AR interpolation, keeping the voice body as recorded.
  Benchmark on a real voice with 300 injected clicks: 89 % removed with -34 dB collateral change
  (FFmpeg adeclick: 27 %, Audacity Click Removal: 17 %).
- `declip` effect: rebuilds clipped (saturated) peaks by LPC interpolation.
- `noise_reduction` effect: port of Audacity 4's Noise Reduction (2048-point Hann/Hann, second-greatest
  discrimination, attack/release, frequency smoothing) with an automatic noise profile from the quietest frames.
- `click_removal` effect: port of Audacity 4's Click Removal.
- `apply_effects` results include `effect_reports` (clicks repaired, clipped runs rebuilt, noise profile used).

## [1.0.0] - 2026-10-06
### Added
- Native engine (C++) on Audacity 4's libsndfile/mpg123: split, process (trim + effects + convert), mix, concat, info.
- 10 MCP tools, FFmpeg fallback for MP3/M4A/AAC/WMA.
- Audacity 4 extension "MCP Audio Tools": Split into Files, Export Selection to File, Import Audio File as Track, Labels Every N Seconds (verified inside Audacity 4.0.1).
