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
        virtual size_t copyFromDisc(IDiscIO::IReadStream& discio, size_t length)=0;
    };
    virtual std::unique_ptr<IWriteStream> beginWriteStream();

    struct IReadStream
    {
        virtual ~IReadStream() {}
        virtual size_t copyToDisc(IDiscIO::IWriteStream& discio, size_t length)=0;
    };
    virtual std::unique_ptr<IReadStream> beginReadStream();
};

}

#endif // __NOD_IFILE_IO__
