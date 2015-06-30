#ifndef __NOD_IFILE_IO__
#define __NOD_IFILE_IO__

#include <memory>
#include <stdlib.h>
#include "IDiscIO.hpp"

namespace NOD
{

class IFileIO
{
public:
    virtual ~IFileIO() {}
    virtual size_t size()=0;

    struct IWriteStream
    {
        virtual ~IWriteStream() {}
        virtual size_t copyFromDisc(struct IPartReadStream& discio, size_t length)=0;
    };
    virtual std::unique_ptr<IWriteStream> beginWriteStream() const=0;

    struct IReadStream
    {
        virtual ~IReadStream() {}
        virtual size_t copyToDisc(struct IPartWriteStream& discio, size_t length)=0;
    };
    virtual std::unique_ptr<IReadStream> beginReadStream() const=0;
};

std::unique_ptr<IFileIO> NewFileIO(const std::string& path);
std::unique_ptr<IFileIO> NewMemIO(void* buf, size_t size);

}

#endif // __NOD_IFILE_IO__
