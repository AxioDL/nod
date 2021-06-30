#ifdef __cplusplus
#include <array>
#include <cstdint>

#if !__cpp_char8_t
using char8_t = uint8_t;
#endif

#define OS_CONSTEXPR constexpr

extern "C" {
#else
#include <stdint.h>
#include <stdbool.h>

#define OS_CONSTEXPR

typedef uint8_t char8_t;
typedef uint16_t char16_t;
typedef uint32_t char32_t;
#endif

OS_CONSTEXPR inline bool IsSjisLeadByte(char8_t c) { return (c > 0x80 && c < 0xa0) || (c > 0xdf && c < 0xfd); }

OS_CONSTEXPR inline bool IsSjisTrailByte(char8_t c) { return c > 0x3f && c < 0xfd && c != 0x7f; }

char32_t OSSJISToUTF32(char16_t sjis);

char16_t OSUTF32ToSJIS(char32_t utf32);

char8_t OSUTF32ToAnsi(char32_t utf32);

char16_t* OSUTF32To16(char32_t utf32, char16_t* utf16);

char8_t* OSUTF32To8(char32_t utf32, char8_t* utf8);

const char16_t* OSUTF16To32(const char16_t* utf16, char32_t* utf32);

const char8_t* OSUTF8To32(const char8_t* utf8, char32_t* utf32);

#ifdef __cplusplus
}

class SJISToUTF8 {
private:
  std::string out;

public:
  SJISToUTF8(std::string_view sv) {
    const auto* in = reinterpret_cast<const char8_t*>(sv.data());
    const auto* end = in + sv.size();
    std::array<char8_t, 4> u8arr{};
    while (in < end) {
      if (IsSjisLeadByte(*in)) {
        char16_t sjis = static_cast<char16_t>(*in) << 8 | *(in + 1);
        in += 2;
        char32_t utf32 = OSSJISToUTF32(sjis);
        if (utf32 == 0) {
          continue;
        }
        char8_t* u8out = u8arr.data();
        char8_t* u8end = OSUTF32To8(utf32, u8out);
        if (u8end == nullptr) {
          continue;
        }
        auto length = static_cast<size_t>(u8end - u8out);
        out.append(std::string_view{reinterpret_cast<char*>(u8out), length});
      } else {
        out.push_back(static_cast<char>(*in++));
      }
    }
  }

  [[nodiscard]] const std::string& str() const { return out; }

  [[nodiscard]] std::string& str() { return out; }

  [[nodiscard]] const char* c_str() const { return out.c_str(); }
};

class UTF8ToSJIS {
private:
  std::string out;

public:
  UTF8ToSJIS(std::string_view sv) {
    const auto* in = reinterpret_cast<const char8_t*>(sv.data());
    const auto* end = in + sv.size();
    while (in < end) {
      char32_t utf32 = 0;
      const char8_t* next = OSUTF8To32(in, &utf32);
      if (next == nullptr) {
        utf32 = *in;
        in++;
      } else {
        in = next;
      }
      char16_t sjis = OSUTF32ToSJIS(utf32);
      char8_t lead = (sjis >> 8) & 0xFF;
      if (IsSjisLeadByte(lead)) {
        out.push_back(static_cast<char>(lead));
      }
      out.push_back(static_cast<char>(sjis & 0xFF));
    }
  }

  [[nodiscard]] const std::string& str() const { return out; }

  [[nodiscard]] std::string& str() { return out; }

  [[nodiscard]] const char* c_str() const { return out.c_str(); }
};
#endif
