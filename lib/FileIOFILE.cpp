#include <stdio.h>
#include <stdlib.h>
#include "NOD/Util.hpp"
#include "NOD/IFileIO.hpp"

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

    bool exists()
    {
        FILE* fp = fopen(m_path.c_str(), "rb");
        if (!fp)
            return false;
        fclose(fp);
        return true;
    }

    uint64_t size()
    {
        FILE* fp = fopen(m_path.c_str(), "rb");
        if (!fp)
            return 0;
        FSeek(fp, 0, SEEK_END);
        uint64_t result = FTell(fp);
        fclose(fp);
        return result;
    }

    struct WriteStream : public IFileIO::IWriteStream
    {
        FILE* fp;
        WriteStream(const SystemString& path)
        {
            fp = fopen(path.c_str(), "wb");
            if (!fp)
                LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for writing"), path.c_str());
        }
        WriteStream(const SystemString& path, uint64_t offset)
        {
            fp = fopen(path.c_str(), "ab");
            if (!fp)
                goto FailLoc;
            fclose(fp);
            fp = fopen(path.c_str(), "r+b");
            if (!fp)
                goto FailLoc;
            FSeek(fp, offset, SEEK_SET);
            return;
        FailLoc:
            LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for writing"), path.c_str());
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
            uint8_t buf[0x7c00];
            while (length)
            {
                uint64_t thisSz = NOD::min(uint64_t(0x7c00), length);
                uint64_t readSz = discio.read(buf, thisSz);
                if (thisSz != readSz)
                {
                    LogModule.report(LogVisor::FatalError, "unable to read enough from disc");
                    return read;
                }
                if (write(buf, readSz) != readSz)
                {
                    LogModule.report(LogVisor::FatalError, "unable to write in file");
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
        ReadStream(const SystemString& path)
        {
            fp = fopen(path.c_str(), "rb");
            if (!fp)
                LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for reading"), path.c_str());
        }
        ReadStream(const SystemString& path, uint64_t offset)
        : ReadStream(path)
        {
            FSeek(fp, offset, SEEK_SET);
        }
        ~ReadStream()
        {
            fclose(fp);
        }
        void seek(int64_t offset, int whence)
        {
            FSeek(fp, offset, whence);
        }
        int64_t position()
        {
            return FTell(fp);
        }
        uint64_t read(void* buf, uint64_t length)
        {
            return fread(buf, 1, length, fp);
        }
        uint64_t copyToDisc(IPartWriteStream& discio, uint64_t length)
        {
            uint64_t written = 0;
            uint8_t buf[0x7c00];
            while (length)
            {
                uint64_t thisSz = NOD::min(uint64_t(0x7c00), length);
                if (read(buf, thisSz) != thisSz)
                {
                    LogModule.report(LogVisor::FatalError, "unable to read enough from file");
                    return written;
                }
                if (discio.write(buf, thisSz) != thisSz)
                {
                    LogModule.report(LogVisor::FatalError, "unable to write enough to disc");
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
