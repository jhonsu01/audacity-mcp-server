#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <map>

#include "dsp.h"

namespace aumcp {
namespace {

struct ContainerDef {
    const char* name;
    int major;
    const char* ext;
    std::vector<std::string> samples; // allowed sample formats, first = default
};

const std::vector<ContainerDef>& containers()
{
    static const std::vector<ContainerDef> defs = {
        { "wav", SF_FORMAT_WAV, "wav", { "pcm16", "pcm24", "pcm32", "float32", "float64", "pcm8" } },
        { "aiff", SF_FORMAT_AIFF, "aiff", { "pcm16", "pcm24", "pcm32", "float32", "float64", "pcm8" } },
        { "flac", SF_FORMAT_FLAC, "flac", { "pcm16", "pcm24", "pcm8" } },
        { "ogg", SF_FORMAT_OGG, "ogg", { "vorbis" } },
        { "opus", SF_FORMAT_OGG, "opus", { "opus" } },
        { "caf", SF_FORMAT_CAF, "caf", { "pcm16", "pcm24", "pcm32", "float32", "float64", "pcm8" } },
        { "w64", SF_FORMAT_W64, "w64", { "pcm16", "pcm24", "pcm32", "float32", "float64", "pcm8" } },
        { "rf64", SF_FORMAT_RF64, "wav", { "pcm16", "pcm24", "pcm32", "float32", "float64", "pcm8" } },
        { "au", SF_FORMAT_AU, "au", { "pcm16", "pcm24", "pcm32", "float32", "float64", "pcm8" } },
    };
    return defs;
}

const ContainerDef* findContainer(const std::string& name)
{
    for (const auto& c : containers()) {
        if (name == c.name) {
            return &c;
        }
    }
    return nullptr;
}

int subtypeOf(const std::string& sample, int major)
{
    if (sample == "pcm8") {
        return (major == SF_FORMAT_WAV || major == SF_FORMAT_W64 || major == SF_FORMAT_RF64) ? SF_FORMAT_PCM_U8 : SF_FORMAT_PCM_S8;
    }
    if (sample == "pcm16") {
        return SF_FORMAT_PCM_16;
    }
    if (sample == "pcm24") {
        return SF_FORMAT_PCM_24;
    }
    if (sample == "pcm32") {
        return SF_FORMAT_PCM_32;
    }
    if (sample == "float32") {
        return SF_FORMAT_FLOAT;
    }
    if (sample == "float64") {
        return SF_FORMAT_DOUBLE;
    }
    if (sample == "vorbis") {
        return SF_FORMAT_VORBIS;
    }
    if (sample == "opus") {
        return SF_FORMAT_OPUS;
    }
    return 0;
}

std::string sampleName(int subtype)
{
    switch (subtype) {
    case SF_FORMAT_PCM_S8:
    case SF_FORMAT_PCM_U8: return "pcm8";
    case SF_FORMAT_PCM_16: return "pcm16";
    case SF_FORMAT_PCM_24: return "pcm24";
    case SF_FORMAT_PCM_32: return "pcm32";
    case SF_FORMAT_FLOAT: return "float32";
    case SF_FORMAT_DOUBLE: return "float64";
    case SF_FORMAT_VORBIS: return "vorbis";
    case SF_FORMAT_OPUS: return "opus";
    default: return "other";
    }
}

std::string lowerExt(const std::filesystem::path& path)
{
    std::string e = u8(path.extension());
    if (!e.empty() && e[0] == '.') {
        e.erase(0, 1);
    }
    for (auto& c : e) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return e;
}

FILE* openBinary(const std::filesystem::path& p)
{
#ifdef _WIN32
    return _wfopen(p.c_str(), L"rb");
#else
    return fopen(p.c_str(), "rb");
#endif
}

// ---------------------------------------------------------------- libsndfile reader
class SfReader final : public Reader
{
public:
    explicit SfReader(SNDFILE* f, const SF_INFO& info) : m_file(f)
    {
        m_info.rate = info.samplerate;
        m_info.channels = info.channels;
        m_info.frames = info.frames;
        const int major = info.format & SF_FORMAT_TYPEMASK;
        const int sub = info.format & SF_FORMAT_SUBMASK;
        m_info.sample = sampleName(sub);
        m_info.container = "other";
        for (const auto& c : containers()) {
            if (c.major == major) {
                m_info.container = c.name;
                break;
            }
        }
        if (major == SF_FORMAT_OGG) {
            m_info.container = sub == SF_FORMAT_OPUS ? "opus" : "ogg";
        }
        if (m_info.container == "other") {
            SF_FORMAT_INFO fi{ major, nullptr, nullptr };
            if (sndfile().command(nullptr, SFC_GET_FORMAT_INFO, &fi, sizeof(fi)) == 0 && fi.extension) {
                m_info.container = fi.extension;
            }
        }
    }
    ~SfReader() override
    {
        if (m_file) {
            sndfile().close(m_file);
        }
    }
    size_t read(float* interleaved, size_t frames) override
    {
        const sf_count_t n = sndfile().readf_float(m_file, interleaved, static_cast<sf_count_t>(frames));
        return n > 0 ? static_cast<size_t>(n) : 0;
    }
    bool seek(int64_t frame) override { return sndfile().seek(m_file, frame, 0 /*SEEK_SET*/) >= 0; }

private:
    SNDFILE* m_file = nullptr;
};

// ---------------------------------------------------------------- mpg123 reader (decodes into memory)
class MemoryReader final : public Reader
{
public:
    MemoryReader(SourceInfo info, std::vector<float> data) : m_data(std::move(data))
    {
        m_info = std::move(info);
        m_info.frames = m_info.channels > 0 ? static_cast<int64_t>(m_data.size() / m_info.channels) : 0;
    }
    size_t read(float* interleaved, size_t frames) override
    {
        const size_t total = static_cast<size_t>(m_info.frames);
        const size_t n = std::min(frames, total - std::min(total, m_pos));
        std::copy_n(m_data.data() + m_pos * m_info.channels, n * m_info.channels, interleaved);
        m_pos += n;
        return n;
    }
    bool seek(int64_t frame) override
    {
        if (frame < 0 || frame > m_info.frames) {
            return false;
        }
        m_pos = static_cast<size_t>(frame);
        return true;
    }

private:
    std::vector<float> m_data;
    size_t m_pos = 0;
};

std::unique_ptr<Reader> openMp3(const std::filesystem::path& path, std::string& error)
{
    Mpg123& m = mpg123();
    if (!m.loaded) {
        error = m.error;
        return nullptr;
    }
    FILE* fp = openBinary(path);
    if (!fp) {
        error = "Cannot open file";
        return nullptr;
    }
    int err = 0;
    mpg123_handle* h = m.create(nullptr, &err);
    if (!h) {
        fclose(fp);
        error = "mpg123_new failed";
        return nullptr;
    }
    bool useFloat = true;
    m.format_none(h);
    static const long rates[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };
    for (long r : rates) {
        if (m.format(h, r, MPG123_MONO | MPG123_STEREO, MPG123_ENC_FLOAT_32) != MPG123_OK) {
            useFloat = false;
            break;
        }
    }
    if (!useFloat) {
        m.format_none(h);
        for (long r : rates) {
            m.format(h, r, MPG123_MONO | MPG123_STEREO, MPG123_ENC_SIGNED_16);
        }
    }
    m.open_feed(h);

    std::vector<unsigned char> in(1 << 16);
    std::vector<unsigned char> out(1 << 18);
    std::vector<float> pcm;
    long rate = 0;
    int channels = 0, encoding = 0;
    bool failed = false;
    bool eof = false;
    while (!failed) {
        size_t got = 0;
        if (!eof) {
            got = fread(in.data(), 1, in.size(), fp);
            if (got == 0) {
                eof = true;
            }
        }
        const unsigned char* feed = got ? in.data() : nullptr;
        size_t feedSize = got;
        for (;;) {
            size_t done = 0;
            const int rc = m.decode(h, feed, feedSize, out.data(), out.size(), &done);
            feed = nullptr;
            feedSize = 0;
            if (rc == MPG123_NEW_FORMAT) {
                m.getformat(h, &rate, &channels, &encoding);
            }
            if (done > 0) {
                if (encoding == MPG123_ENC_FLOAT_32) {
                    const float* f = reinterpret_cast<const float*>(out.data());
                    pcm.insert(pcm.end(), f, f + done / sizeof(float));
                } else {
                    const int16_t* s = reinterpret_cast<const int16_t*>(out.data());
                    for (size_t i = 0; i < done / sizeof(int16_t); ++i) {
                        pcm.push_back(s[i] / 32768.0f);
                    }
                }
            }
            if (rc == MPG123_NEED_MORE || rc == MPG123_DONE) {
                break;
            }
            if (rc == MPG123_ERR) {
                failed = true;
                error = "MP3 decode error";
                break;
            }
            if (rc != MPG123_OK && rc != MPG123_NEW_FORMAT) {
                // Recoverable warnings (e.g. bad frame): keep going
                if (done == 0) {
                    break;
                }
            }
        }
        if (eof) {
            break;
        }
    }
    fclose(fp);
    m.close(h);
    m.destroy(h);
    if (failed) {
        return nullptr;
    }
    if (rate <= 0 || channels <= 0 || pcm.empty()) {
        error = "Not a decodable MPEG audio file";
        return nullptr;
    }
    SourceInfo info;
    info.rate = static_cast<int>(rate);
    info.channels = channels;
    info.container = "mp3";
    info.sample = "mp3";
    return std::make_unique<MemoryReader>(info, std::move(pcm));
}

} // namespace

std::string formatFromExtension(const std::filesystem::path& path)
{
    const std::string e = lowerExt(path);
    if (e == "wav" || e == "wave") {
        return "wav";
    }
    if (e == "aif" || e == "aiff" || e == "aifc") {
        return "aiff";
    }
    if (e == "flac") {
        return "flac";
    }
    if (e == "ogg" || e == "oga") {
        return "ogg";
    }
    if (e == "opus") {
        return "opus";
    }
    if (e == "caf") {
        return "caf";
    }
    if (e == "w64") {
        return "w64";
    }
    if (e == "au" || e == "snd") {
        return "au";
    }
    return {};
}

std::unique_ptr<Reader> openReader(const std::filesystem::path& path, std::string& error)
{
    const std::string ext = lowerExt(path);
    if (ext == "mp3" || ext == "mp2" || ext == "mpga" || ext == "mpeg") {
        // mpg123 (Audacity's MP3 decoder) when available; otherwise libsndfile >= 1.1 built with MPEG support.
        if (mpg123().loaded) {
            return openMp3(path, error);
        }
    }
    Sndfile& sf = sndfile();
    if (!sf.loaded) {
        error = sf.error;
        return nullptr;
    }
    SF_INFO info{};
    SNDFILE* f = sfOpen(path, SFM_READ, &info);
    if (!f) {
        const std::string sfErr = sf.strerror(nullptr);
        // libsndfile does not read MPEG in this build: try mpg123 for mislabelled files
        std::string mp3Err;
        if (auto r = openMp3(path, mp3Err)) {
            return r;
        }
        error = "Unsupported or unreadable audio file: " + sfErr;
        return nullptr;
    }
    if (info.channels < 1 || info.samplerate < 1) {
        sf.close(f);
        error = "Invalid audio stream";
        return nullptr;
    }
    return std::make_unique<SfReader>(f, info);
}

bool resolveOutput(const OutputSpec& spec, const SourceInfo& source, ResolvedOutput& out, std::string& error)
{
    std::string format = spec.format;
    for (auto& c : format) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    if (format == "aif") {
        format = "aiff";
    } else if (format == "oga" || format == "vorbis") {
        format = "ogg";
    }
    const ContainerDef* c = findContainer(format);
    if (!c) {
        error = "Unsupported output format \"" + spec.format + "\". Use one of: wav, aiff, flac, ogg, opus, caf, w64, rf64, au";
        return false;
    }
    std::string sample = spec.sample.empty() ? "auto" : spec.sample;
    if (sample == "auto") {
        // Keep the source resolution when the container supports it; otherwise the container default.
        sample = c->samples.front();
        std::string want = source.sample;
        if (format == "flac" && (want == "float32" || want == "float64" || want == "pcm32")) {
            want = "pcm24";
        }
        if (std::find(c->samples.begin(), c->samples.end(), want) != c->samples.end()) {
            sample = want;
        }
    } else if (std::find(c->samples.begin(), c->samples.end(), sample) == c->samples.end()) {
        std::string allowed;
        for (const auto& s : c->samples) {
            allowed += (allowed.empty() ? "" : ", ") + s;
        }
        error = "Sample format \"" + sample + "\" is not valid for " + format + ". Use: auto, " + allowed;
        return false;
    }
    int rate = spec.rate > 0 ? spec.rate : source.rate;
    if (rate < 1000 || rate > 384000) {
        error = "sample_rate must be between 1000 and 384000";
        return false;
    }
    if (format == "opus") {
        // libopus only encodes these rates
        static const int opusRates[] = { 8000, 12000, 16000, 24000, 48000 };
        if (std::find(std::begin(opusRates), std::end(opusRates), rate) == std::end(opusRates)) {
            if (spec.rate > 0) {
                error = "Opus supports 8000, 12000, 16000, 24000 or 48000 Hz";
                return false;
            }
            rate = 48000;
        }
    }
    if (format == "flac" && rate > 655350) {
        error = "FLAC sample rate too high";
        return false;
    }
    int channels = spec.channels > 0 ? spec.channels : source.channels;
    if (channels < 1 || channels > 8) {
        error = "channels must be between 1 and 8";
        return false;
    }
    if ((format == "flac" || format == "ogg" || format == "opus") && channels > 8) {
        error = "Too many channels for " + format;
        return false;
    }
    out.format = format;
    out.extension = c->ext;
    out.sample = sample;
    out.sfFormat = c->major | subtypeOf(sample, c->major);
    out.rate = rate;
    out.channels = channels;
    out.quality = spec.quality;
    out.compression = spec.compression;
    return true;
}

Writer::~Writer()
{
    close();
}

bool Writer::write(const float* interleaved, size_t frames)
{
    if (!m_file || frames == 0) {
        return frames == 0;
    }
    const sf_count_t n = sndfile().writef_float(m_file, interleaved, static_cast<sf_count_t>(frames));
    m_frames += n > 0 ? n : 0;
    return n == static_cast<sf_count_t>(frames);
}

bool Writer::close(std::string* error)
{
    if (!m_file) {
        return true;
    }
    const int rc = sndfile().close(m_file);
    m_file = nullptr;
    if (rc != 0 && error) {
        *error = "Error closing output file";
    }
    return rc == 0;
}

std::unique_ptr<Writer> openWriter(const std::filesystem::path& path, const ResolvedOutput& out, std::string& error)
{
    Sndfile& sf = sndfile();
    if (!sf.loaded) {
        error = sf.error;
        return nullptr;
    }
    SF_INFO info{};
    info.samplerate = out.rate;
    info.channels = out.channels;
    info.format = out.sfFormat;
    SNDFILE* f = sfOpen(path, SFM_WRITE, &info);
    if (!f) {
        error = std::string("Cannot create output file (") + out.format + "/" + out.sample + ", "
                + std::to_string(out.rate) + " Hz, " + std::to_string(out.channels) + " ch): " + sf.strerror(nullptr);
        return nullptr;
    }
    int on = 1;
    sf.command(f, SFC_SET_CLIPPING, nullptr, on);
    if (out.quality >= 0.0 && (out.format == "ogg" || out.format == "opus")) {
        double q = std::clamp(out.quality, 0.0, 1.0);
        sf.command(f, SFC_SET_VBR_ENCODING_QUALITY, &q, sizeof(q));
    }
    if (out.compression >= 0.0 && out.format == "flac") {
        double c = std::clamp(out.compression, 0.0, 1.0);
        sf.command(f, SFC_SET_COMPRESSION_LEVEL, &c, sizeof(c));
    }
    static const char software[] = "audacity-mcp-server";
    sf.command(f, SFC_SET_STRING, const_cast<char*>(software), SF_STR_SOFTWARE);
    auto w = std::unique_ptr<Writer>(new Writer());
    w->m_file = f;
    w->m_out = out;
    return w;
}

bool loadAudio(const std::filesystem::path& path, double start, double end, Audio& audio, SourceInfo& info, std::string& error)
{
    auto r = openReader(path, error);
    if (!r) {
        return false;
    }
    info = r->info();
    const int nch = info.channels;
    int64_t first = static_cast<int64_t>(std::llround(std::max(0.0, start) * info.rate));
    int64_t last = end > 0.0 ? static_cast<int64_t>(std::llround(end * info.rate)) : info.frames;
    if (info.frames > 0) {
        last = std::min(last, info.frames);
    }
    if (first > 0 && !r->seek(first)) {
        error = "Start time is beyond the end of the file";
        return false;
    }
    if (last <= first && info.frames > 0) {
        error = "Empty time range (start must be before end and inside the file)";
        return false;
    }
    audio.rate = info.rate;
    audio.ch.assign(nch, {});
    const size_t want = static_cast<size_t>(std::max<int64_t>(0, last - first));
    for (auto& c : audio.ch) {
        c.reserve(want);
    }
    std::vector<float> buf(static_cast<size_t>(8192) * nch);
    size_t remaining = want;
    while (remaining > 0) {
        const size_t n = r->read(buf.data(), std::min<size_t>(8192, remaining));
        if (n == 0) {
            break;
        }
        for (size_t i = 0; i < n; ++i) {
            for (int c = 0; c < nch; ++c) {
                audio.ch[c].push_back(buf[i * nch + c]);
            }
        }
        remaining -= n;
    }
    return true;
}

bool saveAudio(const std::filesystem::path& path, const Audio& input, const ResolvedOutput& out, std::string& error)
{
    Audio conv = dsp::convert(input, out.rate, out.channels);
    auto w = openWriter(path, out, error);
    if (!w) {
        return false;
    }
    const int nch = conv.channels();
    const size_t frames = conv.frames();
    std::vector<float> buf(static_cast<size_t>(8192) * nch);
    for (size_t pos = 0; pos < frames; pos += 8192) {
        const size_t n = std::min<size_t>(8192, frames - pos);
        for (size_t i = 0; i < n; ++i) {
            for (int c = 0; c < nch; ++c) {
                buf[i * nch + c] = conv.ch[c][pos + i];
            }
        }
        if (!w->write(buf.data(), n)) {
            error = "Write error (disk full?)";
            w->close();
            std::error_code rmErr;
            std::filesystem::remove(path, rmErr);
            return false;
        }
    }
    if (!w->close(&error)) {
        return false;
    }
    return true;
}

std::vector<std::string> outputFormats()
{
    std::vector<std::string> r;
    for (const auto& c : containers()) {
        r.push_back(c.name);
    }
    return r;
}

std::vector<std::string> sampleFormats(const std::string& format)
{
    const ContainerDef* c = findContainer(format);
    return c ? c->samples : std::vector<std::string>{};
}

} // namespace aumcp
