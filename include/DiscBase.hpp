#ifndef __NOD_DISC_BASE__
#define __NOD_DISC_BASE__

#include <list>
#include <memory>
#include <string>
#include "IDiscIO.hpp"
#include "IFileIO.hpp"

namespace NOD
{

class DiscBase
{
    IDiscIO& m_discIO;

public:
    class IPartReadStream
    {
    public:
        virtual size_t read(void* buf, size_t length)=0;
    };

    class IPartWriteStream
    {
    public:
        virtual size_t write(void* buf, size_t length)=0;
    };

protected:
    class Partition
    {
    public:
        enum Kind
        {
            PART_DATA,
            PART_UPDATE
        };
        class File
        {
            const Partition& m_parent;
            std::unique_ptr<IFileIO> m_hddFile;
            std::string m_discPath;
            size_t m_discOffset;
            size_t m_discLength;
        public:
            File(const Partition& parent, const std::string& discPath)
            : m_parent(parent), m_discPath(discPath) {}
            std::unique_ptr<IPartReadStream> beginReadStream();
        };
    private:
        std::list<File> files;
        Kind m_kind;
    public:
        Partition(Kind kind)
        : m_kind(kind) {}
        std::unique_ptr<IPartReadStream> beginReadStream(size_t offset);
    };
    std::list<Partition> partitions;
    Partition& addPartition(Partition::Kind kind);
public:
    DiscBase(IDiscIO& dio);
    virtual bool sync()=0;

};

}

#endif // __NOD_DISC_BASE__
