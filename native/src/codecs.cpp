#include "codecs.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mutex>
#include <vector>

namespace aumcp {
namespace {

bool fileExists(const std::wstring& p)
{
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExists(const std::wstring& p)
{
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring moduleDir(HMODULE m)
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(m, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0) {
        return {};
    }
    std::wstring p(buf, n);
    const size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : p.substr(0, slash);
}

std::wstring envVar(const wchar_t* name)
{
    wchar_t buf[2048];
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return {};
    }
    std::wstring v(buf, n);
    while (!v.empty() && (v.front() == L'"' || v.front() == L' ')) {
        v.erase(v.begin());
    }
    while (!v.empty() && (v.back() == L'"' || v.back() == L' ' || v.back() == L'\\' || v.back() == L'/')) {
        v.pop_back();
    }
    // An unfilled MCPB user_config placeholder arrives literally: treat it as unset.
    if (v.find(L"${") != std::wstring::npos) {
        return {};
    }
    return v;
}

std::wstring findBinDir()
{
    // 1. Running inside Audacity: the libraries are already loaded.
    if (HMODULE m = GetModuleHandleW(L"sndfile.dll")) {
        return moduleDir(m);
    }
    std::vector<std::wstring> candidates;
    const std::wstring env = envVar(L"AUDACITY_DIR");
    if (!env.empty()) {
        std::wstring lower = env;
        for (auto& c : lower) {
            c = static_cast<wchar_t>(towlower(c));
        }
        if (lower.size() > 4 && lower.substr(lower.size() - 4) == L".exe") {
            const size_t slash = env.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                candidates.push_back(env.substr(0, slash));
            }
        } else {
            candidates.push_back(env + L"\\bin");
            candidates.push_back(env);
        }
    }
    const std::wstring pf = envVar(L"ProgramFiles");
    const std::wstring pf86 = envVar(L"ProgramFiles(x86)");
    for (const std::wstring& root : { pf, std::wstring(L"C:\\Program Files"), pf86 }) {
        if (root.empty()) {
            continue;
        }
        candidates.push_back(root + L"\\Audacity 4\\bin");
        candidates.push_back(root + L"\\Audacity4\\bin");
        candidates.push_back(root + L"\\Audacity\\bin");
    }
    for (const auto& c : candidates) {
        if (dirExists(c) && fileExists(c + L"\\sndfile.dll")) {
            return c;
        }
    }
    return {};
}

HMODULE loadFrom(const std::wstring& dir, const wchar_t* name)
{
    if (HMODULE m = GetModuleHandleW(name)) {
        return m;
    }
    const std::wstring path = dir + L"\\" + name;
    return LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}

template<typename F>
bool bind(HMODULE m, const char* sym, F& out)
{
    out = reinterpret_cast<F>(GetProcAddress(m, sym));
    return out != nullptr;
}

std::once_flag gOnceSf, gOnceMpg;
Sndfile gSf;
Mpg123 gMpg;
std::wstring gBinDir;
std::once_flag gOnceDir;

const std::wstring& binDir()
{
    std::call_once(gOnceDir, [] { gBinDir = findBinDir(); });
    return gBinDir;
}

} // namespace

std::string toUtf8(const std::wstring& s)
{
    if (s.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const std::string& s)
{
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::wstring audacityBinDir()
{
    return binDir();
}

std::wstring audacityExePath()
{
    const std::wstring& d = binDir();
    if (d.empty()) {
        return {};
    }
    for (const wchar_t* exe : { L"\\Audacity4.exe", L"\\Audacity.exe" }) {
        if (fileExists(d + exe)) {
            return d + exe;
        }
    }
    return {};
}

Sndfile& sndfile()
{
    std::call_once(gOnceSf, [] {
        const std::wstring& dir = binDir();
        gSf.dir = dir;
        if (dir.empty()) {
            gSf.error = "Audacity 4 not found (sndfile.dll). Install Audacity 4 or set AUDACITY_DIR to its folder.";
            return;
        }
        HMODULE m = loadFrom(dir, L"sndfile.dll");
        if (!m) {
            gSf.error = "Could not load sndfile.dll from " + toUtf8(dir);
            return;
        }
        const bool ok = bind(m, "sf_wchar_open", gSf.wchar_open) && bind(m, "sf_close", gSf.close)
                        && bind(m, "sf_readf_float", gSf.readf_float) && bind(m, "sf_writef_float", gSf.writef_float)
                        && bind(m, "sf_seek", gSf.seek) && bind(m, "sf_command", gSf.command)
                        && bind(m, "sf_strerror", gSf.strerror) && bind(m, "sf_version_string", gSf.version_string);
        if (!ok) {
            gSf.error = "sndfile.dll is missing expected functions";
            return;
        }
        gSf.version = gSf.version_string();
        gSf.loaded = true;
    });
    return gSf;
}

Mpg123& mpg123()
{
    std::call_once(gOnceMpg, [] {
        const std::wstring& dir = binDir();
        if (dir.empty()) {
            gMpg.error = "Audacity 4 not found (mpg123.dll)";
            return;
        }
        HMODULE m = loadFrom(dir, L"mpg123.dll");
        if (!m) {
            gMpg.error = "Could not load mpg123.dll from " + toUtf8(dir);
            return;
        }
        const bool ok = bind(m, "mpg123_init", gMpg.init) && bind(m, "mpg123_new", gMpg.create)
                        && bind(m, "mpg123_delete", gMpg.destroy) && bind(m, "mpg123_format_none", gMpg.format_none)
                        && bind(m, "mpg123_format", gMpg.format) && bind(m, "mpg123_open_feed", gMpg.open_feed)
                        && bind(m, "mpg123_decode", gMpg.decode) && bind(m, "mpg123_getformat", gMpg.getformat)
                        && bind(m, "mpg123_close", gMpg.close) && bind(m, "mpg123_plain_strerror", gMpg.plain_strerror);
        if (!ok) {
            gMpg.error = "mpg123.dll is missing expected functions";
            return;
        }
        gMpg.init();
        gMpg.loaded = true;
    });
    return gMpg;
}

} // namespace aumcp
