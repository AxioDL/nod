#ifndef __NOD_IDISC_IO__
#define __NOD_IDISC_IO__

#include <memory>
#include <stdlib.h>

namespace NOD
{

class IDiscIO
{
public:
    virtual ~IDiscIO() {}

    class IReadStream
    {
    public:
        virtual size_t read(void* buf, size_t length)=0;
    };
    virtual std::unique_ptr<IReadStream> beginReadStream(size_t offset);

    class IWriteStream
    {
    public:
        virtual size_t write(void* buf, size_t length)=0;
    };
    virtual std::unique_ptr<IWriteStream> beginWriteStream(size_t offset);
};

}

#endif // __NOD_IDISC_IO__
