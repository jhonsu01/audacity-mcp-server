// Native side of the "MCP Audio Tools" Audacity 4 extension.
// Audacity loads this DLL through MuseApi.Native and calls extension_dispatch_v0(call, args...).
// It gives the extension's JavaScript what the sandboxed script engine cannot do itself:
// reading and writing audio files (with Audacity's own libsndfile / mpg123) and creating folders.
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "audio.h"
#include "json.h"
#include "../include/nativeextension.h"

using namespace aumcp;
namespace fs = std::filesystem;

namespace {

thread_local std::string tlsReturn;

struct ReadHandle {
    std::unique_ptr<Reader> reader;
    std::vector<float> scratch;
};

struct WriteHandle {
    std::unique_ptr<Writer> writer;
    std::filesystem::path path;
    std::vector<float> scratch;
};

int32_t ok(ext_value* r)
{
    r->type = EXT_VALUE_NONE;
    return EXT_STATUS_OK;
}

int32_t okString(ext_value* r, std::string s)
{
    tlsReturn = std::move(s);
    r->type = EXT_VALUE_STRING;
    r->as_string = tlsReturn.c_str();
    return EXT_STATUS_OK;
}

int32_t okNumber(ext_value* r, double v)
{
    r->type = EXT_VALUE_NUMBER;
    r->as_number = v;
    return EXT_STATUS_OK;
}

int32_t okBool(ext_value* r, bool v)
{
    r->type = EXT_VALUE_BOOL;
    r->as_bool = v;
    return EXT_STATUS_OK;
}

// An error with a message: Audacity shows the returned string as the JavaScript exception text.
int32_t error(ext_value* r, std::string msg)
{
    tlsReturn = std::move(msg);
    r->type = EXT_VALUE_STRING;
    r->as_string = tlsReturn.c_str();
    return EXT_STATUS_ERROR;
}

bool isString(const ext_value* a, uint32_t n, uint32_t i)
{
    return i < n && a[i].type == EXT_VALUE_STRING && a[i].as_string;
}
bool isNumber(const ext_value* a, uint32_t n, uint32_t i)
{
    return i < n && a[i].type == EXT_VALUE_NUMBER;
}
bool isBuffer(const ext_value* a, uint32_t n, uint32_t i)
{
    return i < n && a[i].type == EXT_VALUE_BUFFER && a[i].as_buffer.data;
}
template<typename T>
T* handle(const ext_value* a, uint32_t n, uint32_t i)
{
    return i < n && a[i].type == EXT_VALUE_OBJECT ? static_cast<T*>(a[i].as_object) : nullptr;
}

// writer_open(path, format, sampleFormat, channels, sampleRate, quality) -> handle
int32_t writerOpen(const ext_value* a, uint32_t n, ext_value* r)
{
    if (!isString(a, n, 0) || !isString(a, n, 1) || !isNumber(a, n, 3) || !isNumber(a, n, 4)) {
        return EXT_STATUS_INVALID_ARGUMENT;
    }
    const std::filesystem::path path = fromUtf8(a[0].as_string);
    OutputSpec spec;
    spec.format = a[1].as_string;
    spec.sample = isString(a, n, 2) ? a[2].as_string : "auto";
    spec.channels = static_cast<int>(a[3].as_number);
    spec.rate = static_cast<int>(a[4].as_number);
    if (isNumber(a, n, 5)) {
        spec.quality = a[5].as_number;
        spec.compression = a[5].as_number;
    }
    SourceInfo src;
    src.rate = spec.rate;
    src.channels = spec.channels;
    src.sample = "float32";
    ResolvedOutput out;
    std::string err;
    if (!resolveOutput(spec, src, out, err)) {
        return error(r, err);
    }
    if (out.rate != spec.rate) {
        return error(r, "The " + out.format + " format cannot store " + std::to_string(spec.rate) + " Hz audio");
    }
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
    }
    auto w = openWriter(path, out, err);
    if (!w) {
        return error(r, err);
    }
    auto* h = new WriteHandle{ std::move(w), path, {} };
    r->type = EXT_VALUE_OBJECT;
    r->as_object = h;
    return EXT_STATUS_OK;
}

// writer_write(handle, channel0[, channel1]) -> frames. Buffers hold float32 samples.
int32_t writerWrite(const ext_value* a, uint32_t n, ext_value* r)
{
    auto* h = handle<WriteHandle>(a, n, 0);
    if (!h || !h->writer || !isBuffer(a, n, 1)) {
        return EXT_STATUS_INVALID_ARGUMENT;
    }
    const int nch = h->writer->output().channels;
    const size_t frames = a[1].as_buffer.size / sizeof(float);
    const float* c0 = static_cast<const float*>(a[1].as_buffer.data);
    const float* c1 = isBuffer(a, n, 2) && a[2].as_buffer.size / sizeof(float) == frames
                      ? static_cast<const float*>(a[2].as_buffer.data) : c0;
    h->scratch.resize(frames * nch);
    for (size_t i = 0; i < frames; ++i) {
        if (nch == 1) {
            h->scratch[i] = c1 == c0 ? c0[i] : 0.5f * (c0[i] + c1[i]);
        } else {
            h->scratch[i * nch] = c0[i];
            h->scratch[i * nch + 1] = c1[i];
            for (int c = 2; c < nch; ++c) {
                h->scratch[i * nch + c] = 0.0f;
            }
        }
    }
    if (!h->writer->write(h->scratch.data(), frames)) {
        return error(r, "Write error (disk full?)");
    }
    return okNumber(r, static_cast<double>(frames));
}

// writer_close(handle) -> JSON {path, frames, bytes}
int32_t writerClose(const ext_value* a, uint32_t n, ext_value* r)
{
    auto* h = handle<WriteHandle>(a, n, 0);
    if (!h) {
        return EXT_STATUS_INVALID_ARGUMENT;
    }
    std::string err;
    const bool good = h->writer->close(&err);
    const long long frames = h->writer->frames();
    const std::filesystem::path path = h->path;
    delete h;
    if (!good) {
        return error(r, err);
    }
    std::error_code ec;
    const auto bytes = fs::file_size(path, ec);
    return okString(r, json::Obj().add("path", toUtf8(path)).add("frames", frames).add("bytes", static_cast<long long>(ec ? 0 : bytes)).str());
}

// reader_open(path) -> handle
int32_t readerOpen(const ext_value* a, uint32_t n, ext_value* r)
{
    if (!isString(a, n, 0)) {
        return EXT_STATUS_INVALID_ARGUMENT;
    }
    std::string err;
    auto rd = openReader(fromUtf8(a[0].as_string), err);
    if (!rd) {
        return error(r, err);
    }
    auto* h = new ReadHandle{ std::move(rd), {} };
    r->type = EXT_VALUE_OBJECT;
    r->as_object = h;
    return EXT_STATUS_OK;
}

// reader_info(handle) -> JSON
int32_t readerInfo(const ext_value* a, uint32_t n, ext_value* r)
{
    auto* h = handle<ReadHandle>(a, n, 0);
    if (!h) {
        return EXT_STATUS_INVALID_ARGUMENT;
    }
    const SourceInfo& i = h->reader->info();
    return okString(r, json::Obj()
                    .add("sampleRate", i.rate)
                    .add("channels", i.channels)
                    .add("frames", static_cast<long long>(i.frames))
                    .add("container", i.container)
                    .add("sampleFormat", i.sample)
                    .str());
}

// reader_read(handle, channel0[, channel1]) -> frames read (fills the buffers, up to their size).
int32_t readerRead(const ext_value* a, uint32_t n, ext_value* r)
{
    auto* h = handle<ReadHandle>(a, n, 0);
    if (!h || !isBuffer(a, n, 1)) {
        return EXT_STATUS_INVALID_ARGUMENT;
    }
    const int nch = h->reader->info().channels;
    const size_t frames = a[1].as_buffer.size / sizeof(float);
    float* c0 = static_cast<float*>(a[1].as_buffer.data);
    float* c1 = isBuffer(a, n, 2) && a[2].as_buffer.size / sizeof(float) >= frames ? static_cast<float*>(a[2].as_buffer.data) : nullptr;
    h->scratch.resize(frames * nch);
    size_t got = 0;
    while (got < frames) {
        const size_t k = h->reader->read(h->scratch.data() + got * nch, frames - got);
        if (k == 0) {
            break;
        }
        got += k;
    }
    for (size_t i = 0; i < got; ++i) {
        const float* f = h->scratch.data() + i * nch;
        if (c1) {
            c0[i] = f[0];
            c1[i] = nch > 1 ? f[1] : f[0];
        } else {
            float s = 0.0f;
            for (int c = 0; c < nch; ++c) {
                s += f[c];
            }
            c0[i] = s / nch;
        }
    }
    return okNumber(r, static_cast<double>(got));
}

int32_t readerClose(const ext_value* a, uint32_t n, ext_value* r)
{
    delete handle<ReadHandle>(a, n, 0);
    return ok(r);
}

int32_t makeDir(const ext_value* a, uint32_t n, ext_value* r)
{
    if (!isString(a, n, 0)) {
        return EXT_STATUS_INVALID_ARGUMENT;
    }
    std::error_code ec;
    fs::create_directories(fromUtf8(a[0].as_string), ec);
    if (ec) {
        return error(r, "Cannot create folder: " + ec.message());
    }
    return okBool(r, true);
}

int32_t fileExists(const ext_value* a, uint32_t n, ext_value* r)
{
    if (!isString(a, n, 0)) {
        return EXT_STATUS_INVALID_ARGUMENT;
    }
    std::error_code ec;
    return okBool(r, fs::exists(fromUtf8(a[0].as_string), ec));
}

int32_t version(ext_value* r)
{
    return okString(r, json::Obj()
                    .add("engine", "audacity-mcp-tools")
                    .add("sndfile", sndfile().version)
                    .add("sndfileError", sndfile().error)
                    .add("mp3", mpg123().loaded)
                    .str());
}

} // namespace

extern "C" EXT_EXPORT int32_t extension_dispatch_v0(const char* call, const ext_value* args, uint32_t argCount, ext_value* result)
{
    if (!call || !result) {
        return EXT_STATUS_INVALID_ARGUMENT;
    }
    try {
        const std::string c = call;
        if (c == "version") {
            return version(result);
        }
        if (!sndfile().loaded) {
            return error(result, sndfile().error);
        }
        if (c == "writer_open") {
            return writerOpen(args, argCount, result);
        }
        if (c == "writer_write") {
            return writerWrite(args, argCount, result);
        }
        if (c == "writer_close") {
            return writerClose(args, argCount, result);
        }
        if (c == "reader_open") {
            return readerOpen(args, argCount, result);
        }
        if (c == "reader_info") {
            return readerInfo(args, argCount, result);
        }
        if (c == "reader_read") {
            return readerRead(args, argCount, result);
        }
        if (c == "reader_close") {
            return readerClose(args, argCount, result);
        }
        if (c == "mkdir") {
            return makeDir(args, argCount, result);
        }
        if (c == "exists") {
            return fileExists(args, argCount, result);
        }
        return EXT_STATUS_UNKNOWN_CALL;
    } catch (const std::exception& e) {
        return error(result, e.what());
    } catch (...) {
        return error(result, "Native error");
    }
}
