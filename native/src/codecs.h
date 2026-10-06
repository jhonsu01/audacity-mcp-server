// Runtime binding to the codec libraries that ship with Audacity 4 (libsndfile and libmpg123).
// Nothing is linked at build time: the DLLs are loaded from Audacity's own bin folder, so the
// engine reads and writes exactly the formats the installed Audacity supports.
#pragma once

#include <cstdint>
#include <string>

namespace aumcp {

using sf_count_t = int64_t;

struct SF_INFO {
    sf_count_t frames;
    int samplerate;
    int channels;
    int format;
    int sections;
    int seekable;
};

struct SF_FORMAT_INFO {
    int format;
    const char* name;
    const char* extension;
};

struct SNDFILE;

// libsndfile constants (sndfile.h, 1.2.x)
enum : int {
    SF_FORMAT_WAV = 0x010000,
    SF_FORMAT_AIFF = 0x020000,
    SF_FORMAT_AU = 0x030000,
    SF_FORMAT_W64 = 0x0B0000,
    SF_FORMAT_FLAC = 0x170000,
    SF_FORMAT_CAF = 0x180000,
    SF_FORMAT_OGG = 0x200000,
    SF_FORMAT_RF64 = 0x220000,

    SF_FORMAT_PCM_S8 = 0x0001,
    SF_FORMAT_PCM_16 = 0x0002,
    SF_FORMAT_PCM_24 = 0x0003,
    SF_FORMAT_PCM_32 = 0x0004,
    SF_FORMAT_PCM_U8 = 0x0005,
    SF_FORMAT_FLOAT = 0x0006,
    SF_FORMAT_DOUBLE = 0x0007,
    SF_FORMAT_VORBIS = 0x0060,
    SF_FORMAT_OPUS = 0x0064,

    SF_FORMAT_SUBMASK = 0x0000FFFF,
    SF_FORMAT_TYPEMASK = 0x0FFF0000,

    SFM_READ = 0x10,
    SFM_WRITE = 0x20,

    SFC_GET_FORMAT_INFO = 0x1028,
    SFC_GET_FORMAT_MAJOR_COUNT = 0x1030,
    SFC_GET_FORMAT_MAJOR = 0x1031,
    SFC_SET_CLIPPING = 0x10C0,
    SFC_SET_VBR_ENCODING_QUALITY = 0x1300,
    SFC_SET_COMPRESSION_LEVEL = 0x1301,
    SFC_SET_STRING = 0x1020,
    SF_STR_SOFTWARE = 0x05,
};

struct Sndfile {
    bool loaded = false;
    std::string error;
    std::wstring dir;
    std::string version;

    SNDFILE* (*wchar_open)(const wchar_t*, int, SF_INFO*) = nullptr;
    int (*close)(SNDFILE*) = nullptr;
    sf_count_t (*readf_float)(SNDFILE*, float*, sf_count_t) = nullptr;
    sf_count_t (*writef_float)(SNDFILE*, const float*, sf_count_t) = nullptr;
    sf_count_t (*seek)(SNDFILE*, sf_count_t, int) = nullptr;
    int (*command)(SNDFILE*, int, void*, int) = nullptr;
    const char* (*strerror)(SNDFILE*) = nullptr;
    const char* (*version_string)() = nullptr;
};

// mpg123 constants (mpg123.h, 1.32.x)
enum : int {
    MPG123_OK = 0,
    MPG123_ERR = -1,
    MPG123_NEED_MORE = -10,
    MPG123_NEW_FORMAT = -11,
    MPG123_DONE = -12,
    MPG123_MONO = 1,
    MPG123_STEREO = 2,
    MPG123_ENC_SIGNED_16 = 0xD0,
    MPG123_ENC_FLOAT_32 = 0x200,
};

struct mpg123_handle;

struct Mpg123 {
    bool loaded = false;
    std::string error;

    int (*init)() = nullptr;
    mpg123_handle* (*create)(const char*, int*) = nullptr;
    void (*destroy)(mpg123_handle*) = nullptr;
    int (*format_none)(mpg123_handle*) = nullptr;
    int (*format)(mpg123_handle*, long, int, int) = nullptr;
    int (*open_feed)(mpg123_handle*) = nullptr;
    int (*decode)(mpg123_handle*, const unsigned char*, size_t, void*, size_t, size_t*) = nullptr;
    int (*getformat)(mpg123_handle*, long*, int*, int*) = nullptr;
    int (*close)(mpg123_handle*) = nullptr;
    const char* (*plain_strerror)(int) = nullptr;
};

// Folder that holds Audacity's codec DLLs. Order: DLLs already loaded in this process (when running
// inside Audacity), AUDACITY_DIR (folder or Audacity4.exe), default install folders.
std::wstring audacityBinDir();
std::wstring audacityExePath();

Sndfile& sndfile();
Mpg123& mpg123();

std::string toUtf8(const std::wstring& s);
std::wstring fromUtf8(const std::string& s);

} // namespace aumcp
