// Audio processing used by the MCP engine and the Audacity extension.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "audio.h"

namespace aumcp::dsp {

struct Effect {
    std::string type;
    std::map<std::string, double> num;
    std::map<std::string, std::string> str;
    double get(const std::string& k, double def) const
    {
        auto it = num.find(k);
        return it == num.end() ? def : it->second;
    }
    std::string text(const std::string& k, const std::string& def) const
    {
        auto it = str.find(k);
        return it == str.end() ? def : it->second;
    }
};

struct EffectInfo {
    const char* type;
    const char* params;
    const char* description;
};
const std::vector<EffectInfo>& effectCatalog();

// Applies one effect in place. Returns false with a message on invalid parameters.
bool apply(Audio& audio, const Effect& effect, std::string& error);

// Channel mapping + sample-rate conversion (windowed-sinc).
Audio convert(const Audio& in, int rate, int channels);
Audio resample(const Audio& in, int rate);
Audio remap(const Audio& in, int channels);

float peak(const Audio& a);
double rmsDb(const Audio& a);

} // namespace aumcp::dsp
