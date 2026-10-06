// Audio file I/O on top of Audacity's codec libraries.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "codecs.h"

namespace aumcp {

struct SourceInfo {
    int rate = 0;
    int channels = 0;
    int64_t frames = 0;
    std::string container; // wav, flac, mp3...
    std::string sample;    // pcm16, float32, vorbis, mp3...
    double seconds() const { return rate > 0 ? static_cast<double>(frames) / rate : 0.0; }
};

class Reader
{
public:
    virtual ~Reader() = default;
    const SourceInfo& info() const { return m_info; }
    // Reads up to `frames` interleaved frames; returns the number read (0 at the end).
    virtual size_t read(float* interleaved, size_t frames) = 0;
    virtual bool seek(int64_t frame) = 0;

protected:
    SourceInfo m_info;
};

// Opens any format Audacity's libsndfile reads (WAV, AIFF, FLAC, OGG Vorbis, Opus, CAF, W64, RF64,
// AU...) plus MP3/MP2 through mpg123.
std::unique_ptr<Reader> openReader(const std::wstring& path, std::string& error);

// Requested output. Empty / zero values mean "same as the source" or "format default".
struct OutputSpec {
    std::string format;          // wav, aiff, flac, ogg, opus, caf, w64, rf64, au. Empty: from the file extension.
    std::string sample = "auto"; // auto, pcm8, pcm16, pcm24, pcm32, float32, float64
    int rate = 0;
    int channels = 0;
    double quality = -1.0;     // 0..1, Vorbis / Opus
    double compression = -1.0; // 0..1, FLAC
};

struct ResolvedOutput {
    int sfFormat = 0;
    std::string format;
    std::string extension;
    std::string sample;
    int rate = 0;
    int channels = 0;
    double quality = -1.0;
    double compression = -1.0;
};

bool resolveOutput(const OutputSpec& spec, const SourceInfo& source, ResolvedOutput& out, std::string& error);
std::string formatFromExtension(const std::wstring& path);

class Writer
{
public:
    ~Writer();
    bool write(const float* interleaved, size_t frames);
    bool close(std::string* error = nullptr);
    int64_t frames() const { return m_frames; }
    const ResolvedOutput& output() const { return m_out; }

private:
    friend std::unique_ptr<Writer> openWriter(const std::wstring&, const ResolvedOutput&, std::string&);
    SNDFILE* m_file = nullptr;
    ResolvedOutput m_out;
    int64_t m_frames = 0;
};

std::unique_ptr<Writer> openWriter(const std::wstring& path, const ResolvedOutput& out, std::string& error);

// Planar float buffer.
struct Audio {
    int rate = 0;
    std::vector<std::vector<float> > ch;
    size_t frames() const { return ch.empty() ? 0 : ch[0].size(); }
    int channels() const { return static_cast<int>(ch.size()); }
    double seconds() const { return rate > 0 ? static_cast<double>(frames()) / rate : 0.0; }
};

// Reads [start, end) seconds (end <= 0: to the end of the file).
bool loadAudio(const std::wstring& path, double start, double end, Audio& audio, SourceInfo& info, std::string& error);
bool saveAudio(const std::wstring& path, const Audio& audio, const ResolvedOutput& out, std::string& error);

std::vector<std::string> outputFormats();
std::vector<std::string> sampleFormats(const std::string& format);

} // namespace aumcp
