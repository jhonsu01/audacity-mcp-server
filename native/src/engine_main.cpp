// aumcp-engine.exe: headless audio engine used by the MCP server.
// Reads one JSON job from stdin, writes one JSON result to stdout (UTF-8). Exit code 0 = success.
#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>

#include "audio.h"
#include "dsp.h"
#include "json.h"

using namespace aumcp;
namespace fs = std::filesystem;
using json::Value;

namespace {

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const std::string& msg)
{
    throw Failure(msg);
}

std::wstring needPath(const Value& job, const char* key)
{
    const std::string p = job.str(key);
    if (p.empty()) {
        fail(std::string("Missing \"") + key + "\"");
    }
    const std::wstring w = fromUtf8(p);
    if (!fs::path(w).is_absolute()) {
        fail(std::string("\"") + key + "\" must be an absolute path: " + p);
    }
    return w;
}

std::wstring needInput(const Value& job, const char* key = "input")
{
    const std::wstring p = needPath(job, key);
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) {
        fail("Input file not found: " + toUtf8(p));
    }
    return p;
}

OutputSpec outputSpec(const Value& job)
{
    OutputSpec spec;
    if (const Value* o = job.find("output"); o && o->type == Value::Type::Object) {
        spec.format = o->str("format");
        spec.sample = o->str("sample_format", "auto");
        spec.rate = static_cast<int>(o->num("sample_rate", 0));
        spec.channels = static_cast<int>(o->num("channels", 0));
        spec.quality = o->num("quality", -1.0);
        spec.compression = o->num("compression", -1.0);
    }
    return spec;
}

double dbfs(float p)
{
    return p > 0.0f ? 20.0 * std::log10(p) : -200.0;
}

std::string infoJson(const SourceInfo& i)
{
    return json::Obj()
           .add("container", i.container)
           .add("sample_format", i.sample)
           .add("sample_rate", i.rate)
           .add("channels", i.channels)
           .add("frames", static_cast<long long>(i.frames))
           .add("duration_seconds", i.seconds())
           .str();
}

std::string outputJson(const ResolvedOutput& o)
{
    return json::Obj()
           .add("format", o.format)
           .add("sample_format", o.sample)
           .add("sample_rate", o.rate)
           .add("channels", o.channels)
           .str();
}

void ensureWritable(const std::wstring& path, bool overwrite)
{
    std::error_code ec;
    if (fs::exists(path, ec) && !overwrite) {
        fail("File already exists (set overwrite: true to replace it): " + toUtf8(path));
    }
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            fail("Cannot create folder " + toUtf8(parent.wstring()) + ": " + ec.message());
        }
    }
}

// Output format for an explicit output path: the "format" option, else the extension.
ResolvedOutput resolveFor(const std::wstring& outPath, OutputSpec spec, const SourceInfo& src)
{
    const std::string fromExt = formatFromExtension(outPath);
    if (spec.format.empty()) {
        if (fromExt.empty()) {
            fail("Cannot tell the output format from \"" + toUtf8(outPath) + "\": give output.format or a known extension");
        }
        spec.format = fromExt;
    }
    ResolvedOutput out;
    std::string err;
    if (!resolveOutput(spec, src, out, err)) {
        fail(err);
    }
    if (!fromExt.empty() && fromExt != out.format && !(out.format == "rf64" && fromExt == "wav")) {
        fail("output_path extension does not match format \"" + out.format + "\"");
    }
    return out;
}

std::string fileResult(const std::wstring& path, const Audio& a, const ResolvedOutput& o)
{
    std::error_code ec;
    const auto bytes = fs::file_size(path, ec);
    return json::Obj()
           .add("path", toUtf8(path))
           .add("bytes", static_cast<long long>(ec ? 0 : bytes))
           .add("duration_seconds", a.seconds())
           .raw("output", outputJson(o))
           .str();
}

std::vector<dsp::Effect> parseEffects(const Value& job)
{
    std::vector<dsp::Effect> list;
    const Value* arr = job.find("effects");
    if (!arr) {
        return list;
    }
    if (arr->type != Value::Type::Array) {
        fail("\"effects\" must be an array");
    }
    for (const Value& v : arr->arr) {
        if (v.type != Value::Type::Object) {
            fail("Each effect must be an object like {\"type\":\"gain\",\"db\":-3}");
        }
        dsp::Effect e;
        for (const auto& kv : v.obj) {
            if (kv.first == "type") {
                e.type = kv.second.s;
            } else if (kv.second.type == Value::Type::Number) {
                e.num[kv.first] = kv.second.n;
            } else if (kv.second.type == Value::Type::Bool) {
                e.num[kv.first] = kv.second.b ? 1.0 : 0.0;
            } else if (kv.second.type == Value::Type::String) {
                e.str[kv.first] = kv.second.s;
            }
        }
        if (e.type.empty()) {
            fail("Effect without \"type\"");
        }
        list.push_back(std::move(e));
    }
    return list;
}

// ------------------------------------------------------------------ commands
std::string cmdProbe()
{
    Sndfile& sf = sndfile();
    Mpg123& mp = mpg123();
    json::Obj formats;
    for (const auto& f : outputFormats()) {
        json::Arr a;
        for (const auto& s : sampleFormats(f)) {
            a.add(s);
        }
        formats.raw(f, a.str());
    }
    json::Arr effects;
    for (const auto& e : dsp::effectCatalog()) {
        effects.raw(json::Obj().add("type", e.type).add("params", e.params).add("description", e.description).str());
    }
    json::Arr inputs;
    for (const char* e : { "wav", "aif", "aiff", "flac", "ogg", "oga", "opus", "mp3", "mp2", "caf", "w64", "rf64", "au", "snd",
                           "voc", "wv", "sd2", "iff", "8svx", "paf", "sf", "avr", "htk", "xi", "mat", "pvf", "sds" }) {
        inputs.add(e);
    }
    return json::Obj()
           .add("success", sf.loaded)
           .add("audacity_bin_dir", toUtf8(audacityBinDir()))
           .add("audacity_exe", toUtf8(audacityExePath()))
           .add("sndfile_version", sf.version)
           .add("sndfile_error", sf.error)
           .add("mp3_decoder", mp.loaded)
           .raw("input_extensions", inputs.str())
           .raw("output_formats", formats.str())
           .raw("effects", effects.str())
           .str();
}

std::string cmdInfo(const Value& job)
{
    const std::wstring in = needInput(job);
    std::string err;
    auto r = openReader(in, err);
    if (!r) {
        fail(err);
    }
    json::Obj o;
    o.add("success", true).add("input", toUtf8(in)).raw("info", infoJson(r->info()));
    if (job.flag("analyze", false)) {
        const int nch = r->info().channels;
        std::vector<float> buf(static_cast<size_t>(16384) * nch);
        float pk = 0.0f;
        double sum = 0.0;
        long long count = 0;
        for (;;) {
            const size_t n = r->read(buf.data(), 16384);
            if (n == 0) {
                break;
            }
            for (size_t i = 0; i < n * nch; ++i) {
                pk = std::max(pk, std::fabs(buf[i]));
                sum += static_cast<double>(buf[i]) * buf[i];
            }
            count += static_cast<long long>(n * nch);
        }
        o.add("peak_dbfs", dbfs(pk)).add("rms_dbfs", count && sum > 0 ? 10.0 * std::log10(sum / count) : -200.0);
    }
    return o.str();
}

std::string cmdSplit(const Value& job)
{
    const std::wstring in = needInput(job);
    const std::wstring outDir = needPath(job, "output_dir");
    const double seg = job.num("segment_seconds", 15.0);
    if (!(seg >= 0.05) || seg > 86400) {
        fail("segment_seconds must be between 0.05 and 86400");
    }
    const double minLast = std::max(0.0, job.num("min_last_seconds", 0.0));
    const bool overwrite = job.flag("overwrite", false);

    std::string err;
    auto r = openReader(in, err);
    if (!r) {
        fail(err);
    }
    const SourceInfo src = r->info();
    ResolvedOutput out;
    {
        OutputSpec spec = outputSpec(job);
        if (spec.format.empty()) {
            spec.format = formatFromExtension(in);
            if (spec.format.empty()) {
                spec.format = "wav";
            }
        }
        if (!resolveOutput(spec, src, out, err)) {
            fail(err);
        }
    }
    const double start = std::max(0.0, job.num("start", 0.0));
    double end = job.num("end", 0.0);
    const double total = src.seconds();
    if (end <= 0.0 || end > total) {
        end = total;
    }
    if (end - start <= 0.0) {
        fail("Nothing to split: empty time range");
    }
    const int64_t segFrames = std::max<int64_t>(1, std::llround(seg * src.rate));
    const int64_t firstFrame = std::llround(start * src.rate);
    const int64_t lastFrame = std::min<int64_t>(src.frames, std::llround(end * src.rate));
    const int64_t span = lastFrame - firstFrame;
    int64_t count = (span + segFrames - 1) / segFrames;
    const int64_t tail = span - (count - 1) * segFrames;
    const bool dropTail = count > 1 && static_cast<double>(tail) / src.rate < minLast;
    if (dropTail) {
        --count;
    }

    std::string prefix = job.str("prefix");
    if (prefix.empty()) {
        prefix = toUtf8(fs::path(in).stem().wstring());
    }
    for (const char* bad : { "\\", "/", ":", "*", "?", "\"", "<", ">", "|" }) {
        if (prefix.find(bad) != std::string::npos) {
            fail("prefix contains a character not allowed in file names");
        }
    }
    const int width = std::max(3, static_cast<int>(std::to_string(count).size()));
    const int firstIndex = static_cast<int>(job.num("first_index", 1));

    std::vector<std::wstring> names;
    for (int64_t i = 0; i < count; ++i) {
        char num[32];
        std::snprintf(num, sizeof(num), "%0*lld", width, static_cast<long long>(i + firstIndex));
        names.push_back(outDir + L"\\" + fromUtf8(prefix + "_" + num + "." + out.extension));
    }
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        fail("Cannot create output_dir: " + ec.message());
    }
    if (!overwrite) {
        for (const auto& n : names) {
            if (fs::exists(n, ec)) {
                fail("File already exists (set overwrite: true to replace it): " + toUtf8(n));
            }
        }
    }
    if (firstFrame > 0 && !r->seek(firstFrame)) {
        fail("Cannot seek to start");
    }

    const int nch = src.channels;
    std::vector<float> buf(static_cast<size_t>(16384) * nch);
    json::Arr files;
    long long totalBytes = 0;
    for (int64_t i = 0; i < count; ++i) {
        const int64_t want = std::min(segFrames, span - i * segFrames);
        Audio a;
        a.rate = src.rate;
        a.ch.assign(nch, {});
        for (auto& c : a.ch) {
            c.reserve(static_cast<size_t>(want));
        }
        int64_t got = 0;
        while (got < want) {
            const size_t n = r->read(buf.data(), static_cast<size_t>(std::min<int64_t>(16384, want - got)));
            if (n == 0) {
                break;
            }
            for (size_t k = 0; k < n; ++k) {
                for (int c = 0; c < nch; ++c) {
                    a.ch[c].push_back(buf[k * nch + c]);
                }
            }
            got += static_cast<int64_t>(n);
        }
        if (got == 0) {
            break;
        }
        if (!saveAudio(names[static_cast<size_t>(i)], a, out, err)) {
            fail("Writing " + toUtf8(names[static_cast<size_t>(i)]) + ": " + err);
        }
        const double t0 = static_cast<double>(firstFrame + i * segFrames) / src.rate;
        const auto bytes = fs::file_size(names[static_cast<size_t>(i)], ec);
        totalBytes += ec ? 0 : static_cast<long long>(bytes);
        files.raw(json::Obj()
                  .add("path", toUtf8(names[static_cast<size_t>(i)]))
                  .add("start_seconds", t0)
                  .add("end_seconds", t0 + static_cast<double>(got) / src.rate)
                  .add("bytes", static_cast<long long>(ec ? 0 : bytes))
                  .str());
    }
    return json::Obj()
           .add("success", true)
           .add("input", toUtf8(in))
           .add("output_dir", toUtf8(outDir))
           .raw("source", infoJson(src))
           .raw("output", outputJson(out))
           .add("segment_seconds", seg)
           .add("count", static_cast<long long>(count))
           .add("dropped_tail_seconds", dropTail ? static_cast<double>(tail) / src.rate : 0.0)
           .add("total_bytes", totalBytes)
           .raw("files", files.str())
           .str();
}

std::string cmdProcess(const Value& job)
{
    const std::wstring in = needInput(job);
    const std::wstring outPath = needPath(job, "output_path");
    const bool overwrite = job.flag("overwrite", false);
    if (fs::path(in) == fs::path(outPath)) {
        fail("output_path must be different from the input");
    }
    const auto effects = parseEffects(job);
    Audio a;
    SourceInfo src;
    std::string err;
    if (!loadAudio(in, job.num("start", 0.0), job.num("end", 0.0), a, src, err)) {
        fail(err);
    }
    json::Arr reports;
    for (const auto& e : effects) {
        std::string rep;
        if (!dsp::apply(a, e, err, &rep)) {
            fail(e.type + ": " + err);
        }
        if (!rep.empty()) {
            reports.raw(json::Obj().add("effect", e.type).raw("report", rep).str());
        }
    }
    SourceInfo cur = src;
    cur.channels = a.channels();
    ResolvedOutput out = resolveFor(outPath, outputSpec(job), cur);
    ensureWritable(outPath, overwrite);
    if (!saveAudio(outPath, a, out, err)) {
        fail(err);
    }
    return json::Obj()
           .add("success", true)
           .add("input", toUtf8(in))
           .raw("source", infoJson(src))
           .raw("result", fileResult(outPath, a, out))
           .add("peak_dbfs", dbfs(dsp::peak(a)))
           .add("effects_applied", static_cast<int>(effects.size()))
           .raw("effect_reports", reports.str())
           .str();
}

struct Piece {
    std::wstring path;
    double gainDb = 0, offset = 0, start = 0, end = 0;
};

std::vector<Piece> parsePieces(const Value& job)
{
    const Value* arr = job.find("inputs");
    if (!arr || arr->type != Value::Type::Array || arr->arr.empty()) {
        fail("\"inputs\" must be a non-empty array");
    }
    std::vector<Piece> list;
    for (const Value& v : arr->arr) {
        Piece p;
        std::string path;
        if (v.type == Value::Type::String) {
            path = v.s;
        } else if (v.type == Value::Type::Object) {
            path = v.str("path");
            p.gainDb = v.num("gain_db", 0);
            p.offset = v.num("offset_seconds", 0);
            p.start = v.num("start", 0);
            p.end = v.num("end", 0);
        }
        p.path = fromUtf8(path);
        std::error_code ec;
        if (path.empty() || !fs::path(p.path).is_absolute() || !fs::is_regular_file(p.path, ec)) {
            fail("Input not found or not absolute: " + path);
        }
        list.push_back(p);
    }
    return list;
}

std::string cmdMixOrConcat(const Value& job, bool mix)
{
    const auto pieces = parsePieces(job);
    const std::wstring outPath = needPath(job, "output_path");
    const bool overwrite = job.flag("overwrite", false);
    OutputSpec spec = outputSpec(job);
    std::vector<Audio> audios;
    int rate = spec.rate;
    int channels = 0;
    std::string err;
    SourceInfo firstInfo;
    for (const auto& p : pieces) {
        Audio a;
        SourceInfo info;
        if (!loadAudio(p.path, p.start, p.end, a, info, err)) {
            fail(toUtf8(p.path) + ": " + err);
        }
        if (audios.empty()) {
            firstInfo = info;
        }
        if (rate <= 0) {
            rate = a.rate;
        }
        channels = std::max(channels, a.channels());
        if (p.gainDb != 0.0) {
            dsp::Effect g;
            g.type = "gain";
            g.num["db"] = p.gainDb;
            dsp::apply(a, g, err);
        }
        audios.push_back(std::move(a));
    }
    channels = std::min(channels, 2);
    if (spec.rate > 0) {
        rate = spec.rate;
    }
    for (auto& a : audios) {
        a = dsp::convert(a, rate, channels);
    }
    Audio result;
    result.rate = rate;
    result.ch.assign(static_cast<size_t>(channels), {});
    if (mix) {
        size_t len = 0;
        for (size_t i = 0; i < audios.size(); ++i) {
            const size_t off = static_cast<size_t>(std::max(0.0, pieces[i].offset) * rate);
            len = std::max(len, off + audios[i].frames());
        }
        for (auto& c : result.ch) {
            c.assign(len, 0.0f);
        }
        for (size_t i = 0; i < audios.size(); ++i) {
            const size_t off = static_cast<size_t>(std::max(0.0, pieces[i].offset) * rate);
            for (int c = 0; c < channels; ++c) {
                const auto& src = audios[i].ch[static_cast<size_t>(c)];
                auto& dst = result.ch[static_cast<size_t>(c)];
                for (size_t k = 0; k < src.size(); ++k) {
                    dst[off + k] += src[k];
                }
            }
        }
    } else {
        const size_t xf = static_cast<size_t>(std::max(0.0, job.num("crossfade_seconds", 0.0)) * rate);
        const size_t gap = static_cast<size_t>(std::max(0.0, job.num("gap_seconds", 0.0)) * rate);
        for (size_t i = 0; i < audios.size(); ++i) {
            const Audio& a = audios[i];
            for (int c = 0; c < channels; ++c) {
                auto& dst = result.ch[static_cast<size_t>(c)];
                const auto& src = a.ch[static_cast<size_t>(c)];
                size_t overlap = 0;
                if (i > 0 && gap == 0) {
                    overlap = std::min({ xf, dst.size(), src.size() });
                }
                if (i > 0 && gap > 0) {
                    dst.insert(dst.end(), gap, 0.0f);
                }
                const size_t base = dst.size() - overlap;
                for (size_t k = 0; k < overlap; ++k) {
                    const double t = (k + 0.5) / overlap;
                    // Equal-power crossfade
                    dst[base + k] = static_cast<float>(dst[base + k] * std::cos(t * 1.5707963) + src[k] * std::sin(t * 1.5707963));
                }
                dst.insert(dst.end(), src.begin() + static_cast<std::ptrdiff_t>(overlap), src.end());
            }
        }
    }
    if (job.flag("normalize", false)) {
        dsp::Effect n;
        n.type = "normalize";
        n.num["peak_db"] = job.num("normalize_peak_db", -1.0);
        dsp::apply(result, n, err);
    }
    SourceInfo cur = firstInfo;
    cur.rate = rate;
    cur.channels = channels;
    ResolvedOutput out = resolveFor(outPath, spec, cur);
    ensureWritable(outPath, overwrite);
    const float pk = dsp::peak(result);
    if (!saveAudio(outPath, result, out, err)) {
        fail(err);
    }
    return json::Obj()
           .add("success", true)
           .add("inputs", static_cast<int>(pieces.size()))
           .raw("result", fileResult(outPath, result, out))
           .add("peak_dbfs", dbfs(pk))
           .add("clipping_warning", pk > 1.0f)
           .str();
}

} // namespace

int wmain()
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    std::stringstream ss;
    ss << std::cin.rdbuf();
    std::string result;
    int code = 0;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        const std::string text = ss.str();
        Value job = text.empty() ? Value{} : json::Parser(text).parse();
        const std::string cmd = job.str("command", "probe");
        if (cmd == "probe") {
            result = cmdProbe();
        } else {
            if (!sndfile().loaded) {
                fail(sndfile().error);
            }
            if (cmd == "info") {
                result = cmdInfo(job);
            } else if (cmd == "split") {
                result = cmdSplit(job);
            } else if (cmd == "process") {
                result = cmdProcess(job);
            } else if (cmd == "mix") {
                result = cmdMixOrConcat(job, true);
            } else if (cmd == "concat") {
                result = cmdMixOrConcat(job, false);
            } else {
                fail("Unknown command: " + cmd);
            }
        }
    } catch (const std::exception& e) {
        result = json::Obj().add("success", false).add("error", e.what()).str();
        code = 1;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (!result.empty() && result.back() == '}') {
        result.insert(result.size() - 1, ",\"engine_ms\":" + std::to_string(ms));
    }
    fwrite(result.data(), 1, result.size(), stdout);
    fputc('\n', stdout);
    return code;
}
