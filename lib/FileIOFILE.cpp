#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdexcept>
#include "IFileIO.hpp"

/* Macros for min/max */
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

namespace NOD
{

class FileIOFILE : public IFileIO
{
    std::string m_path;
public:
    FileIOFILE(const std::string& path)
    : m_path(path) {}

    size_t size()
    {
        FILE* fp = fopen(m_path.c_str(), "rb");
        if (!fp)
            return 0;
        fseek(fp, 0, SEEK_END);
        size_t result = ftell(fp);
        fclose(fp);
        return result;
    }

    struct WriteStream : public IFileIO::IWriteStream
    {
        FILE* fp;
        uint8_t buf[0x7c00];
        WriteStream(const std::string& path)
        {
            fp = fopen(path.c_str(), "wb");
            if (!fp)
                throw std::runtime_error("unable to open '" + path + "' for writing");
        }
        ~WriteStream() {fclose(fp);}
        size_t copyFromDisc(IPartReadStream& discio, size_t length)
        {
            size_t read = 0;
            while (length)
            {
                size_t thisSz = MIN(0x7c00, length);
                size_t readSz = discio.read(buf, thisSz);
                if (thisSz != readSz)
                    throw std::runtime_error("unable to read enough from disc");
                if (fwrite(buf, 1, readSz, fp) != readSz)
                    throw std::runtime_error("unable to write in file");
                length -= thisSz;
                read += thisSz;
            }
            return read;
        }
    };
    std::unique_ptr<IWriteStream> beginWriteStream() const
    {return std::unique_ptr<IWriteStream>(new WriteStream(m_path));}

    struct ReadStream : public IFileIO::IReadStream
    {
        FILE* fp;
        uint8_t buf[0x7c00];
        ReadStream(const std::string& path)
        {
            fp = fopen(path.c_str(), "rb");
            if (!fp)
                throw std::runtime_error("unable to open '" + path + "' for reading");
        }
        ~ReadStream() {fclose(fp);}
        size_t copyToDisc(IPartWriteStream& discio, size_t length)
        {
            size_t written = 0;
            while (length)
            {
                size_t thisSz = MIN(0x7c00, length);
                if (fread(buf, 1, thisSz, fp) != thisSz)
                    throw std::runtime_error("unable to read enough from file");
                if (discio.write(buf, thisSz) != thisSz)
                    throw std::runtime_error("unable to write enough to disc");
                length -= thisSz;
                written += thisSz;
            }
            return written;
        }
    };
    std::unique_ptr<IReadStream> beginReadStream() const
    {return std::unique_ptr<IReadStream>(new ReadStream(m_path));}
};

std::unique_ptr<IFileIO> NewFileIO(const std::string& path)
{
    return std::unique_ptr<IFileIO>(new FileIOFILE(path));
}

}
