#include "restore.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <deque>
#include <numeric>
#include <vector>

#include "json.h"

namespace aumcp::restore {
namespace {

constexpr double PI = 3.14159265358979323846;

// ------------------------------------------------------------------ FFT (radix-2, in place)
class Fft
{
public:
    explicit Fft(size_t n) : m_n(n), m_tw(n / 2), m_rev(n)
    {
        for (size_t i = 0; i < n / 2; ++i) {
            m_tw[i] = std::polar(1.0, -2.0 * PI * i / n);
        }
        size_t bits = 0;
        while ((size_t(1) << bits) < n) {
            ++bits;
        }
        for (size_t i = 0; i < n; ++i) {
            size_t r = 0;
            for (size_t b = 0; b < bits; ++b) {
                r |= ((i >> b) & 1) << (bits - 1 - b);
            }
            m_rev[i] = r;
        }
    }
    void run(std::vector<std::complex<double> >& a, bool inverse) const
    {
        for (size_t i = 0; i < m_n; ++i) {
            if (i < m_rev[i]) {
                std::swap(a[i], a[m_rev[i]]);
            }
        }
        for (size_t len = 2; len <= m_n; len <<= 1) {
            const size_t step = m_n / len;
            for (size_t i = 0; i < m_n; i += len) {
                for (size_t j = 0; j < len / 2; ++j) {
                    std::complex<double> w = m_tw[j * step];
                    if (inverse) {
                        w = std::conj(w);
                    }
                    const auto u = a[i + j];
                    const auto v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                }
            }
        }
        if (inverse) {
            for (auto& x : a) {
                x /= static_cast<double>(m_n);
            }
        }
    }

private:
    size_t m_n;
    std::vector<std::complex<double> > m_tw;
    std::vector<size_t> m_rev;
};

// ------------------------------------------------------------------ Noise Reduction (Audacity)
constexpr size_t NR_WINDOW = 2048;      // DEFAULT_WINDOW_SIZE_CHOICE = 8
constexpr size_t NR_STEPS = 4;          // DEFAULT_STEPS_PER_WINDOW_CHOICE = 1
constexpr size_t NR_HOP = NR_WINDOW / NR_STEPS;
constexpr size_t NR_BINS = NR_WINDOW / 2 + 1;

std::vector<double> hann(size_t n)
{
    std::vector<double> w(n);
    for (size_t i = 0; i < n; ++i) {
        w[i] = 0.5 - 0.5 * std::cos(2.0 * PI * i / n);
    }
    return w;
}

struct NrWindow {
    std::vector<std::complex<double> > spec; // full complex spectrum
    std::vector<float> power;                // NR_BINS
    std::vector<float> gains;                // NR_BINS
    long long frame = 0;
};

void powerSpectrum(const std::vector<std::complex<double> >& s, std::vector<float>& p)
{
    p.resize(NR_BINS);
    for (size_t k = 0; k < NR_BINS; ++k) {
        p[k] = static_cast<float>(std::norm(s[k]));
    }
}

void applyFreqSmoothing(std::vector<float>& gains, int bins)
{
    if (bins <= 0) {
        return;
    }
    std::vector<float> logs(NR_BINS), out(NR_BINS, 0.0f);
    for (size_t i = 0; i < NR_BINS; ++i) {
        logs[i] = std::log(gains[i]);
    }
    for (int i = 0; i < static_cast<int>(NR_BINS); ++i) {
        const int j0 = std::max(0, i - bins);
        const int j1 = std::min(static_cast<int>(NR_BINS) - 1, i + bins);
        float acc = 0.0f;
        for (int j = j0; j <= j1; ++j) {
            acc += logs[static_cast<size_t>(j)];
        }
        out[static_cast<size_t>(i)] = acc / (j1 - j0 + 1);
    }
    for (size_t i = 0; i < NR_BINS; ++i) {
        gains[i] = std::exp(out[i]);
    }
}

} // namespace

bool noiseReduction(Audio& audio, const NoiseReductionParams& p, std::string& report, std::string& error)
{
    const size_t n = audio.frames();
    if (n < NR_WINDOW * 2) {
        error = "noise_reduction needs at least 4096 samples";
        return false;
    }
    if (p.reductionDb < 0 || p.reductionDb > 48 || p.sensitivity < 0.01 || p.sensitivity > 24 || p.smoothingBands < 0
        || p.smoothingBands > 12) {
        error = "noise_reduction: reduction_db 0..48, sensitivity 0.01..24, smoothing_bands 0..12";
        return false;
    }
    const auto win = hann(NR_WINDOW);
    double wsum = 0.0;
    for (size_t i = 0; i < NR_WINDOW; i += 1) {
        wsum += win[i] * win[i];
    }
    const double olaNorm = static_cast<double>(NR_HOP) / wsum; // Hann*Hann at 75 % overlap -> 1/1.5
    const Fft fft(NR_WINDOW);

    // ---- 1. Noise profile (mean power per bin), from a given region or the quietest frames
    std::vector<size_t> profileStarts;
    std::string profileKind;
    if (p.profileStart >= 0.0 && p.profileEnd > p.profileStart) {
        const size_t a = static_cast<size_t>(p.profileStart * audio.rate);
        const size_t b = std::min(n, static_cast<size_t>(p.profileEnd * audio.rate));
        for (size_t s = a; s + NR_WINDOW <= b; s += NR_HOP) {
            profileStarts.push_back(s);
        }
        profileKind = "region";
    } else {
        std::vector<std::pair<double, size_t> > energy;
        for (size_t s = 0; s + NR_WINDOW <= n; s += NR_HOP) {
            double e = 0.0;
            for (const auto& c : audio.ch) {
                for (size_t i = 0; i < NR_WINDOW; i += 4) {
                    e += static_cast<double>(c[s + i]) * c[s + i];
                }
            }
            if (e > 1e-12) { // skip digital silence: it says nothing about the noise
                energy.emplace_back(e, s);
            }
        }
        std::sort(energy.begin(), energy.end());
        const size_t take = std::max<size_t>(8, static_cast<size_t>(energy.size() * std::clamp(p.autoPercent, 1.0, 50.0) / 100.0));
        for (size_t i = 0; i < std::min(take, energy.size()); ++i) {
            profileStarts.push_back(energy[i].second);
        }
        profileKind = "automatic (quietest " + json::number(p.autoPercent) + "% of frames)";
    }
    if (profileStarts.size() < 4) {
        error = "noise_reduction: the noise profile is too short (need at least ~0.1 s of noise)";
        return false;
    }
    std::vector<double> means(NR_BINS, 0.0);
    double profileEnergy = 0.0;
    size_t profileCount = 0;
    std::vector<std::complex<double> > buf(NR_WINDOW);
    for (const auto& c : audio.ch) {
        for (size_t s : profileStarts) {
            for (size_t i = 0; i < NR_WINDOW; ++i) {
                buf[i] = c[s + i] * win[i];
                profileEnergy += static_cast<double>(c[s + i]) * c[s + i];
            }
            fft.run(buf, false);
            for (size_t k = 0; k < NR_BINS; ++k) {
                means[k] += std::norm(buf[k]);
            }
            ++profileCount;
        }
    }
    for (auto& m : means) {
        m /= profileCount;
    }
    const double profileDb = 10.0 * std::log10(profileEnergy / (profileCount * NR_WINDOW) + 1e-20);

    // ---- 2. Reduction (Audacity Worker::ReduceNoise, Hann/Hann, second-greatest discrimination)
    const double sensitivity = p.sensitivity * std::log(10.0);
    const float atten = static_cast<float>(std::pow(10.0, -p.reductionDb / 20.0));
    const unsigned attackBlocks = 1 + static_cast<unsigned>(0.02 * audio.rate / NR_HOP);
    const unsigned releaseBlocks = 1 + static_cast<unsigned>(0.1 * audio.rate / NR_HOP);
    const float oneAttack = static_cast<float>(std::pow(10.0, -p.reductionDb / attackBlocks / 20.0));
    const float oneRelease = static_cast<float>(std::pow(10.0, -p.reductionDb / releaseBlocks / 20.0));
    const unsigned nExamine = 1 + NR_STEPS;
    const unsigned center = nExamine / 2;
    const size_t historyLen = std::max<size_t>(nExamine, center + attackBlocks);

    // Padded timeline: NR_WINDOW zeros before and after, like Audacity's leading/trailing padding.
    const long long pad = static_cast<long long>(NR_WINDOW);
    const long long total = static_cast<long long>(n) + 2 * pad;
    const long long frames = (total - static_cast<long long>(NR_WINDOW)) / static_cast<long long>(NR_HOP) + 1;
    long long noiseBins = 0, totalBins = 0;

    for (auto& c : audio.ch) {
        std::vector<double> out(static_cast<size_t>(total), 0.0);
        std::deque<NrWindow> q; // front = newest
        auto sampleAt = [&](long long t) -> double {
            const long long i = t - pad;
            return (i >= 0 && i < static_cast<long long>(n)) ? c[static_cast<size_t>(i)] : 0.0;
        };
        auto emitOldest = [&]() {
            NrWindow& w = q.back();
            applyFreqSmoothing(w.gains, p.smoothingBands);
            for (size_t k = 0; k < NR_BINS; ++k) {
                w.spec[k] *= w.gains[k];
                if (k > 0 && k < NR_WINDOW / 2) {
                    w.spec[NR_WINDOW - k] = std::conj(w.spec[k]);
                }
            }
            fft.run(w.spec, true);
            const long long start = w.frame * static_cast<long long>(NR_HOP);
            for (size_t i = 0; i < NR_WINDOW; ++i) {
                out[static_cast<size_t>(start) + i] += w.spec[i].real() * win[i] * olaNorm;
            }
            q.pop_back();
        };
        for (long long f = 0; f < frames + static_cast<long long>(historyLen); ++f) {
            NrWindow w;
            w.frame = f;
            w.spec.assign(NR_WINDOW, {});
            if (f < frames) {
                for (size_t i = 0; i < NR_WINDOW; ++i) {
                    w.spec[i] = sampleAt(f * static_cast<long long>(NR_HOP) + static_cast<long long>(i)) * win[i];
                }
                fft.run(w.spec, false);
            }
            powerSpectrum(w.spec, w.power);
            w.gains.assign(NR_BINS, atten);
            q.push_front(std::move(w));

            const size_t hist = q.size();
            const unsigned nWin = static_cast<unsigned>(std::min<size_t>(nExamine, hist));
            if (nWin > center) {
                auto& g = q[center].gains;
                for (size_t k = 0; k < NR_BINS; ++k) {
                    float greatest = 0.0f, second = 0.0f;
                    for (unsigned i = 0; i < nWin; ++i) {
                        const float pw = q[i].power[k];
                        if (pw >= greatest) {
                            second = greatest;
                            greatest = pw;
                        } else if (pw >= second) {
                            second = pw;
                        }
                    }
                    const bool isNoise = second <= sensitivity * means[k];
                    if (!isNoise) {
                        g[k] = 1.0f;
                    }
                    noiseBins += isNoise ? 1 : 0;
                    ++totalBins;
                }
                // Attack (backwards in time = older windows)
                for (size_t k = 0; k < NR_BINS; ++k) {
                    for (size_t i = center + 1; i < hist; ++i) {
                        const float minimum = std::max(atten, q[i - 1].gains[k] * oneAttack);
                        float& gain = q[i].gains[k];
                        if (gain < minimum) {
                            gain = minimum;
                        } else {
                            break;
                        }
                    }
                }
                // Release (one window ahead)
                for (size_t k = 0; k < NR_BINS; ++k) {
                    float& next = q[center - 1].gains[k];
                    next = std::max(next, std::max(atten, q[center].gains[k] * oneRelease));
                }
            }
            if (q.size() >= historyLen) {
                if (q.back().frame < frames) {
                    emitOldest();
                } else {
                    q.pop_back();
                }
            }
        }
        for (size_t i = 0; i < n; ++i) {
            c[i] = static_cast<float>(out[i + static_cast<size_t>(pad)]);
        }
    }
    report = json::Obj()
             .add("profile", profileKind)
             .add("profile_seconds", static_cast<double>(profileStarts.size() * NR_HOP) / audio.rate)
             .add("profile_level_dbfs", profileDb)
             .add("noise_bins_percent", totalBins ? 100.0 * noiseBins / totalBins : 0.0)
             .str();
    return true;
}

// ------------------------------------------------------------------ Click Removal (Audacity)
namespace {

bool removeClicksWindow(std::vector<float>& buffer, int threshold, int width, int& sep, int& clicks)
{
    const size_t len = buffer.size();
    bool did = false;
    int left = 0;
    const int s2 = sep / 2;
    std::vector<float> ms(len), b2(len);
    for (size_t i = 0; i < len; ++i) {
        b2[i] = buffer[i] * buffer[i];
        ms[i] = b2[i];
    }
    size_t i;
    for (i = 1; static_cast<int>(i) < sep; i *= 2) {
        for (size_t j = 0; j < len - i; ++j) {
            ms[j] += ms[j + i];
        }
    }
    sep = static_cast<int>(i); // Audacity truncates sep to the next power of two (member state)
    for (i = 0; i < len - sep; ++i) {
        ms[i] /= sep;
    }
    for (int wrc = width / 4; wrc >= 1; wrc /= 2) {
        const int ww = width / wrc;
        for (i = 0; i < len - sep; ++i) {
            float msw = 0;
            for (int j = 0; j < ww; ++j) {
                msw += b2[i + s2 + j];
            }
            msw /= ww;
            if (msw >= threshold * ms[i] / 10) {
                if (left == 0) {
                    left = static_cast<int>(i) + s2;
                }
            } else {
                if (left != 0 && (static_cast<int>(i) - left + s2) <= ww * 2) {
                    const float lv = buffer[static_cast<size_t>(left)];
                    const float rv = buffer[i + ww + s2];
                    for (size_t j = static_cast<size_t>(left); j < i + ww + s2; ++j) {
                        did = true;
                        buffer[j] = (rv * (j - left) + lv * (i + ww + s2 - j)) / static_cast<float>(i + ww + s2 - left);
                        b2[j] = buffer[j] * buffer[j];
                    }
                    ++clicks;
                    left = 0;
                } else if (left != 0) {
                    left = 0;
                }
            }
        }
    }
    return did;
}

} // namespace

bool clickRemoval(Audio& audio, int threshold, int width, std::string& report, std::string& error)
{
    constexpr size_t windowSize = 8192;
    if (threshold < 0 || threshold > 900 || width < 0 || width > 40) {
        error = "click_removal: threshold 0..900, width 0..40";
        return false;
    }
    const size_t n = audio.frames();
    if (n <= windowSize / 2) {
        error = "click_removal: selection must be larger than 4096 samples";
        return false;
    }
    int clicks = 0;
    for (auto& c : audio.ch) {
        int sep = 2049;
        std::vector<float> w(windowSize);
        for (size_t i = 0; i + windowSize / 2 < n; i += windowSize / 2) {
            const size_t wcopy = std::min(windowSize, n - i);
            std::fill(w.begin(), w.end(), 0.0f);
            std::copy_n(c.begin() + static_cast<std::ptrdiff_t>(i), wcopy, w.begin());
            removeClicksWindow(w, threshold, width, sep, clicks);
            std::copy_n(w.begin(), wcopy, c.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    report = json::Obj().add("clicks_repaired", clicks).add("per_minute", clicks / std::max(1e-9, audio.seconds() / 60.0)).str();
    return true;
}

// ------------------------------------------------------------------ Mouth de-click (LPC)
namespace {

struct Biquad {
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    double run(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

Biquad highpass(double rate, double f, double q)
{
    const double w0 = 2 * PI * f / rate, cw = std::cos(w0), alpha = std::sin(w0) / (2 * q);
    const double a0 = 1 + alpha;
    return { (1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0, -2 * cw / a0, (1 - alpha) / a0 };
}

// Burg's method: returns a[0..p] (a[0] = 1), prediction x[n] = -sum_{k=1..p} a[k] x[n-k]
std::vector<double> burg(const double* x, size_t m, size_t p)
{
    std::vector<double> a(p + 1, 0.0);
    a[0] = 1.0;
    std::vector<double> f(x, x + m), b(x, x + m);
    double den = 0.0;
    for (size_t i = 0; i < m; ++i) {
        den += 2.0 * x[i] * x[i];
    }
    for (size_t k = 1; k <= p && k < m; ++k) {
        double num = 0.0;
        den = 0.0;
        for (size_t n = k; n < m; ++n) {
            num += f[n] * b[n - 1];
            den += f[n] * f[n] + b[n - 1] * b[n - 1];
        }
        if (den <= 1e-30) {
            break;
        }
        const double kk = -2.0 * num / den;
        std::vector<double> prev = a;
        for (size_t i = 1; i <= k; ++i) {
            a[i] = prev[i] + kk * prev[k - i];
        }
        for (size_t n = m - 1; n >= k; --n) {
            const double fn = f[n];
            f[n] = fn + kk * b[n - 1];
            b[n] = b[n - 1] + kk * fn;
            if (n == k) {
                break;
            }
        }
    }
    return a;
}

} // namespace

namespace {

// Least-squares AR interpolation (Godsill & Rayner): the missing samples c[a, b) are the values that
// minimise the AR prediction error over the gap and p samples after it, given the known samples
// around. The AR model (Burg) is fitted on the context at both sides of the gap.
// Returns false when it cannot be solved (then a straight line is used).
bool repairSpan(std::vector<float>& c, size_t a, size_t b, size_t maxContext)
{
    const size_t n = c.size();
    const size_t len = b - a;
    const size_t lc = std::min(maxContext, a), rc = std::min(maxContext, n - b);
    const size_t p = std::min<size_t>(32, std::min(lc, rc) / 4);
    auto line = [&]() {
        const double l = a > 0 ? c[a - 1] : 0.0, r = b < n ? c[b] : 0.0;
        for (size_t t = 0; t < len; ++t) {
            c[a + t] = static_cast<float>(l + (r - l) * (t + 1) / (len + 1));
        }
        return false;
    };
    if (p < 4 || len == 0 || a < p || b + p > n) {
        return line();
    }
    // AR model from both contexts (gap excluded).
    std::vector<double> ctx;
    ctx.reserve(lc + rc);
    ctx.insert(ctx.end(), c.begin() + static_cast<std::ptrdiff_t>(a - lc), c.begin() + static_cast<std::ptrdiff_t>(a));
    ctx.insert(ctx.end(), c.begin() + static_cast<std::ptrdiff_t>(b), c.begin() + static_cast<std::ptrdiff_t>(b + rc));
    const auto ar = burg(ctx.data(), ctx.size(), p);
    const size_t q = ar.size() - 1;
    // Window [a - q, b + q): unknowns at offsets q .. q+len-1.
    const size_t w0 = a - q;
    const size_t W = len + 2 * q;
    std::vector<double> xw(W);
    for (size_t i = 0; i < W; ++i) {
        xw[i] = c[w0 + i];
    }
    // Prediction error rows for times t = a .. b+q-1 (row r -> time a + r), e = sum_k ar[k] x[t-k].
    // Normal equations: (Au^T Au) xu = -Au^T (Ak xk)
    const size_t rows = len + q;
    std::vector<double> M(len * len, 0.0), rhs(len, 0.0);
    std::vector<double> known(rows, 0.0);
    for (size_t r = 0; r < rows; ++r) {
        const size_t t = q + r; // index in window
        for (size_t k = 0; k <= q; ++k) {
            const size_t col = t - k;
            if (col < q || col >= q + len) {
                known[r] += ar[k] * xw[col];
            }
        }
    }
    for (size_t r = 0; r < rows; ++r) {
        const size_t t = q + r;
        // unknown columns touched by this row
        for (size_t k1 = 0; k1 <= q; ++k1) {
            const size_t c1 = t - k1;
            if (c1 < q || c1 >= q + len) {
                continue;
            }
            const size_t u1 = c1 - q;
            rhs[u1] -= ar[k1] * known[r];
            for (size_t k2 = 0; k2 <= q; ++k2) {
                const size_t c2 = t - k2;
                if (c2 < q || c2 >= q + len) {
                    continue;
                }
                M[u1 * len + (c2 - q)] += ar[k1] * ar[k2];
            }
        }
    }
    // Cholesky solve (M is symmetric positive definite)
    for (size_t i = 0; i < len; ++i) {
        M[i * len + i] += 1e-9;
    }
    for (size_t j = 0; j < len; ++j) {
        double d = M[j * len + j];
        for (size_t k = 0; k < j; ++k) {
            d -= M[j * len + k] * M[j * len + k];
        }
        if (d <= 0.0) {
            return line();
        }
        d = std::sqrt(d);
        M[j * len + j] = d;
        for (size_t i = j + 1; i < len; ++i) {
            double s = M[i * len + j];
            for (size_t k = 0; k < j; ++k) {
                s -= M[i * len + k] * M[j * len + k];
            }
            M[i * len + j] = s / d;
        }
    }
    std::vector<double> y(len);
    for (size_t i = 0; i < len; ++i) {
        double s = rhs[i];
        for (size_t k = 0; k < i; ++k) {
            s -= M[i * len + k] * y[k];
        }
        y[i] = s / M[i * len + i];
    }
    for (size_t i = len; i-- > 0;) {
        double s = y[i];
        for (size_t k = i + 1; k < len; ++k) {
            s -= M[k * len + i] * y[k];
        }
        y[i] = s / M[i * len + i];
    }
    double ctxPeak = 0.0;
    for (double v : ctx) {
        ctxPeak = std::max(ctxPeak, std::fabs(v));
    }
    for (double v : y) {
        if (!std::isfinite(v) || std::fabs(v) > 2.0 * ctxPeak + 1e-6) {
            return line();
        }
    }
    for (size_t t = 0; t < len; ++t) {
        c[a + t] = static_cast<float>(y[t]);
    }
    return true;
}

// Zero-phase 4th-order Butterworth low-pass (forward + backward 2nd-order passes).
std::vector<float> zeroPhaseLowpass(const std::vector<float>& x, double rate, double f)
{
    const double w0 = 2 * PI * f / rate, cw = std::cos(w0), alpha = std::sin(w0) / (2 * 0.70710678);
    const double a0 = 1 + alpha;
    const double b0 = (1 - cw) / 2 / a0, b1 = (1 - cw) / a0, b2 = b0, a1 = -2 * cw / a0, a2 = (1 - alpha) / a0;
    std::vector<double> y(x.begin(), x.end());
    auto pass = [&](bool reverse) {
        double z1 = 0, z2 = 0;
        const size_t n = y.size();
        for (size_t k = 0; k < n; ++k) {
            double& v = y[reverse ? n - 1 - k : k];
            const double out = b0 * v + z1;
            z1 = b1 * v - a1 * out + z2;
            z2 = b2 * v - a2 * out;
            v = out;
        }
    };
    pass(false);
    pass(true);
    std::vector<float> out(y.size());
    for (size_t i = 0; i < y.size(); ++i) {
        out[i] = static_cast<float>(y[i]);
    }
    return out;
}

// Repairs only the high band of c[a, b): the low band (voice body, below `crossover`) is kept as
// recorded and the high band, where a mouth click lives, is rebuilt by LSAR interpolation.
bool repairHighBand(std::vector<float>& c, size_t a, size_t b, double rate, double crossover, size_t ctx)
{
    const size_t n = c.size();
    const size_t pad = ctx + 512;
    const size_t w0 = a > pad ? a - pad : 0;
    const size_t w1 = std::min(n, b + pad);
    std::vector<float> win(c.begin() + static_cast<std::ptrdiff_t>(w0), c.begin() + static_cast<std::ptrdiff_t>(w1));
    const auto low = zeroPhaseLowpass(win, rate, crossover);
    std::vector<float> high(win.size());
    for (size_t i = 0; i < win.size(); ++i) {
        high[i] = win[i] - low[i];
    }
    const bool ok = repairSpan(high, a - w0, b - w0, ctx);
    for (size_t t = a; t < b; ++t) {
        c[t] = low[t - w0] + high[t - w0];
    }
    return ok;
}

double meanAbs(const std::vector<double>& prefix, size_t a, size_t b)
{
    return b > a ? (prefix[b] - prefix[a]) / static_cast<double>(b - a) : 0.0;
}

} // namespace

bool mouthDeclick(Audio& audio, const DeclickParams& p, std::string& report, std::string& error)
{
    const size_t n = audio.frames();
    const double rate = audio.rate;
    if (p.sensitivity < 1 || p.sensitivity > 10 || p.maxClickMs < 0.2 || p.maxClickMs > 20 || p.frequency < 500
        || p.frequency >= rate / 2) {
        error = "mouth_declick: sensitivity 1..10, max_click_ms 0.2..20, frequency 500..Nyquist";
        return false;
    }
    if (n < 4096) {
        return true;
    }
    // Detection signal: 4th-order Butterworth high-pass of the channel mix.
    Biquad h1 = highpass(rate, p.frequency, 0.54119610), h2 = highpass(rate, p.frequency, 1.30656296);
    std::vector<float> e(n);
    for (size_t i = 0; i < n; ++i) {
        double m = 0.0;
        for (const auto& c : audio.ch) {
            m += c[i];
        }
        e[i] = static_cast<float>(std::fabs(h2.run(h1.run(m / audio.channels()))));
    }
    std::vector<double> prefix(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) {
        prefix[i + 1] = prefix[i] + e[i];
    }
    const auto ms = [&](double v) { return static_cast<size_t>(v / 1000.0 * rate); };
    // Detection on the AR prediction residual of the channel mix (Godsill & Rayner): an AR model
    // whitens the voice, so a click stands out even inside loud speech. Threshold: k times a
    // robust (median-based) estimate of the residual level in each block.
    std::vector<double> mono(n);
    for (size_t i = 0; i < n; ++i) {
        double m = 0.0;
        for (const auto& c : audio.ch) {
            m += c[i];
        }
        mono[i] = m / audio.channels();
    }
    constexpr size_t ORDER = 24;
    constexpr size_t BLOCK = 1024;
    std::vector<float> res(n, 0.0f);
    {
        std::vector<double> seg, w, ac(ORDER + 1), a(ORDER + 1), tmp(ORDER + 1);
        for (size_t s = ORDER; s + BLOCK <= n; s += BLOCK) {
            const size_t s0 = s - ORDER;
            const size_t len = BLOCK + ORDER;
            w.assign(len, 0.0);
            for (size_t i = 0; i < len; ++i) {
                w[i] = mono[s0 + i] * (0.5 - 0.5 * std::cos(2.0 * PI * (i + 0.5) / len));
            }
            for (size_t lag = 0; lag <= ORDER; ++lag) {
                double acc = 0.0;
                for (size_t i = lag; i < len; ++i) {
                    acc += w[i] * w[i - lag];
                }
                ac[lag] = acc;
            }
            if (ac[0] < 1e-12) {
                continue;
            }
            ac[0] *= 1.0001; // white-noise correction keeps Levinson stable
            // Levinson-Durbin
            std::fill(a.begin(), a.end(), 0.0);
            a[0] = 1.0;
            double err = ac[0];
            for (size_t i = 1; i <= ORDER; ++i) {
                double acc = ac[i];
                for (size_t j = 1; j < i; ++j) {
                    acc += a[j] * ac[i - j];
                }
                const double kk = -acc / err;
                tmp = a;
                for (size_t j = 1; j < i; ++j) {
                    a[j] = tmp[j] + kk * tmp[i - j];
                }
                a[i] = kk;
                err *= (1.0 - kk * kk);
                if (err <= 0.0) {
                    break;
                }
            }
            for (size_t t = s; t < s + BLOCK; ++t) {
                double r = mono[t];
                for (size_t j = 1; j <= ORDER; ++j) {
                    r += a[j] * mono[t - j];
                }
                res[t] = static_cast<float>(std::fabs(r));
            }
        }
    }
    const double k = std::max(6.0, 30.0 - 2.0 * p.sensitivity); // sensitivity 6 -> 18
    const float floorLevel = 1e-4f;                              // ~ -80 dBFS: ignore the noise floor
    std::vector<size_t> hits;
    {
        std::vector<float> blk;
        constexpr size_t SIGMA_BLOCK = 2048;
        for (size_t s = 0; s < n; s += SIGMA_BLOCK) {
            const size_t e2 = std::min(n, s + SIGMA_BLOCK);
            blk.assign(res.begin() + static_cast<std::ptrdiff_t>(s), res.begin() + static_cast<std::ptrdiff_t>(e2));
            std::nth_element(blk.begin(), blk.begin() + static_cast<std::ptrdiff_t>(blk.size() / 2), blk.end());
            const double sigma = 1.4826 * blk[blk.size() / 2] + 1e-7;
            for (size_t i = s; i < e2; ++i) {
                if (res[i] > k * sigma && std::fabs(mono[i]) > floorLevel) {
                    hits.push_back(i);
                }
            }
        }
    }
    struct Event {
        size_t first, last, peakAt;
        float peak;
    };
    std::vector<Event> events;
    const size_t mergeGap = ms(0.3);
    for (size_t i = 0; i < hits.size();) {
        size_t j = i;
        while (j + 1 < hits.size() && hits[j + 1] - hits[j] <= mergeGap) {
            ++j;
        }
        Event ev{ hits[i], hits[j], hits[i], 0.0f };
        for (size_t t = hits[i]; t <= hits[j]; ++t) {
            if (res[t] > ev.peak) {
                ev.peak = res[t];
                ev.peakAt = t;
            }
        }
        events.push_back(ev);
        i = j + 1;
    }
    const size_t maxLen = ms(p.maxClickMs);
    const size_t margin = ms(0.5) + 2;
    const size_t guard = ms(8.0);
    size_t tooLong = 0, plosive = 0, periodic = 0;
    std::vector<std::pair<size_t, size_t> > regions;
    for (size_t idx = 0; idx < events.size(); ++idx) {
        const Event& ev = events[idx];
        if (ev.last - ev.first + 1 > maxLen) {
            ++tooLong;
            continue;
        }
        // Consonant burst (t, k, p, ch): the high band stays loud right after the transient,
        // while it was quiet just before. A mouth click decays at once.
        const size_t a = ev.first > margin ? ev.first - margin : 0;
        const double pre = meanAbs(prefix, a > guard ? a - guard : 0, a);
        // Cover the click's decaying tail: extend while the high band stays well above its level
        // just before the click (0.5 ms averages), up to max_click_ms.
        size_t tail = ev.last + 1;
        const size_t limit = std::min(n, ev.first + maxLen);
        const size_t half = std::max<size_t>(1, ms(0.5));
        while (tail < limit && meanAbs(prefix, tail, std::min(n, tail + half)) > 2.5 * pre + floorLevel) {
            tail += half / 2 + 1;
        }
        const size_t b = std::min(n, std::min(tail, limit) + margin);
        const double post = meanAbs(prefix, b, std::min(n, b + guard));
        float hfPeak = 0.0f;
        for (size_t t = ev.first; t <= ev.last; ++t) {
            hfPeak = std::max(hfPeak, e[t]);
        }
        if (post > 3.0 * pre && post > hfPeak / 6.0) {
            ++plosive;
            continue;
        }
        // Glottal pulses of voiced speech: similar transients one pitch period apart
        // (2.5..12.5 ms) on both sides. Mouth clicks are isolated.
        bool before = false, after = false;
        for (size_t j = idx; j-- > 0;) {
            const double dt = static_cast<double>(ev.peakAt - events[j].peakAt) / rate * 1000.0;
            if (dt > 12.5) {
                break;
            }
            if (dt >= 2.5 && events[j].peak > 0.4f * ev.peak && events[j].peak < 2.5f * ev.peak) {
                before = true;
                break;
            }
        }
        for (size_t j = idx + 1; j < events.size(); ++j) {
            const double dt = static_cast<double>(events[j].peakAt - ev.peakAt) / rate * 1000.0;
            if (dt > 12.5) {
                break;
            }
            if (dt >= 2.5 && events[j].peak > 0.4f * ev.peak && events[j].peak < 2.5f * ev.peak) {
                after = true;
                break;
            }
        }
        if (before && after) {
            ++periodic;
            continue;
        }
        if (!regions.empty() && a <= regions.back().second + mergeGap) {
            regions.back().second = b;
        } else {
            regions.emplace_back(a, b);
        }
    }
    // A mouth click is high-frequency energy. If a repair would mostly change the low band, the
    // region was voice (a glottal pulse or a consonant), not a click: keep the original there.
    const size_t lpLen = std::max<size_t>(3, static_cast<size_t>(rate / 2000.0)); // ~2 kHz moving average
    const double crossover = std::clamp(p.frequency / 2.0, 500.0, rate / 4.0);
    size_t repaired = 0, fallback = 0, rejected = 0;
    for (const auto& [a, b] : regions) {
        const size_t ctx = std::clamp<size_t>((b - a) * 8, 256, 2048);
        if (a < 64 || b + 64 > n) {
            continue;
        }
        std::vector<std::vector<float> > before;
        for (auto& c : audio.ch) {
            before.emplace_back(c.begin() + static_cast<std::ptrdiff_t>(a), c.begin() + static_cast<std::ptrdiff_t>(b));
        }
        bool voiceLike = false;
        size_t fb = 0;
        for (size_t ci = 0; ci < audio.ch.size(); ++ci) {
            auto& c = audio.ch[ci];
            if (!repairHighBand(c, a, b, rate, crossover, ctx)) {
                ++fb;
            }
            double all = 0.0, low = 0.0, acc = 0.0;
            std::vector<double> d(b - a);
            for (size_t t = a; t < b; ++t) {
                d[t - a] = static_cast<double>(c[t]) - before[ci][t - a];
                all += d[t - a] * d[t - a];
            }
            for (size_t t = 0; t < d.size(); ++t) {
                acc += d[t];
                if (t >= lpLen) {
                    acc -= d[t - lpLen];
                }
                const double lp = acc / static_cast<double>(std::min(t + 1, lpLen));
                low += lp * lp;
            }
            if (all > 0.0 && low > 0.5 * all) {
                voiceLike = true;
            }
        }
        if (voiceLike) {
            for (size_t ci = 0; ci < audio.ch.size(); ++ci) {
                std::copy(before[ci].begin(), before[ci].end(), audio.ch[ci].begin() + static_cast<std::ptrdiff_t>(a));
            }
            ++rejected;
            continue;
        }
        fallback += fb;
        ++repaired;
    }
    const double minutes = std::max(1e-9, audio.seconds() / 60.0);
    report = json::Obj()
             .add("clicks_repaired", static_cast<long long>(repaired))
             .add("per_minute", repaired / minutes)
             .add("skipped_consonant_bursts", static_cast<long long>(plosive))
             .add("skipped_voice_pulses", static_cast<long long>(periodic))
             .add("skipped_long_events", static_cast<long long>(tooLong))
             .add("skipped_voice_like_repairs", static_cast<long long>(rejected))
             .add("linear_fallbacks", static_cast<long long>(fallback))
             .str();
    return true;
}

bool declip(Audio& audio, double thresholdDb, std::string& report, std::string& error)
{
    if (thresholdDb > 0.0 || thresholdDb < -6.0) {
        error = "declip: threshold_db must be between -6 and 0";
        return false;
    }
    const float level = static_cast<float>(std::pow(10.0, thresholdDb / 20.0));
    const size_t n = audio.frames();
    size_t runs = 0, samples = 0;
    float maxRebuilt = 0.0f;
    for (auto& c : audio.ch) {
        for (size_t i = 0; i < n;) {
            if (std::fabs(c[i]) < level) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < n && std::fabs(c[j]) >= level && (c[j] > 0) == (c[i] > 0)) {
                ++j;
            }
            const size_t a = i > 1 ? i - 1 : i, b = std::min(n, j + 1);
            const std::vector<float> orig(c.begin() + static_cast<std::ptrdiff_t>(a), c.begin() + static_cast<std::ptrdiff_t>(b));
            if (a >= 64 && b + 64 <= n && j - i <= 400) {
                repairSpan(c, a, b, 1024);
                // The true waveform was at least as loud as the clipped plateau, with the same sign.
                for (size_t t = a; t < b; ++t) {
                    const float o = orig[t - a];
                    if (std::fabs(o) >= level && (std::fabs(c[t]) < std::fabs(o) || (c[t] > 0) != (o > 0))) {
                        c[t] = o;
                    }
                    maxRebuilt = std::max(maxRebuilt, std::fabs(c[t]));
                }
                ++runs;
                samples += j - i;
            }
            i = j;
        }
    }
    report = json::Obj()
             .add("clipped_runs_rebuilt", static_cast<long long>(runs))
             .add("clipped_samples", static_cast<long long>(samples))
             .add("rebuilt_peak", static_cast<double>(maxRebuilt))
             .add("hint", maxRebuilt > 1.0f ? "Rebuilt peaks exceed 0 dBFS: follow with normalize or limiter." : "")
             .str();
    return true;
}

} // namespace aumcp::restore
