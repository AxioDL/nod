#include <stdio.h>
#include <stdlib.h>
#include <stdexcept>
#include "NOD/Util.hpp"
#include "NOD/IFileIO.hpp"

/* Macros for min/max */
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

namespace NOD
{

class FileIOFILE : public IFileIO
{
    SystemString m_path;
public:
    FileIOFILE(const SystemString& path)
    : m_path(path) {}

    uint64_t size()
    {
#if NOD_UCS2
        FILE* fp = _wfopen(m_path.c_str(), L"rb");
#else
        FILE* fp = fopen(m_path.c_str(), "rb");
#endif
        if (!fp)
            return 0;
        fseeko(fp, 0, SEEK_END);
        uint64_t result = ftello(fp);
        fclose(fp);
        return result;
    }

    struct WriteStream : public IFileIO::IWriteStream
    {
        FILE* fp;
        uint8_t buf[0x7c00];
        WriteStream(const SystemString& path)
        {
#if NOD_UCS2
            fp = _wfopen(path.c_str(), L"wb");
#else
            fp = fopen(path.c_str(), "wb");
#endif
            if (!fp)
            {
#if NOD_UCS2
                throw std::runtime_error("Unable to open '" + WideToUTF8(path) + "' for writing");
#else
                throw std::runtime_error("Unable to open '" + path + "' for writing");
#endif
            }
        }
        ~WriteStream() {fclose(fp);}
        uint64_t write(void* buf, uint64_t length)
        {return fwrite(buf, 1, length, fp);}
        uint64_t copyFromDisc(IPartReadStream& discio, uint64_t length)
        {
            uint64_t read = 0;
            while (length)
            {
                uint64_t thisSz = MIN(0x7c00, length);
                uint64_t readSz = discio.read(buf, thisSz);
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
        ReadStream(const SystemString& path)
        {
#if NOD_UCS2
            fp = _wfopen(path.c_str(), L"rb");
#else
            fp = fopen(path.c_str(), "rb");
#endif
            if (!fp)
            {
#if NOD_UCS2
                throw std::runtime_error("Unable to open '" + WideToUTF8(path) + "' for reading");
#else
                throw std::runtime_error("Unable to open '" + path + "' for reading");
#endif
            }
        }
        ~ReadStream() {fclose(fp);}
        uint64_t read(void* buf, uint64_t length)
        {return fread(buf, 1, length, fp);}
        uint64_t copyToDisc(IPartWriteStream& discio, uint64_t length)
        {
            uint64_t written = 0;
            while (length)
            {
                uint64_t thisSz = MIN(0x7c00, length);
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

std::unique_ptr<IFileIO> NewFileIO(const SystemString& path)
{
    return std::unique_ptr<IFileIO>(new FileIOFILE(path));
}

}
