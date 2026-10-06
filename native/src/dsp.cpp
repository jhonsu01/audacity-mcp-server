#include "dsp.h"

#include "restore.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace aumcp::dsp {
namespace {

constexpr double PI = 3.14159265358979323846;

double dbToGain(double db)
{
    return std::pow(10.0, db / 20.0);
}

// Blackman-Harris window over [-1, 1]
double window(double x)
{
    if (x <= -1.0 || x >= 1.0) {
        return 0.0;
    }
    const double t = (x + 1.0) * 0.5; // 0..1
    return 0.35875 - 0.48829 * std::cos(2 * PI * t) + 0.14128 * std::cos(4 * PI * t) - 0.01168 * std::cos(6 * PI * t);
}

double sinc(double x)
{
    if (std::fabs(x) < 1e-9) {
        return 1.0;
    }
    return std::sin(PI * x) / (PI * x);
}

std::vector<float> resampleChannel(const std::vector<float>& x, double ratio /* out/in */)
{
    const size_t inN = x.size();
    const size_t outN = static_cast<size_t>(std::llround(static_cast<double>(inN) * ratio));
    std::vector<float> y(outN);
    if (inN == 0) {
        return y;
    }
    const double fc = std::min(1.0, ratio) * 0.97; // cutoff relative to input Nyquist
    const int zeroCrossings = 32;
    const double half = zeroCrossings / fc; // taps on each side
    // Pre-tabulated kernel for speed (oversampled by 512 per input sample).
    const int os = 512;
    const int tableLen = static_cast<int>(std::ceil(half * os)) + 2;
    std::vector<float> table(static_cast<size_t>(tableLen));
    for (int i = 0; i < tableLen; ++i) {
        const double d = static_cast<double>(i) / os;
        table[static_cast<size_t>(i)] = static_cast<float>(fc * sinc(fc * d) * window(d / half));
    }
    auto kernel = [&](double d) {
        const double p = std::fabs(d) * os;
        const int i = static_cast<int>(p);
        if (i + 1 >= tableLen) {
            return 0.0f;
        }
        const float frac = static_cast<float>(p - i);
        return table[static_cast<size_t>(i)] + (table[static_cast<size_t>(i) + 1] - table[static_cast<size_t>(i)]) * frac;
    };
    const double step = 1.0 / ratio;
    const long long reach = static_cast<long long>(std::ceil(half));
    for (size_t n = 0; n < outN; ++n) {
        const double t = n * step;
        const long long center = static_cast<long long>(std::floor(t));
        const long long k0 = std::max<long long>(0, center - reach + 1);
        const long long k1 = std::min<long long>(static_cast<long long>(inN) - 1, center + reach);
        double acc = 0.0;
        for (long long k = k0; k <= k1; ++k) {
            acc += x[static_cast<size_t>(k)] * kernel(t - static_cast<double>(k));
        }
        y[n] = static_cast<float>(acc);
    }
    return y;
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    float run(float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
};

Biquad makeBiquad(const std::string& kind, double rate, double freq, double q, double gainDb)
{
    const double w0 = 2 * PI * freq / rate;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2 * q);
    const double A = std::pow(10.0, gainDb / 40.0);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    if (kind == "lowpass") {
        b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    } else if (kind == "highpass") {
        b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    } else if (kind == "bandpass") {
        b0 = alpha; b1 = 0; b2 = -alpha;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    } else if (kind == "notch") {
        b0 = 1; b1 = -2 * cw; b2 = 1;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    } else if (kind == "peak") {
        b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
    } else if (kind == "lowshelf" || kind == "highshelf") {
        const double sq = 2 * std::sqrt(A) * alpha;
        if (kind == "lowshelf") {
            b0 = A * ((A + 1) - (A - 1) * cw + sq);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sq);
            a0 = (A + 1) + (A - 1) * cw + sq;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sq;
        } else {
            b0 = A * ((A + 1) + (A - 1) * cw + sq);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sq);
            a0 = (A + 1) - (A - 1) * cw + sq;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sq;
        }
    }
    Biquad f;
    f.b0 = b0 / a0; f.b1 = b1 / a0; f.b2 = b2 / a0; f.a1 = a1 / a0; f.a2 = a2 / a0;
    return f;
}

double curveGain(double t /*0..1*/, const std::string& curve)
{
    t = std::clamp(t, 0.0, 1.0);
    if (curve == "exponential") {
        return t * t * t;
    }
    if (curve == "logarithmic") {
        return std::sqrt(t);
    }
    if (curve == "scurve") {
        return 0.5 - 0.5 * std::cos(PI * t);
    }
    return t; // linear
}

bool needPositive(double v, const char* name, std::string& error)
{
    if (!(v > 0.0) || !std::isfinite(v)) {
        error = std::string(name) + " must be a positive number";
        return false;
    }
    return true;
}

// Envelope-follower dynamics (compressor / limiter / gate share the detector).
void dynamics(Audio& a, double thresholdDb, double ratio, double attackS, double releaseS, double makeupDb, bool gate,
              double floorDb)
{
    const double att = std::exp(-1.0 / (std::max(1e-5, attackS) * a.rate));
    const double rel = std::exp(-1.0 / (std::max(1e-5, releaseS) * a.rate));
    const double makeup = dbToGain(makeupDb);
    double env = 0.0;
    double gainSmooth = 1.0;
    const size_t n = a.frames();
    for (size_t i = 0; i < n; ++i) {
        float lvl = 0.0f;
        for (auto& c : a.ch) {
            lvl = std::max(lvl, std::fabs(c[i]));
        }
        const double coeff = lvl > env ? att : rel;
        env = coeff * env + (1.0 - coeff) * lvl;
        const double envDb = 20.0 * std::log10(std::max(env, 1e-9));
        double gainDb = 0.0;
        if (gate) {
            gainDb = envDb < thresholdDb ? floorDb : 0.0;
        } else if (envDb > thresholdDb) {
            gainDb = (thresholdDb + (envDb - thresholdDb) / ratio) - envDb;
        }
        const double target = dbToGain(gainDb);
        const double gc = target < gainSmooth ? att : rel;
        gainSmooth = gc * gainSmooth + (1.0 - gc) * target;
        const float g = static_cast<float>(gainSmooth * makeup);
        for (auto& c : a.ch) {
            c[i] *= g;
        }
    }
}

} // namespace

const std::vector<EffectInfo>& effectCatalog()
{
    static const std::vector<EffectInfo> list = {
        { "gain", "db", "Change volume by db decibels (negative = quieter)." },
        { "normalize", "peak_db=-1, per_channel=0", "Scale so the loudest sample reaches peak_db dBFS (per_channel=1 normalizes each channel separately)." },
        { "fade_in", "seconds, curve=linear|exponential|logarithmic|scurve", "Fade in from silence over the first seconds." },
        { "fade_out", "seconds, curve", "Fade out to silence over the last seconds." },
        { "trim", "start=0, end=0 (0 = to the end)", "Keep only [start, end] seconds." },
        { "pad", "start=0, end=0", "Add seconds of silence before / after." },
        { "trim_silence", "threshold_db=-50, padding=0.1", "Remove leading and trailing silence quieter than threshold_db, keeping padding seconds." },
        { "reverse", "", "Play backwards." },
        { "speed", "factor", "Change speed and pitch together (2 = twice as fast, one octave up)." },
        { "invert", "", "Invert polarity." },
        { "remove_dc", "", "Remove DC offset." },
        { "highpass", "frequency, q=0.707", "Remove content below frequency Hz (12 dB/oct)." },
        { "lowpass", "frequency, q=0.707", "Remove content above frequency Hz (12 dB/oct)." },
        { "bandpass", "frequency, q=1", "Keep a band around frequency Hz." },
        { "notch", "frequency, q=10", "Cut a narrow band (e.g. 50/60 Hz hum)." },
        { "eq", "frequency, gain_db, q=1", "Peaking equalizer band." },
        { "bass", "gain_db, frequency=100", "Low-shelf boost/cut." },
        { "treble", "gain_db, frequency=8000", "High-shelf boost/cut." },
        { "echo", "delay=0.3, decay=0.4", "Feedback echo: delay seconds, decay 0..0.95." },
        { "compressor", "threshold_db=-20, ratio=4, attack=0.01, release=0.15, makeup_db=0", "Reduce dynamic range." },
        { "limiter", "ceiling_db=-1, release=0.05", "Keep peaks below ceiling_db." },
        { "noise_gate", "threshold_db=-45, attack=0.005, release=0.1, floor_db=-80", "Silence passages below threshold_db." },
        { "mono", "", "Mix all channels down to mono." },
        { "stereo", "", "Duplicate mono to two channels (stereo stays stereo)." },
        { "swap_channels", "", "Swap left and right." },
        { "pan", "value (-1 left .. 1 right)", "Pan a stereo (or mono, made stereo) signal." },
        { "mouth_declick", "sensitivity=6 (1..10), max_click_ms=4, frequency=3000", "Remove mouth clicks, lip smacks and saliva ticks from voice: short impulses are rebuilt by LPC prediction." },
        { "noise_reduction", "reduction_db=12, sensitivity=6, smoothing_bands=6, profile_start, profile_end, auto_percent=10", "Audacity's Noise Reduction. Noise profile from profile_start..profile_end seconds, or automatically from the quietest frames." },
        { "declip", "threshold_db=-0.01 (-6..0)", "Rebuild clipped (saturated) peaks by LPC interpolation. Follow with normalize or limiter." },
        { "click_removal", "threshold=200 (0..900), width=20 (0..40)", "Audacity's Click Removal (vinyl-style clicks; linear interpolation)." },
    };
    return list;
}

Audio remap(const Audio& in, int channels)
{
    if (in.channels() == channels || in.channels() == 0) {
        return in;
    }
    Audio out;
    out.rate = in.rate;
    const size_t n = in.frames();
    if (channels == 1) {
        out.ch.assign(1, std::vector<float>(n, 0.0f));
        const float k = 1.0f / in.channels();
        for (const auto& c : in.ch) {
            for (size_t i = 0; i < n; ++i) {
                out.ch[0][i] += c[i] * k;
            }
        }
        return out;
    }
    out.ch.resize(static_cast<size_t>(channels));
    for (int c = 0; c < channels; ++c) {
        out.ch[static_cast<size_t>(c)] = in.ch[static_cast<size_t>(c % in.channels())];
    }
    return out;
}

Audio resample(const Audio& in, int rate)
{
    if (in.rate == rate || in.rate <= 0) {
        return in;
    }
    Audio out;
    out.rate = rate;
    const double ratio = static_cast<double>(rate) / in.rate;
    for (const auto& c : in.ch) {
        out.ch.push_back(resampleChannel(c, ratio));
    }
    return out;
}

Audio convert(const Audio& in, int rate, int channels)
{
    // Fewer channels first (less work), more channels last.
    if (channels < in.channels()) {
        return resample(remap(in, channels), rate);
    }
    return remap(resample(in, rate), channels);
}

float peak(const Audio& a)
{
    float p = 0.0f;
    for (const auto& c : a.ch) {
        for (float s : c) {
            p = std::max(p, std::fabs(s));
        }
    }
    return p;
}

double rmsDb(const Audio& a)
{
    double sum = 0.0;
    size_t count = 0;
    for (const auto& c : a.ch) {
        for (float s : c) {
            sum += static_cast<double>(s) * s;
        }
        count += c.size();
    }
    if (count == 0 || sum <= 0.0) {
        return -200.0;
    }
    return 10.0 * std::log10(sum / count);
}

bool apply(Audio& a, const Effect& e, std::string& error, std::string* report)
{
    std::string rep;
    const std::string& t = e.type;
    const size_t n = a.frames();
    const double rate = a.rate;

    if (t == "gain") {
        const float g = static_cast<float>(dbToGain(e.get("db", 0.0)));
        for (auto& c : a.ch) {
            for (auto& s : c) {
                s *= g;
            }
        }
    } else if (t == "normalize") {
        const double target = dbToGain(std::min(0.0, e.get("peak_db", -1.0)));
        if (e.get("per_channel", 0.0) != 0.0) {
            for (auto& c : a.ch) {
                float p = 0.0f;
                for (float s : c) {
                    p = std::max(p, std::fabs(s));
                }
                if (p > 1e-9f) {
                    const float g = static_cast<float>(target / p);
                    for (auto& s : c) {
                        s *= g;
                    }
                }
            }
        } else {
            const float p = peak(a);
            if (p > 1e-9f) {
                const float g = static_cast<float>(target / p);
                for (auto& c : a.ch) {
                    for (auto& s : c) {
                        s *= g;
                    }
                }
            }
        }
    } else if (t == "fade_in" || t == "fade_out") {
        const double secs = e.get("seconds", 1.0);
        if (!needPositive(secs, "seconds", error)) {
            return false;
        }
        const size_t len = std::min(n, static_cast<size_t>(secs * rate));
        const std::string curve = e.text("curve", "linear");
        for (auto& c : a.ch) {
            for (size_t i = 0; i < len; ++i) {
                const double g = curveGain(static_cast<double>(i) / std::max<size_t>(1, len), curve);
                if (t == "fade_in") {
                    c[i] *= static_cast<float>(g);
                } else {
                    c[n - 1 - i] *= static_cast<float>(g);
                }
            }
        }
    } else if (t == "trim") {
        const double s = std::max(0.0, e.get("start", 0.0));
        const double en = e.get("end", 0.0);
        const size_t i0 = std::min(n, static_cast<size_t>(std::llround(s * rate)));
        const size_t i1 = en > 0 ? std::min(n, static_cast<size_t>(std::llround(en * rate))) : n;
        if (i1 <= i0) {
            error = "trim: end must be after start and inside the audio";
            return false;
        }
        for (auto& c : a.ch) {
            c = std::vector<float>(c.begin() + static_cast<std::ptrdiff_t>(i0), c.begin() + static_cast<std::ptrdiff_t>(i1));
        }
    } else if (t == "pad") {
        const size_t pre = static_cast<size_t>(std::max(0.0, e.get("start", 0.0)) * rate);
        const size_t post = static_cast<size_t>(std::max(0.0, e.get("end", 0.0)) * rate);
        for (auto& c : a.ch) {
            c.insert(c.begin(), pre, 0.0f);
            c.insert(c.end(), post, 0.0f);
        }
    } else if (t == "trim_silence") {
        const float th = static_cast<float>(dbToGain(e.get("threshold_db", -50.0)));
        const size_t padding = static_cast<size_t>(std::max(0.0, e.get("padding", 0.1)) * rate);
        size_t first = n, last = 0;
        for (size_t i = 0; i < n; ++i) {
            for (const auto& c : a.ch) {
                if (std::fabs(c[i]) > th) {
                    first = std::min(first, i);
                    last = i;
                }
            }
        }
        if (first == n) {
            error = "trim_silence: the whole audio is below threshold_db";
            return false;
        }
        const size_t i0 = first > padding ? first - padding : 0;
        const size_t i1 = std::min(n, last + 1 + padding);
        for (auto& c : a.ch) {
            c = std::vector<float>(c.begin() + static_cast<std::ptrdiff_t>(i0), c.begin() + static_cast<std::ptrdiff_t>(i1));
        }
    } else if (t == "reverse") {
        for (auto& c : a.ch) {
            std::reverse(c.begin(), c.end());
        }
    } else if (t == "speed") {
        const double f = e.get("factor", 1.0);
        if (!needPositive(f, "factor", error) || f < 0.1 || f > 10.0) {
            error = "speed: factor must be between 0.1 and 10";
            return false;
        }
        Audio r = resample(Audio{ static_cast<int>(std::llround(rate * f)), a.ch }, a.rate);
        a.ch = std::move(r.ch);
    } else if (t == "invert") {
        for (auto& c : a.ch) {
            for (auto& s : c) {
                s = -s;
            }
        }
    } else if (t == "remove_dc") {
        for (auto& c : a.ch) {
            const double mean = c.empty() ? 0.0 : std::accumulate(c.begin(), c.end(), 0.0) / c.size();
            for (auto& s : c) {
                s = static_cast<float>(s - mean);
            }
        }
    } else if (t == "highpass" || t == "lowpass" || t == "bandpass" || t == "notch" || t == "eq" || t == "bass"
               || t == "treble") {
        double freq = e.get("frequency", t == "bass" ? 100.0 : t == "treble" ? 8000.0 : 0.0);
        if (!needPositive(freq, "frequency", error)) {
            return false;
        }
        if (freq >= rate / 2) {
            error = "frequency must be below half the sample rate";
            return false;
        }
        const double q = e.get("q", t == "notch" ? 10.0 : (t == "bandpass" || t == "eq") ? 1.0 : 0.707);
        if (!needPositive(q, "q", error)) {
            return false;
        }
        const std::string kind = t == "eq" ? "peak" : t == "bass" ? "lowshelf" : t == "treble" ? "highshelf" : t;
        const double gainDb = e.get("gain_db", 0.0);
        for (auto& c : a.ch) {
            Biquad f = makeBiquad(kind, rate, freq, kind == "lowshelf" || kind == "highshelf" ? 0.707 : q, gainDb);
            for (auto& s : c) {
                s = f.run(s);
            }
        }
    } else if (t == "echo") {
        const double delay = e.get("delay", 0.3);
        const double decay = std::clamp(e.get("decay", 0.4), 0.0, 0.95);
        if (!needPositive(delay, "delay", error)) {
            return false;
        }
        const size_t d = std::max<size_t>(1, static_cast<size_t>(delay * rate));
        for (auto& c : a.ch) {
            for (size_t i = d; i < c.size(); ++i) {
                c[i] += static_cast<float>(decay * c[i - d]);
            }
        }
    } else if (t == "compressor") {
        const double ratio = e.get("ratio", 4.0);
        if (ratio < 1.0) {
            error = "compressor: ratio must be >= 1";
            return false;
        }
        dynamics(a, e.get("threshold_db", -20.0), ratio, e.get("attack", 0.01), e.get("release", 0.15), e.get("makeup_db", 0.0),
                 false, 0.0);
    } else if (t == "limiter") {
        const double ceiling = std::min(0.0, e.get("ceiling_db", -1.0));
        dynamics(a, ceiling, 1000.0, 0.0005, e.get("release", 0.05), 0.0, false, 0.0);
        const float hard = static_cast<float>(dbToGain(ceiling));
        for (auto& c : a.ch) {
            for (auto& s : c) {
                s = std::clamp(s, -hard, hard);
            }
        }
    } else if (t == "noise_gate") {
        dynamics(a, e.get("threshold_db", -45.0), 1.0, e.get("attack", 0.005), e.get("release", 0.1), 0.0, true,
                 std::min(0.0, e.get("floor_db", -80.0)));
    } else if (t == "mono") {
        a = remap(a, 1);
    } else if (t == "stereo") {
        a = remap(a, 2);
    } else if (t == "swap_channels") {
        if (a.channels() >= 2) {
            std::swap(a.ch[0], a.ch[1]);
        }
    } else if (t == "pan") {
        const double v = std::clamp(e.get("value", 0.0), -1.0, 1.0);
        if (a.channels() == 1) {
            a = remap(a, 2);
        }
        // Constant-power balance
        const double angle = (v + 1.0) * PI / 4.0;
        const float gl = static_cast<float>(std::cos(angle) * std::sqrt(2.0));
        const float gr = static_cast<float>(std::sin(angle) * std::sqrt(2.0));
        for (auto& s : a.ch[0]) {
            s *= std::min(1.0f, gl);
        }
        for (auto& s : a.ch[1]) {
            s *= std::min(1.0f, gr);
        }
    } else if (t == "mouth_declick") {
        restore::DeclickParams dp;
        dp.sensitivity = e.get("sensitivity", 6.0);
        dp.maxClickMs = e.get("max_click_ms", 4.0);
        dp.frequency = e.get("frequency", 3000.0);
        if (!restore::mouthDeclick(a, dp, rep, error)) {
            return false;
        }
    } else if (t == "noise_reduction") {
        restore::NoiseReductionParams np;
        np.reductionDb = e.get("reduction_db", 12.0);
        np.sensitivity = e.get("sensitivity", 6.0);
        np.smoothingBands = static_cast<int>(e.get("smoothing_bands", 6.0));
        np.profileStart = e.get("profile_start", -1.0);
        np.profileEnd = e.get("profile_end", -1.0);
        np.autoPercent = e.get("auto_percent", 10.0);
        if (!restore::noiseReduction(a, np, rep, error)) {
            return false;
        }
    } else if (t == "declip") {
        if (!restore::declip(a, e.get("threshold_db", -0.01), rep, error)) {
            return false;
        }
    } else if (t == "click_removal") {
        if (!restore::clickRemoval(a, static_cast<int>(e.get("threshold", 200.0)), static_cast<int>(e.get("width", 20.0)), rep, error)) {
            return false;
        }
    } else {
        std::string names;
        for (const auto& info : effectCatalog()) {
            names += (names.empty() ? "" : ", ") + std::string(info.type);
        }
        error = "Unknown effect \"" + t + "\". Available: " + names;
        return false;
    }
    if (report) {
        *report = rep;
    }
    return true;
}

} // namespace aumcp::dsp
