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

    bool exists()
    {
#if _WIN32
        HANDLE fp = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fp == INVALID_HANDLE_VALUE)
            return false;
        CloseHandle(fp);
        return true;
#else
        FILE* fp = fopen(m_path.c_str(), "rb");
        if (!fp)
            return false;
        fclose(fp);
        return true;
#endif
    }

    uint64_t size()
    {
#if _WIN32
        HANDLE fp = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fp == INVALID_HANDLE_VALUE)
            return 0;
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(fp, &sz))
        {
            CloseHandle(fp);
            return 0;
        }
        CloseHandle(fp);
        return sz.QuadPart;
#else
        FILE* fp = fopen(m_path.c_str(), "rb");
        if (!fp)
            return 0;
        FSeek(fp, 0, SEEK_END);
        uint64_t result = ftello(fp);
        fclose(fp);
        return result;
#endif
    }

    struct WriteStream : public IFileIO::IWriteStream
    {
        uint8_t buf[0x7c00];
#if _WIN32
        HANDLE fp;
#else
        FILE* fp;
#endif
        WriteStream(const SystemString& path)
        {
#if _WIN32
            fp = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE,
                             nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (fp == INVALID_HANDLE_VALUE)
                goto FailLoc;
#else
            fp = fopen(path.c_str(), "wb");
            if (!fp)
                goto FailLoc;
#endif
            return;
        FailLoc:
            LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for writing"), path.c_str());
        }
        WriteStream(const SystemString& path, uint64_t offset)
        {
#if _WIN32
            fp = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (fp == INVALID_HANDLE_VALUE)
                goto FailLoc;
            LARGE_INTEGER lioffset;
            lioffset.QuadPart = offset;
            SetFilePointerEx(fp, lioffset, nullptr, FILE_BEGIN);
#else
            fp = fopen(path.c_str(), "ab");
            if (!fp)
                goto FailLoc;
            fclose(fp);
            fp = fopen(path.c_str(), "r+b");
            if (!fp)
                goto FailLoc;
            FSeek(fp, offset, SEEK_SET);
#endif
            return;
        FailLoc:
            LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for writing"), path.c_str());
        }
        ~WriteStream()
        {
#if _WIN32
            CloseHandle(fp);
#else
            fclose(fp);
#endif
        }
        uint64_t write(const void* buf, uint64_t length)
        {
#if _WIN32
            DWORD ret = 0;
            WriteFile(fp, buf, length, &ret, nullptr);
            return ret;
#else
            return fwrite(buf, 1, length, fp);
#endif
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
#if _WIN32
        HANDLE fp;
#else
        FILE* fp;
#endif
        uint8_t buf[0x7c00];
        ReadStream(const SystemString& path)
        {
#if _WIN32
            fp = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (fp == INVALID_HANDLE_VALUE)
                goto FailLoc;
#else
            fp = fopen(path.c_str(), "rb");
            if (!fp)
                goto FailLoc;
#endif
            return;
        FailLoc:
            LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for reading"), path.c_str());
        }
        ReadStream(const SystemString& path, uint64_t offset)
        : ReadStream(path)
        {
#if _WIN32
            LARGE_INTEGER lioffset;
            lioffset.QuadPart = offset;
            SetFilePointerEx(fp, lioffset, nullptr, FILE_BEGIN);
#else
            FSeek(fp, offset, SEEK_SET);
#endif
        }
        ~ReadStream()
        {
#if _WIN32
            CloseHandle(fp);
#else
            fclose(fp);
#endif
        }
        void seek(int64_t offset, int whence)
        {
#if _WIN32
            LARGE_INTEGER li;
            li.QuadPart = offset;
            SetFilePointerEx(fp, li, nullptr, whence);
#else
            FSeek(fp, offset, whence);
#endif
        }
        uint64_t read(void* buf, uint64_t length)
        {
#if _WIN32
            DWORD ret = 0;
            ReadFile(fp, buf, length, &ret, nullptr);
            return ret;
#else
            return fread(buf, 1, length, fp);
#endif
        }
        uint64_t copyToDisc(IPartWriteStream& discio, uint64_t length)
        {
            uint64_t written = 0;
            while (length)
            {
                uint64_t thisSz = MIN(0x7c00, length);
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
