#ifndef __NOD_IDISC_IO__
#define __NOD_IDISC_IO__

#include <memory>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

namespace NOD
{

class IDiscIO
{
public:
    virtual ~IDiscIO() {}

    struct IReadStream
    {
        virtual uint64_t read(void* buf, uint64_t length)=0;
        virtual void seek(int64_t offset, int whence=SEEK_SET)=0;
    };
    virtual std::unique_ptr<IReadStream> beginReadStream(uint64_t offset=0) const=0;

    struct IWriteStream
    {
        virtual uint64_t write(void* buf, uint64_t length)=0;
    };
    virtual std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset=0) const=0;
};

struct IPartReadStream
{
    virtual void seek(int64_t offset, int whence=SEEK_SET)=0;
    virtual uint64_t read(void* buf, uint64_t length)=0;
};

struct IPartWriteStream
{
    virtual uint64_t write(void* buf, uint64_t length)=0;
};

}

#endif // __NOD_IDISC_IO__
