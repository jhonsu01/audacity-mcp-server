#include "codecs.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <link.h>
#endif
#endif

namespace fs = std::filesystem;

namespace aumcp {
namespace {

bool isFile(const fs::path& p)
{
    std::error_code e;
    return fs::is_regular_file(p, e);
}

bool isDir(const fs::path& p)
{
    std::error_code e;
    return fs::is_directory(p, e);
}

std::string lower(std::string s)
{
    for (auto& c : s) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Environment variable as a path; unfilled MCPB placeholders ("${user_config.x}") count as unset.
fs::path envPath(const char* name)
{
#ifdef _WIN32
    std::wstring wname(name, name + strlen(name));
    wchar_t buf[4096];
    const DWORD n = GetEnvironmentVariableW(wname.c_str(), buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return {};
    }
    std::wstring v(buf, n);
#else
    const char* raw = std::getenv(name);
    if (!raw) {
        return {};
    }
    std::string v = raw;
#endif
    while (!v.empty() && (v.front() == '"' || v.front() == ' ')) {
        v.erase(v.begin());
    }
    while (!v.empty() && (v.back() == '"' || v.back() == ' ' || v.back() == '/' || v.back() == '\\')) {
        v.pop_back();
    }
    fs::path p(v);
    if (u8(p).find("${") != std::string::npos) {
        return {};
    }
    return p;
}

#ifdef _WIN32
constexpr const char* SNDFILE_PREFIX = "sndfile";
constexpr const char* MPG123_PREFIX = "mpg123";
#else
constexpr const char* SNDFILE_PREFIX = "libsndfile";
constexpr const char* MPG123_PREFIX = "libmpg123";
#endif

// Shared library named <prefix>.dll / <prefix>[.N].dylib / <prefix>.so[.N] inside dir.
fs::path findLibIn(const fs::path& dir, const std::string& prefix)
{
    if (!isDir(dir)) {
        return {};
    }
    std::error_code e;
    for (const auto& entry : fs::directory_iterator(dir, e)) {
        if (!entry.is_regular_file(e) && !entry.is_symlink(e)) {
            continue;
        }
        const std::string name = lower(u8(entry.path().filename()));
        if (name.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::string rest = name.substr(prefix.size());
#ifdef _WIN32
        if (rest == ".dll" || rest == "-1.dll") {
            return entry.path();
        }
#elif defined(__APPLE__)
        if (rest.size() >= 6 && rest.substr(rest.size() - 6) == ".dylib" && (rest[0] == '.' || rest[0] == '-')) {
            return entry.path();
        }
#else
        if (rest.rfind(".so", 0) == 0) {
            return entry.path();
        }
#endif
    }
    return {};
}

// Folders where an Audacity installation keeps its codec libraries.
std::vector<fs::path> audacityLibDirs(const fs::path& root)
{
    std::vector<fs::path> dirs;
    if (root.empty()) {
        return dirs;
    }
    fs::path base = root;
    if (isFile(base)) { // Audacity4.exe, an AppImage or a binary
        base = base.parent_path();
    }
    for (const char* sub : { "", "bin", "lib", "lib64", "usr/lib", "usr/lib64", "Contents/Frameworks",
                             "Contents/Resources/Frameworks", "Contents/MacOS", "Contents/lib", "../lib" }) {
        dirs.push_back(*sub ? base / sub : base);
    }
    return dirs;
}

std::vector<fs::path> defaultAudacityRoots()
{
    std::vector<fs::path> roots;
#ifdef _WIN32
    for (const char* var : { "ProgramFiles", "ProgramW6432", "ProgramFiles(x86)" }) {
        const fs::path pf = envPath(var);
        if (!pf.empty()) {
            roots.push_back(pf / "Audacity 4");
            roots.push_back(pf / "Audacity4");
            roots.push_back(pf / "Audacity");
        }
    }
    roots.push_back("C:/Program Files/Audacity 4");
#elif defined(__APPLE__)
    roots.push_back("/Applications/Audacity.app");
    roots.push_back("/Applications/Audacity 4.app");
    const fs::path home = envPath("HOME");
    if (!home.empty()) {
        roots.push_back(home / "Applications/Audacity.app");
    }
#else
    roots.push_back("/opt/audacity");
    roots.push_back("/usr/lib/audacity");
    roots.push_back("/usr/local/lib/audacity");
#endif
    return roots;
}

// Already-loaded copy of a library (the engine running inside Audacity's process).
fs::path loadedInProcess(const std::string& prefix)
{
#ifdef _WIN32
    const std::wstring w(prefix.begin(), prefix.end());
    if (HMODULE m = GetModuleHandleW((w + L".dll").c_str())) {
        wchar_t buf[MAX_PATH * 2];
        const DWORD n = GetModuleFileNameW(m, buf, static_cast<DWORD>(std::size(buf)));
        if (n > 0) {
            return fs::path(std::wstring(buf, n));
        }
    }
    return {};
#elif defined(__APPLE__)
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const char* name = _dyld_get_image_name(i);
        if (name && lower(fs::path(name).filename().string()).rfind(prefix, 0) == 0) {
            return fs::path(name);
        }
    }
    return {};
#else
    struct Ctx {
        std::string prefix;
        fs::path found;
    } ctx{ prefix, {} };
    dl_iterate_phdr(
        [](struct dl_phdr_info* info, size_t, void* data) -> int {
        auto* c = static_cast<Ctx*>(data);
        if (info->dlpi_name && *info->dlpi_name) {
            const fs::path p(info->dlpi_name);
            if (lower(p.filename().string()).rfind(c->prefix, 0) == 0) {
                c->found = p;
                return 1;
            }
        }
        return 0;
    },
        &ctx);
    return ctx.found;
#endif
}

struct Lib {
    void* handle = nullptr;
    std::string path;
    std::string source;
    template<typename F>
    bool bind(const char* sym, F& out) const
    {
#ifdef _WIN32
        out = reinterpret_cast<F>(GetProcAddress(static_cast<HMODULE>(handle), sym));
#else
        out = reinterpret_cast<F>(dlsym(handle, sym));
#endif
        return out != nullptr;
    }
};

void* openLibrary(const fs::path& p, std::string& err)
{
#ifdef _WIN32
    HMODULE m = LoadLibraryExW(p.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!m) {
        err = "LoadLibrary failed (" + std::to_string(GetLastError()) + ")";
    }
    return m;
#else
    void* h = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* e = dlerror();
        err = e ? e : "dlopen failed";
    }
    return h;
#endif
}

void* openSystem(const char* name, std::string& err)
{
#ifdef _WIN32
    HMODULE m = LoadLibraryA(name);
    if (!m) {
        err = "not on PATH";
    }
    return m;
#else
    void* h = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* e = dlerror();
        err = e ? e : "dlopen failed";
    }
    return h;
#endif
}

// macOS / Linux: libsndfile inside Audacity.app (or an extracted AppImage) refers to its codec
// dependencies through @rpath / RUNPATH entries that only resolve inside Audacity's own process.
// Loading those siblings first lets the dynamic loader reuse them by install name / soname.
// Only codec libraries are touched (never Qt or anything else in the folder).
void preloadCodecSiblings(const fs::path& dir)
{
#ifdef _WIN32
    (void)dir; // Windows already searches the DLL's own folder (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)
#else
    static const char* const prefixes[] = { "libogg", "libflac", "libvorbis", "libopus", "libmp3lame", "libmpg123" };
    std::vector<fs::path> pending;
    std::error_code e;
    for (const auto& entry : fs::directory_iterator(dir, e)) {
        const std::string name = lower(u8(entry.path().filename()));
        for (const char* p : prefixes) {
#ifdef __APPLE__
            const bool lib = name.find(".dylib") != std::string::npos;
#else
            const bool lib = name.find(".so") != std::string::npos;
#endif
            if (lib && name.rfind(p, 0) == 0) {
                pending.push_back(entry.path());
            }
        }
    }
    // A few passes resolve libraries that depend on each other (vorbisenc -> vorbis -> ogg).
    for (int pass = 0; pass < 4 && !pending.empty(); ++pass) {
        std::vector<fs::path> failed;
        for (const auto& p : pending) {
            if (!dlopen(p.c_str(), RTLD_NOW | RTLD_GLOBAL)) {
                failed.push_back(p);
            }
        }
        if (failed.size() == pending.size()) {
            break;
        }
        pending.swap(failed);
    }
#endif
}

fs::path gAudacityRoot; // the installation the libraries came from (or the one found)
std::once_flag gOnceRoot;

const fs::path& audacityRoot()
{
    std::call_once(gOnceRoot, [] {
        std::vector<fs::path> roots;
        const fs::path env = envPath("AUDACITY_DIR");
        if (!env.empty()) {
            roots.push_back(env);
        }
        for (const auto& r : defaultAudacityRoots()) {
            roots.push_back(r);
        }
        for (const auto& r : roots) {
            for (const auto& d : audacityLibDirs(r)) {
                if (!findLibIn(d, SNDFILE_PREFIX).empty()) {
                    gAudacityRoot = r;
                    return;
                }
            }
        }
        // No bundled codecs found: still remember an existing installation (for open_in_audacity).
        for (const auto& r : roots) {
            std::error_code e;
            if (fs::exists(r, e)) {
                gAudacityRoot = r;
                return;
            }
        }
    });
    return gAudacityRoot;
}

Lib loadCodecLib(const std::string& prefix, const std::vector<const char*>& systemNames, std::string& error)
{
    Lib lib;
    std::string errors;
    // 1. Inside Audacity's process
    const fs::path inProc = loadedInProcess(prefix);
    if (!inProc.empty()) {
        std::string e;
        if ((lib.handle = openLibrary(inProc, e))) {
            lib.path = u8(inProc);
            lib.source = "process";
            return lib;
        }
    }
    // 2-3. AUDACITY_DIR and default installations
    std::vector<fs::path> roots;
    const fs::path env = envPath("AUDACITY_DIR");
    if (!env.empty()) {
        roots.push_back(env);
    }
    for (const auto& r : defaultAudacityRoots()) {
        roots.push_back(r);
    }
    for (const auto& r : roots) {
        for (const auto& d : audacityLibDirs(r)) {
            const fs::path f = findLibIn(d, prefix);
            if (f.empty()) {
                continue;
            }
            preloadCodecSiblings(d);
            std::string e;
            if ((lib.handle = openLibrary(f, e))) {
                lib.path = u8(f);
                lib.source = "audacity";
                return lib;
            }
            errors += u8(f) + ": " + e + "; ";
        }
    }
    // 4. System libraries
    for (const char* name : systemNames) {
        std::string e;
        if ((lib.handle = openSystem(name, e))) {
            lib.path = name;
            lib.source = "system";
            return lib;
        }
    }
    error = "not found";
    if (!errors.empty()) {
        error += " (" + errors + ")";
    }
    return lib;
}

std::once_flag gOnceSf, gOnceMpg;
Sndfile gSf;
Mpg123 gMpg;

const char* SF_HINT =
#ifdef _WIN32
    "Install Audacity 4 or set AUDACITY_DIR to its install folder.";
#elif defined(__APPLE__)
    "Install Audacity 4 in /Applications, set AUDACITY_DIR to Audacity.app, or install libsndfile (brew install libsndfile mpg123).";
#else
    "Install libsndfile (Debian/Ubuntu: sudo apt install libsndfile1 libmpg123-0; Fedora: sudo dnf install libsndfile mpg123-libs) "
    "or set AUDACITY_DIR to an extracted Audacity AppImage (Audacity.AppImage --appimage-extract).";
#endif

} // namespace

std::string u8(const fs::path& p)
{
#if defined(__cpp_char8_t)
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
#else
    return p.u8string();
#endif
}

fs::path pathFromU8(const std::string& s)
{
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

const char* platformName()
{
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

SNDFILE* sfOpen(const fs::path& p, int mode, SF_INFO* info)
{
    Sndfile& sf = sndfile();
    if (!sf.loaded) {
        return nullptr;
    }
#ifdef _WIN32
    return sf.wchar_open(p.c_str(), mode, info);
#else
    return sf.open(p.c_str(), mode, info);
#endif
}

std::string audacityLibDir()
{
    Sndfile& sf = sndfile();
    if (!sf.loaded || sf.library.empty()) {
        return {};
    }
    const fs::path p = pathFromU8(sf.library);
    return p.has_parent_path() ? u8(p.parent_path()) : std::string{};
}

std::string audacityExePath()
{
    const fs::path& root = audacityRoot();
    if (root.empty()) {
        return {};
    }
#ifdef _WIN32
    if (isFile(root)) {
        return u8(root);
    }
    for (const char* exe : { "bin/Audacity4.exe", "Audacity4.exe", "bin/Audacity.exe", "Audacity.exe" }) {
        if (isFile(root / exe)) {
            return u8((root / exe).make_preferred());
        }
    }
    return {};
#elif defined(__APPLE__)
    return u8(root); // Audacity.app (opened with `open -a`)
#else
    if (isFile(root)) {
        return u8(root); // AppImage or binary
    }
    for (const char* exe : { "AppRun", "bin/audacity", "audacity", "usr/bin/audacity" }) {
        if (isFile(root / exe)) {
            return u8(root / exe);
        }
    }
    return {};
#endif
}

Sndfile& sndfile()
{
    std::call_once(gOnceSf, [] {
#ifdef _WIN32
        const std::vector<const char*> sys = { "sndfile.dll", "libsndfile-1.dll" };
#elif defined(__APPLE__)
        const std::vector<const char*> sys = { "libsndfile.1.dylib", "/opt/homebrew/lib/libsndfile.1.dylib",
                                               "/usr/local/lib/libsndfile.1.dylib", "/opt/local/lib/libsndfile.1.dylib" };
#else
        const std::vector<const char*> sys = { "libsndfile.so.1", "libsndfile.so" };
#endif
        std::string err;
        Lib lib = loadCodecLib(SNDFILE_PREFIX, sys, err);
        if (!lib.handle) {
            gSf.error = std::string("libsndfile ") + err + ". " + SF_HINT;
            return;
        }
        const bool ok =
#ifdef _WIN32
            lib.bind("sf_wchar_open", gSf.wchar_open) &&
#else
            lib.bind("sf_open", gSf.open) &&
#endif
            lib.bind("sf_close", gSf.close) && lib.bind("sf_readf_float", gSf.readf_float)
            && lib.bind("sf_writef_float", gSf.writef_float) && lib.bind("sf_seek", gSf.seek)
            && lib.bind("sf_command", gSf.command) && lib.bind("sf_strerror", gSf.strerror)
            && lib.bind("sf_version_string", gSf.version_string);
        if (!ok) {
            gSf.error = "libsndfile at " + lib.path + " is missing expected functions";
            return;
        }
        gSf.library = lib.path;
        gSf.source = lib.source;
        gSf.version = gSf.version_string();
        gSf.loaded = true;
    });
    return gSf;
}

Mpg123& mpg123()
{
    std::call_once(gOnceMpg, [] {
#ifdef _WIN32
        const std::vector<const char*> sys = { "mpg123.dll", "libmpg123-0.dll" };
#elif defined(__APPLE__)
        const std::vector<const char*> sys = { "libmpg123.0.dylib", "/opt/homebrew/lib/libmpg123.0.dylib",
                                               "/usr/local/lib/libmpg123.0.dylib", "/opt/local/lib/libmpg123.0.dylib" };
#else
        const std::vector<const char*> sys = { "libmpg123.so.0", "libmpg123.so" };
#endif
        std::string err;
        Lib lib = loadCodecLib(MPG123_PREFIX, sys, err);
        if (!lib.handle) {
            gMpg.error = std::string("libmpg123 ") + err;
            return;
        }
        const bool ok = lib.bind("mpg123_init", gMpg.init) && lib.bind("mpg123_new", gMpg.create)
                        && lib.bind("mpg123_delete", gMpg.destroy) && lib.bind("mpg123_format_none", gMpg.format_none)
                        && lib.bind("mpg123_format", gMpg.format) && lib.bind("mpg123_open_feed", gMpg.open_feed)
                        && lib.bind("mpg123_decode", gMpg.decode) && lib.bind("mpg123_getformat", gMpg.getformat)
                        && lib.bind("mpg123_close", gMpg.close) && lib.bind("mpg123_plain_strerror", gMpg.plain_strerror);
        if (!ok) {
            gMpg.error = "libmpg123 at " + lib.path + " is missing expected functions";
            return;
        }
        gMpg.library = lib.path;
        gMpg.init();
        gMpg.loaded = true;
    });
    return gMpg;
}

} // namespace aumcp
