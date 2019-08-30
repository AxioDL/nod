#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <vector>

#include "nod/Util.hpp"

namespace nod {

struct CaseInsensitiveCompare {
  bool operator()(std::string_view lhs, std::string_view rhs) const {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](char lhs, char rhs) {
      return std::tolower(static_cast<unsigned char>(lhs)) < std::tolower(static_cast<unsigned char>(rhs));
    });
  }

#if _WIN32
  bool operator()(std::wstring_view lhs, std::wstring_view rhs) const {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](wchar_t lhs, wchar_t rhs) {
      return std::towlower(lhs) < std::towlower(rhs);
    });
  }
#endif
};

class DirectoryEnumerator {
public:
  enum class Mode { Native, DirsSorted, FilesSorted, DirsThenFilesSorted };
  struct Entry {
    SystemString m_path;
    SystemString m_name;
    size_t m_fileSz;
    bool m_isDir;

    Entry(const SystemString& path, const SystemChar* name, size_t sz, bool isDir)
    : m_path(path), m_name(name), m_fileSz(sz), m_isDir(isDir) {}
  };

private:
  std::vector<Entry> m_entries;

public:
  DirectoryEnumerator(SystemStringView path, Mode mode = Mode::DirsThenFilesSorted, bool sizeSort = false,
                      bool reverse = false, bool noHidden = false);

  operator bool() const { return m_entries.size() != 0; }
  size_t size() const { return m_entries.size(); }
  std::vector<Entry>::const_iterator begin() const { return m_entries.cbegin(); }
  std::vector<Entry>::const_iterator end() const { return m_entries.cend(); }
};

} // namespace nod
