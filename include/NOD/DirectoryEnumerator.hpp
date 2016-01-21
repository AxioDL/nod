#ifndef __NOD_DIRECTORY_ENUMERATOR__
#define __NOD_DIRECTORY_ENUMERATOR__

#include "Util.hpp"

namespace NOD
{

class DirectoryEnumerator
{
public:
    enum class Mode
    {
        Native,
        DirsSorted,
        FilesSorted,
        DirsThenFilesSorted
    };
    struct Entry
    {
        SystemString m_path;
        SystemString m_name;
        size_t m_fileSz;
        bool m_isDir;

    private:
        friend class DirectoryEnumerator;
        Entry(SystemString&& path, const SystemChar* name, size_t sz, bool isDir)
        : m_path(std::move(path)), m_name(name), m_fileSz(sz), m_isDir(isDir) {}
    };

private:
    std::vector<Entry> m_entries;

public:
    DirectoryEnumerator(const SystemString& path, Mode mode=Mode::DirsThenFilesSorted,
                        bool sizeSort=false, bool reverse=false, bool noHidden=false)
    : DirectoryEnumerator(path.c_str(), mode, sizeSort, reverse, noHidden) {}
    DirectoryEnumerator(const SystemChar* path, Mode mode=Mode::DirsThenFilesSorted,
                        bool sizeSort=false, bool reverse=false, bool noHidden=false);

    operator bool() const {return m_entries.size() != 0;}
    size_t size() const {return m_entries.size();}
    std::vector<Entry>::const_iterator begin() const {return m_entries.cbegin();}
    std::vector<Entry>::const_iterator end() const {return m_entries.cend();}
};

}

#endif // __NOD_DIRECTORY_ENUMERATOR__
