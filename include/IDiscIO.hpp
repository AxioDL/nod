#ifndef __NOD_IDISC_IO__
#define __NOD_IDISC_IO__

#include <memory>
#include <stdlib.h>
#include <stdio.h>

namespace NOD
{

class IDiscIO
{
public:
    virtual ~IDiscIO() {}

    struct IReadStream
    {
        virtual size_t read(void* buf, size_t length)=0;
        virtual void seek(size_t offset, int whence=SEEK_SET)=0;
    };
    virtual std::unique_ptr<IReadStream> beginReadStream(size_t offset=0) const=0;

    struct IWriteStream
    {
        virtual size_t write(void* buf, size_t length)=0;
    };
    virtual std::unique_ptr<IWriteStream> beginWriteStream(size_t offset=0) const=0;
};

struct IPartReadStream
{
    virtual void seek(size_t offset, int whence=SEEK_SET)=0;
    virtual size_t read(void* buf, size_t length)=0;
};

struct IPartWriteStream
{
    virtual size_t write(void* buf, size_t length)=0;
};

}

#endif // __NOD_IDISC_IO__
