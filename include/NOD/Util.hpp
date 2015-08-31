#ifndef __NOD_UTIL_HPP__
#define __NOD_UTIL_HPP__

#if _WIN32 && UNICODE
#include <wctype.h>
#include <direct.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#endif
#else
#include <ctype.h>
#endif
#include <sys/stat.h>

#include <string>
#include <algorithm>
#include <LogVisor/LogVisor.hpp>

namespace NOD
{

/* Log Module */
extern LogVisor::LogModule LogModule;

/* filesystem char type */
#if _WIN32 && UNICODE
#define NOD_UCS2 1
typedef struct _stat Sstat;
static inline int Mkdir(const wchar_t* path, int) {return _wmkdir(path);}
static inline int Stat(const wchar_t* path, Sstat* statout) {return _wstat(path, statout);}
#else
typedef struct stat Sstat;
static inline int Mkdir(const char* path, mode_t mode) {return mkdir(path, mode);}
static inline int Stat(const char* path, Sstat* statout) {return stat(path, statout);}
#endif

/* String-converting views */
#if NOD_UCS2
typedef wchar_t SystemChar;
typedef std::wstring SystemString;
static inline void ToLower(SystemString& str)
{std::transform(str.begin(), str.end(), str.begin(), towlower);}
static inline void ToUpper(SystemString& str)
{std::transform(str.begin(), str.end(), str.begin(), towupper);}
class SystemUTF8View
{
    std::string m_utf8;
public:
    SystemUTF8View(const SystemString& str)
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), str.size(), nullptr, 0, nullptr, nullptr);
        m_utf8.assign('\0', len);
        WideCharToMultiByte(CP_UTF8, 0, str.c_str(), str.size(), &m_utf8[0], len, nullptr, nullptr);
    }
    inline const std::string& utf8_str() {return m_utf8;}
};
class SystemStringView
{
    std::wstring m_sys;
public:
    SystemStringView(const std::string& str)
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.size(), nullptr, 0);
        m_sys.assign(L'\0', len);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.size(), &m_sys[0], len);
    }
    inline const std::wstring& sys_str() {return m_sys;}
};
#ifndef _S
#define _S(val) L ## val
#endif
#else
typedef char SystemChar;
typedef std::string SystemString;
static inline void ToLower(SystemString& str)
{std::transform(str.begin(), str.end(), str.begin(), tolower);}
static inline void ToUpper(SystemString& str)
{std::transform(str.begin(), str.end(), str.begin(), toupper);}
class SystemUTF8View
{
    const std::string& m_utf8;
public:
    SystemUTF8View(const SystemString& str)
    : m_utf8(str) {}
    inline const std::string& utf8_str() {return m_utf8;}
};
class SystemStringView
{
    const std::string& m_sys;
public:
    SystemStringView(const std::string& str)
    : m_sys(str) {}
    inline const std::string& sys_str() {return m_sys;}
};
#ifndef _S
#define _S(val) val
#endif
#endif

/* Type-sensitive byte swappers */
template <typename T>
static inline T bswap16(T val)
{
#if __GNUC__
    return __builtin_bswap16(val);
#elif _WIN32
    return _byteswap_ushort(val);
#else
    return (val = (val << 8) | ((val >> 8) & 0xFF));
#endif
}

template <typename T>
static inline T bswap32(T val)
{
#if __GNUC__
    return __builtin_bswap32(val);
#elif _WIN32
    return _byteswap_ulong(val);
#else
    val = (val & 0x0000FFFF) << 16 | (val & 0xFFFF0000) >> 16;
    val = (val & 0x00FF00FF) << 8 | (val & 0xFF00FF00) >> 8;
    return val;
#endif
}

template <typename T>
static inline T bswap64(T val)
{
#if __GNUC__
    return __builtin_bswap64(val);
#elif _WIN32
    return _byteswap_uint64(val);
#else
    return ((val & 0xFF00000000000000ULL) >> 56) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x000000FF00000000ULL) >>  8) |
           ((val & 0x00000000FF000000ULL) <<  8) |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x00000000000000FFULL) << 56);
#endif
}


#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static inline int16_t SBig(int16_t val) {return bswap16(val);}
static inline uint16_t SBig(uint16_t val) {return bswap16(val);}
static inline int32_t SBig(int32_t val) {return bswap32(val);}
static inline uint32_t SBig(uint32_t val) {return bswap32(val);}
static inline int64_t SBig(int64_t val) {return bswap64(val);}
static inline uint64_t SBig(uint64_t val) {return bswap64(val);}

static inline int16_t SLittle(int16_t val) {return val;}
static inline uint16_t SLittle(uint16_t val) {return val;}
static inline int32_t SLittle(int32_t val) {return val;}
static inline uint32_t SLittle(uint32_t val) {return val;}
static inline int64_t SLittle(int64_t val) {return val;}
static inline uint64_t SLittle(uint64_t val) {return val;}
#else
static inline int16_t SLittle(int16_t val) {return bswap16(val);}
static inline uint16_t SLittle(uint16_t val) {return bswap16(val);}
static inline int32_t SLittle(int32_t val) {return bswap32(val);}
static inline uint32_t SLittle(uint32_t val) {return bswap32(val);}
static inline int64_t SLittle(int64_t val) {return bswap64(val);}
static inline uint64_t SLittle(uint64_t val) {return bswap64(val);}

static inline int16_t SBig(int16_t val) {return val;}
static inline uint16_t SBig(uint16_t val) {return val;}
static inline int32_t SBig(int32_t val) {return val;}
static inline uint32_t SBig(uint32_t val) {return val;}
static inline int64_t SBig(int64_t val) {return val;}
static inline uint64_t SBig(uint64_t val) {return val;}
#endif

}

#endif // __NOD_UTIL_HPP__
