// Audio restoration: Audacity's Noise Reduction and Click Removal algorithms (ported from
// Audacity 4.0.1, GPL) plus an LPC-based mouth de-click for voice recordings.
#pragma once

#include <string>

#include "audio.h"

namespace aumcp::restore {

struct NoiseReductionParams {
    double reductionDb = 12.0;   // Audacity "Noise reduction (dB)", 0..48
    double sensitivity = 6.0;    // Audacity "Sensitivity", 0.01..24
    int smoothingBands = 6;      // Audacity "Frequency smoothing (bands)", 0..12
    double profileStart = -1.0;  // noise-only region in seconds; < 0 = automatic (quietest frames)
    double profileEnd = -1.0;
    double autoPercent = 10.0;   // automatic profile: quietest % of the frames
};
bool noiseReduction(Audio& audio, const NoiseReductionParams& p, std::string& report, std::string& error);

// Audacity "Click Removal": threshold 0..900 (default 200), width 0..40 samples (default 20).
bool clickRemoval(Audio& audio, int threshold, int width, std::string& report, std::string& error);

struct DeclickParams {
    double sensitivity = 6.0;  // 1..10, higher repairs more (and quieter) clicks
    double maxClickMs = 4.0;   // longer impulsive events are left alone (plosives, consonants)
    double frequency = 3000.0; // detection band (Hz)
};
bool mouthDeclick(Audio& audio, const DeclickParams& p, std::string& report, std::string& error);

// Rebuilds clipped peaks (|x| >= threshold) by LPC interpolation; never lowers a clipped sample.
bool declip(Audio& audio, double thresholdDb, std::string& report, std::string& error);

} // namespace aumcp::restore
