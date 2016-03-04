#include <stdio.h>
#include "nod/Util.hpp"
#include "nod/IDiscIO.hpp"
#include "nod/IFileIO.hpp"

namespace nod
{

class DiscIOISO : public IDiscIO
{
    std::unique_ptr<IFileIO> m_fio;
public:
    DiscIOISO(const SystemString& fpin)
    : m_fio(std::move(NewFileIO(fpin))) {}

    class ReadStream : public IReadStream
    {
        friend class DiscIOISO;
        std::unique_ptr<IFileIO::IReadStream> fp;
        ReadStream(std::unique_ptr<IFileIO::IReadStream>&& fpin)
        : fp(std::move(fpin)) {}
    public:
        uint64_t read(void* buf, uint64_t length)
        {return fp->read(buf, length);}
        uint64_t position() const
        {return fp->position();}
        void seek(int64_t offset, int whence)
        {fp->seek(offset, whence);}
    };

    std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const
    {
        return std::unique_ptr<IReadStream>(new ReadStream(std::move(m_fio->beginReadStream(offset))));
    }

    class WriteStream : public IWriteStream
    {
        friend class DiscIOISO;
        std::unique_ptr<IFileIO::IWriteStream> fp;
        WriteStream(std::unique_ptr<IFileIO::IWriteStream>&& fpin)
        : fp(std::move(fpin)) {}
    public:
        uint64_t write(const void* buf, uint64_t length)
        {
            return fp->write(buf, length);
        }
    };

    std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const
    {
        return std::unique_ptr<IWriteStream>(new WriteStream(std::move(m_fio->beginWriteStream(offset))));
    }
};

std::unique_ptr<IDiscIO> NewDiscIOISO(const SystemChar* path)
{
    return std::unique_ptr<IDiscIO>(new DiscIOISO(path));
}

}

