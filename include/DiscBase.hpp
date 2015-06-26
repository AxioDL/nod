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
protected:
    struct Partition
    {
        enum Kind
        {
            PART_DATA,
            PART_UPDATE
        };
        struct File
        {
            std::unique_ptr<IFileIO> m_hddFile;
            std::string m_discPath;
            size_t m_discOffset;
            size_t m_discLength;
            File(const std::string& discPath)
            : m_discPath(discPath) {}
        };
        std::list<File> files;
        Kind m_kind;
        Partition(Kind kind)
        : m_kind(kind) {}
    };
    std::list<Partition> partitions;
    Partition& addPartition(Partition::Kind kind);
public:
    DiscBase(IDiscIO& dio);
    virtual bool sync()=0;

    class IPartReadStream
    {
    public:
        virtual size_t read(void* buf, size_t length)=0;
    };
    virtual std::unique_ptr<IPartReadStream> beginDataReadStream()=0;
    virtual std::unique_ptr<IPartReadStream> beginUpdateReadStream()=0;

    class IPartWriteStream
    {
    public:
        virtual size_t write(void* buf, size_t length)=0;
    };
    virtual std::unique_ptr<IPartWriteStream> beginDataWriteStream()=0;
    virtual std::unique_ptr<IPartWriteStream> beginUpdateWriteStream()=0;

};

}

#endif // __NOD_DISC_BASE__
