#include <stdio.h>
#include <stdexcept>
#include "IDiscIO.hpp"

namespace NOD
{

class DiscIOISO : public IDiscIO
{
    std::string filepath;
public:
    DiscIOISO(const std::string& fpin)
    : filepath(fpin) {}

    class ReadStream : public IReadStream
    {
        friend class DiscIOISO;
        FILE* fp;
        ReadStream(FILE* fpin)
        : fp(fpin) {}
        ~ReadStream() {fclose(fp);}
    public:
        size_t read(void* buf, size_t length)
        {return fread(buf, 1, length, fp);}
        void seek(size_t offset, int whence)
        {fseek(fp, offset, whence);}
    };
    std::unique_ptr<IReadStream> beginReadStream(size_t offset) const
    {
        FILE* fp = fopen(filepath.c_str(), "rb");
        if (!fp)
        {
            throw std::runtime_error("Unable to open '" + filepath + "' for reading");
            return std::unique_ptr<IReadStream>();
        }
        fseek(fp, offset, SEEK_SET);
        return std::unique_ptr<IReadStream>(new ReadStream(fp));
    }

    class WriteStream : public IWriteStream
    {
        friend class DiscIOISO;
        FILE* fp;
        WriteStream(FILE* fpin)
        : fp(fpin) {}
        ~WriteStream() {fclose(fp);}
    public:
        size_t write(void* buf, size_t length)
        {return fwrite(buf, 1, length, fp);}
    };
    std::unique_ptr<IWriteStream> beginWriteStream(size_t offset) const
    {
        FILE* fp = fopen(filepath.c_str(), "wb");
        if (!fp)
        {
            throw std::runtime_error("Unable to open '" + filepath + "' for writing");
            return std::unique_ptr<IWriteStream>();
        }
        fseek(fp, offset, SEEK_SET);
        return std::unique_ptr<IWriteStream>(new WriteStream(fp));
    }
};

std::unique_ptr<IDiscIO> NewDiscIOISO(const char* path)
{
    return std::unique_ptr<IDiscIO>(new DiscIOISO(path));
}

}

