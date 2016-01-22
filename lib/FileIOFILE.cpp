#include <stdio.h>
#include <stdlib.h>
#include "NOD/Util.hpp"
#include "NOD/IFileIO.hpp"

#if _WIN32
#define ftello _ftelli64
#define fseeko _fseeki64
#endif

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
    FileIOFILE(const SystemChar* path)
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
        uint8_t buf[0x7c00];
        FILE* fp;
        WriteStream(const SystemString& path)
        {
#if NOD_UCS2
            fp = _wfopen(path.c_str(), L"wb");
#else
            fp = fopen(path.c_str(), "wb");
#endif
            if (!fp)
                LogModule.report(LogVisor::Error, _S("unable to open '%s' for writing"), path.c_str());
        }
        WriteStream(const SystemString& path, uint64_t offset)
        {
#if NOD_UCS2
            fp = _wfopen(path.c_str(), L"ab");
            fclose(fp);
            fp = _wfopen(path.c_str(), L"r+b");
#else
            fp = fopen(path.c_str(), "ab");
            fclose(fp);
            fp = fopen(path.c_str(), "r+b");
#endif
            if (!fp)
                LogModule.report(LogVisor::Error, _S("unable to open '%s' for writing"), path.c_str());
            fseeko64(fp, offset, SEEK_SET);
        }
        ~WriteStream()
        {
            fclose(fp);
        }
        uint64_t write(const void* buf, uint64_t length)
        {
            return fwrite(buf, 1, length, fp);
        }
        uint64_t copyFromDisc(IPartReadStream& discio, uint64_t length)
        {
            uint64_t read = 0;
            while (length)
            {
                uint64_t thisSz = MIN(0x7c00, length);
                uint64_t readSz = discio.read(buf, thisSz);
                if (thisSz != readSz)
                {
                    LogModule.report(LogVisor::Error, "unable to read enough from disc");
                    return read;
                }
                if (fwrite(buf, 1, readSz, fp) != readSz)
                {
                    LogModule.report(LogVisor::Error, "unable to write in file");
                    return read;
                }
                length -= thisSz;
                read += thisSz;
            }
            return read;
        }
    };
    std::unique_ptr<IWriteStream> beginWriteStream() const
    {
        return std::unique_ptr<IWriteStream>(new WriteStream(m_path));
    }
    std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const
    {
        return std::unique_ptr<IWriteStream>(new WriteStream(m_path, offset));
    }

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
                LogModule.report(LogVisor::Error, _S("unable to open '%s' for reading"), path.c_str());
        }
        ReadStream(const SystemString& path, uint64_t offset)
        : ReadStream(path)
        {
            fseeko64(fp, offset, SEEK_SET);
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
                {
                    LogModule.report(LogVisor::Error, "unable to read enough from file");
                    return written;
                }
                if (discio.write(buf, thisSz) != thisSz)
                {
                    LogModule.report(LogVisor::Error, "unable to write enough to disc");
                    return written;
                }
                length -= thisSz;
                written += thisSz;
            }
            return written;
        }
    };
    std::unique_ptr<IReadStream> beginReadStream() const
    {
        return std::unique_ptr<IReadStream>(new ReadStream(m_path));
    }
    std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const
    {
        return std::unique_ptr<IReadStream>(new ReadStream(m_path, offset));
    }
};

std::unique_ptr<IFileIO> NewFileIO(const SystemString& path)
{
    return std::unique_ptr<IFileIO>(new FileIOFILE(path));
}

std::unique_ptr<IFileIO> NewFileIO(const SystemChar* path)
{
    return std::unique_ptr<IFileIO>(new FileIOFILE(path));
}

}
