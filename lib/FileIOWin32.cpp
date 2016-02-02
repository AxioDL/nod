#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "NOD/Util.hpp"
#include "NOD/IFileIO.hpp"

namespace NOD
{

class FileIOWin32 : public IFileIO
{
    SystemString m_path;
    int64_t m_maxWriteSize;
public:
    FileIOWin32(const SystemString& path, int64_t maxWriteSize)
    : m_path(path), m_maxWriteSize(maxWriteSize) {}
    FileIOWin32(const SystemChar* path, int64_t maxWriteSize)
    : m_path(path), m_maxWriteSize(maxWriteSize) {}

    bool exists()
    {
        HANDLE fp = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fp == INVALID_HANDLE_VALUE)
            return false;
        CloseHandle(fp);
        return true;
    }

    uint64_t size()
    {
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
    }

    struct WriteStream : public IFileIO::IWriteStream
    {
        HANDLE fp;
        int64_t m_maxWriteSize;
        WriteStream(const SystemString& path, int64_t maxWriteSize)
        : m_maxWriteSize(maxWriteSize)
        {
            fp = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE,
                             nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (fp == INVALID_HANDLE_VALUE)
                LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for writing"), path.c_str());
        }
        WriteStream(const SystemString& path, uint64_t offset, int64_t maxWriteSize)
        : m_maxWriteSize(maxWriteSize)
        {
            fp = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (fp == INVALID_HANDLE_VALUE)
                LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for writing"), path.c_str());
            LARGE_INTEGER lioffset;
            lioffset.QuadPart = offset;
            SetFilePointerEx(fp, lioffset, nullptr, FILE_BEGIN);
        }
        ~WriteStream()
        {
            CloseHandle(fp);
        }
        uint64_t write(const void* buf, uint64_t length)
        {
            if (m_maxWriteSize >= 0)
            {
                LARGE_INTEGER li = {};
                LARGE_INTEGER res;
                SetFilePointerEx(fp, li, &res, FILE_CURRENT);
                if (res.QuadPart + int64_t(length) > m_maxWriteSize)
                    LogModule.report(LogVisor::FatalError, _S("write operation exceeds file's %" PRIi64 "-byte limit"), m_maxWriteSize);
            }

            DWORD ret = 0;
            WriteFile(fp, buf, length, &ret, nullptr);
            return ret;
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
        return std::unique_ptr<IWriteStream>(new WriteStream(m_path, m_maxWriteSize));
    }
    std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const
    {
        return std::unique_ptr<IWriteStream>(new WriteStream(m_path, offset, m_maxWriteSize));
    }

    struct ReadStream : public IFileIO::IReadStream
    {
        HANDLE fp;
        ReadStream(const SystemString& path)
        {
            fp = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (fp == INVALID_HANDLE_VALUE)
                LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for reading"), path.c_str());
        }
        ReadStream(const SystemString& path, uint64_t offset)
        : ReadStream(path)
        {
            LARGE_INTEGER lioffset;
            lioffset.QuadPart = offset;
            SetFilePointerEx(fp, lioffset, nullptr, FILE_BEGIN);
        }
        ~ReadStream()
        {
            CloseHandle(fp);
        }
        void seek(int64_t offset, int whence)
        {
            LARGE_INTEGER li;
            li.QuadPart = offset;
            SetFilePointerEx(fp, li, nullptr, whence);
        }
        int64_t position()
        {
            LARGE_INTEGER li = {};
            LARGE_INTEGER res;
            SetFilePointerEx(fp, li, &res, FILE_CURRENT);
            return res.QuadPart;
        }
        uint64_t read(void* buf, uint64_t length)
        {
            DWORD ret = 0;
            ReadFile(fp, buf, length, &ret, nullptr);
            return ret;
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

std::unique_ptr<IFileIO> NewFileIO(const SystemString& path, int64_t maxWriteSize)
{
    return std::unique_ptr<IFileIO>(new FileIOWin32(path, maxWriteSize));
}

std::unique_ptr<IFileIO> NewFileIO(const SystemChar* path, int64_t maxWriteSize)
{
    return std::unique_ptr<IFileIO>(new FileIOWin32(path, maxWriteSize));
}

}
