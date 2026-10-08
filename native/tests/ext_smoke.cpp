// Smoke test of the Audacity extension library: loads audacity_mcp_native the way Audacity's
// MuseApi.Native does (dlopen / LoadLibrary + extension_dispatch_v0) and round-trips a tone
// through writer_* and reader_*.  Usage: ext_smoke <library> <temp folder>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../include/nativeextension.h"

#ifdef _WIN32
#include <windows.h>
static void* openLib(const char* p) { return LoadLibraryA(p); }
static void* sym(void* h, const char* s) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h), s)); }
#else
#include <dlfcn.h>
static void* openLib(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void* sym(void* h, const char* s) { return dlsym(h, s); }
#endif

static int failures = 0;
static void check(bool ok, const std::string& msg)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", msg.c_str());
    if (!ok) {
        ++failures;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: ext_smoke <library> <temp folder>\n");
        return 2;
    }
    void* h = openLib(argv[1]);
    check(h != nullptr, std::string("load ") + argv[1]);
    if (!h) {
        return 1;
    }
    auto dispatch = reinterpret_cast<ext_dispatch_fn>(sym(h, "extension_dispatch_v0"));
    check(dispatch != nullptr, "extension_dispatch_v0 exported");
    if (!dispatch) {
        return 1;
    }
    ext_value r{};
    int32_t st = dispatch("version", nullptr, 0, &r);
    check(st == EXT_STATUS_OK && r.type == EXT_VALUE_STRING, std::string("version: ") + (r.as_string ? r.as_string : "?"));

    const std::string path = std::string(argv[2]) + "/ext_smoke.flac";
    ext_value args[6]{};
    args[0].type = EXT_VALUE_STRING; args[0].as_string = path.c_str();
    args[1].type = EXT_VALUE_STRING; args[1].as_string = "flac";
    args[2].type = EXT_VALUE_STRING; args[2].as_string = "pcm24";
    args[3].type = EXT_VALUE_NUMBER; args[3].as_number = 2;
    args[4].type = EXT_VALUE_NUMBER; args[4].as_number = 48000;
    args[5].type = EXT_VALUE_NUMBER; args[5].as_number = 0.5;
    st = dispatch("writer_open", args, 6, &r);
    check(st == EXT_STATUS_OK && r.type == EXT_VALUE_OBJECT, "writer_open flac/pcm24 48 kHz stereo");
    if (st != EXT_STATUS_OK) {
        std::printf("      %s\n", r.as_string ? r.as_string : "");
        return 1;
    }
    void* writer = r.as_object;
    const size_t n = 48000;
    std::vector<float> left(n), right(n);
    for (size_t i = 0; i < n; ++i) {
        left[i] = 0.5f * static_cast<float>(std::sin(2 * 3.14159265 * 440 * i / 48000.0));
        right[i] = -left[i];
    }
    ext_value w[3]{};
    w[0].type = EXT_VALUE_OBJECT; w[0].as_object = writer;
    w[1].type = EXT_VALUE_BUFFER; w[1].as_buffer = { left.data(), n * sizeof(float) };
    w[2].type = EXT_VALUE_BUFFER; w[2].as_buffer = { right.data(), n * sizeof(float) };
    st = dispatch("writer_write", w, 3, &r);
    check(st == EXT_STATUS_OK && r.as_number == n, "writer_write 1 s");
    st = dispatch("writer_close", w, 1, &r);
    check(st == EXT_STATUS_OK && r.type == EXT_VALUE_STRING, std::string("writer_close: ") + (r.as_string ? r.as_string : ""));

    ext_value o[1]{};
    o[0].type = EXT_VALUE_STRING; o[0].as_string = path.c_str();
    st = dispatch("reader_open", o, 1, &r);
    check(st == EXT_STATUS_OK && r.type == EXT_VALUE_OBJECT, "reader_open");
    void* reader = r.as_object;
    ext_value ri[1]{};
    ri[0].type = EXT_VALUE_OBJECT; ri[0].as_object = reader;
    st = dispatch("reader_info", ri, 1, &r);
    const std::string info = r.as_string ? r.as_string : "";
    check(st == EXT_STATUS_OK && info.find("\"frames\":48000") != std::string::npos, "reader_info: " + info);
    std::vector<float> l2(n), r2(n);
    ext_value rr[3]{};
    rr[0] = ri[0];
    rr[1].type = EXT_VALUE_BUFFER; rr[1].as_buffer = { l2.data(), n * sizeof(float) };
    rr[2].type = EXT_VALUE_BUFFER; rr[2].as_buffer = { r2.data(), n * sizeof(float) };
    st = dispatch("reader_read", rr, 3, &r);
    double err = 0.0;
    for (size_t i = 0; i < n; ++i) {
        err = std::fmax(err, std::fabs(l2[i] - left[i]) + std::fabs(r2[i] - right[i]));
    }
    check(st == EXT_STATUS_OK && r.as_number == n && err < 1e-5, "reader_read round trip, max error " + std::to_string(err));
    dispatch("reader_close", ri, 1, &r);
    st = dispatch("no_such_call", nullptr, 0, &r);
    check(st == EXT_STATUS_UNKNOWN_CALL, "unknown call rejected");
    return failures ? 1 : 0;
}
