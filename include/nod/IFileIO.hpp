#ifndef __NOD_IFILE_IO__
#define __NOD_IFILE_IO__

#include <memory>
#include <stdlib.h>
#include "IDiscIO.hpp"
#include "Util.hpp"

namespace nod
{

class IFileIO
{
public:
    virtual ~IFileIO() {}
    virtual bool exists()=0;
    virtual uint64_t size()=0;

    struct IWriteStream
    {
        virtual ~IWriteStream() {}
        virtual uint64_t write(const void* buf, uint64_t length)=0;
        virtual uint64_t copyFromDisc(struct IPartReadStream& discio, uint64_t length)=0;
    };
    virtual std::unique_ptr<IWriteStream> beginWriteStream() const=0;
    virtual std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const=0;

    struct IReadStream
    {
        virtual ~IReadStream() {}
        virtual void seek(int64_t offset, int whence)=0;
        virtual int64_t position()=0;
        virtual uint64_t read(void* buf, uint64_t length)=0;
        virtual uint64_t copyToDisc(struct IPartWriteStream& discio, uint64_t length)=0;
    };
    virtual std::unique_ptr<IReadStream> beginReadStream() const=0;
    virtual std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const=0;
};

std::unique_ptr<IFileIO> NewFileIO(const SystemString& path, int64_t maxWriteSize=-1);
std::unique_ptr<IFileIO> NewFileIO(const SystemChar* path, int64_t maxWriteSize=-1);

}

#endif // __NOD_IFILE_IO__
