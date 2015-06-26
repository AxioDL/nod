#include <stdio.h>
#include <stdexcept>
#include "IDiscIO.hpp"

namespace NOD
{

class DiscIOFILE : public IDiscIO
{
    std::string filepath;
public:
    DiscIOFILE(const std::string& fpin)
    : filepath(fpin) {}

    class FILEReadStream : public IReadStream
    {
        friend class DiscIOFILE;
        FILE* fp;
        FILEReadStream(FILE* fpin)
        : fp(fpin) {}
        ~FILEReadStream() {fclose(fp);}
    public:
        size_t read(void* buf, size_t length)
        {return fread(buf, 1, length, fp);}
    };
    std::unique_ptr<IReadStream> beginReadStream(size_t offset)
    {
        FILE* fp = fopen(filepath.c_str(), "rb");
        if (!fp)
        {
            throw std::runtime_error("Unable to open '" + filepath + "' for reading");
            return std::unique_ptr<IReadStream>();
        }
        fseek(fp, offset, SEEK_SET);
        return std::unique_ptr<IReadStream>(new FILEReadStream(fp));
    }

    class FILEWriteStream : public IWriteStream
    {
        friend class DiscIOFILE;
        FILE* fp;
        FILEWriteStream(FILE* fpin)
        : fp(fpin) {}
        ~FILEWriteStream() {fclose(fp);}
    public:
        size_t write(void* buf, size_t length)
        {return fwrite(buf, 1, length, fp);}
    };
    std::unique_ptr<IWriteStream> beginWriteStream(size_t offset)
    {
        FILE* fp = fopen(filepath.c_str(), "wb");
        if (!fp)
        {
            throw std::runtime_error("Unable to open '" + filepath + "' for writing");
            return std::unique_ptr<IWriteStream>();
        }
        fseek(fp, offset, SEEK_SET);
        return std::unique_ptr<IWriteStream>(new FILEWriteStream(fp));
    }
};

std::unique_ptr<IDiscIO> NewDiscIOFILE(const char* path)
{
    return std::unique_ptr<IDiscIO>(new DiscIOFILE(path));
}

}

