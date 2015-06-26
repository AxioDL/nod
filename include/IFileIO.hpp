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

    class IWriteStream
    {
    public:
        virtual ~IWriteStream() {}
        virtual size_t copyFromDisc(IDiscIO::IReadStream& discio, size_t length)=0;
    };
    virtual std::unique_ptr<IWriteStream> beginWriteStream();

    class IReadStream
    {
    public:
        virtual ~IReadStream() {}
        virtual size_t copyToDisc(IDiscIO::IWriteStream& discio, size_t length)=0;
    };
    virtual std::unique_ptr<IReadStream> beginReadStream();
};

}

#endif // __NOD_IFILE_IO__
