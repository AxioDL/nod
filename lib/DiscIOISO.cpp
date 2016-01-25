#include <stdio.h>
#include "NOD/Util.hpp"
#include "NOD/IDiscIO.hpp"

namespace NOD
{

class DiscIOISO : public IDiscIO
{
    SystemString filepath;
public:
    DiscIOISO(const SystemString& fpin)
    : filepath(fpin) {}

    class ReadStream : public IReadStream
    {
        friend class DiscIOISO;
#if _WIN32
        HANDLE fp;
        ReadStream(HANDLE fpin)
        : fp(fpin) {}
        ~ReadStream() {CloseHandle(fp);}
#else
        FILE* fp;
        ReadStream(FILE* fpin)
        : fp(fpin) {}
        ~ReadStream() {fclose(fp);}
#endif
    public:
#if _WIN32
        uint64_t read(void* buf, uint64_t length)
        {
            DWORD ret = 0;
            ReadFile(fp, buf, length, &ret, nullptr);
            return ret;
        }
        uint64_t position() const
        {
            LARGE_INTEGER liOfs={0};
            LARGE_INTEGER liNew={0};
            SetFilePointerEx(fp, liOfs, &liNew, FILE_CURRENT);
            return liNew.QuadPart;
        }
        void seek(int64_t offset, int whence)
        {
            LARGE_INTEGER li;
            li.QuadPart = offset;
            SetFilePointerEx(fp, li, nullptr, whence);
        }
#else
        uint64_t read(void* buf, uint64_t length)
        {return fread(buf, 1, length, fp);}
        uint64_t position() const
        {return ftello(fp);}
        void seek(int64_t offset, int whence)
        {FSeek(fp, offset, whence);}
#endif
    };

#if _WIN32
    std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const
    {
        HANDLE fp = CreateFileW(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fp == INVALID_HANDLE_VALUE)
        {
            LogModule.report(LogVisor::Error, _S("Unable to open '%s' for reading"), filepath.c_str());
            return std::unique_ptr<IReadStream>();
        }
        LARGE_INTEGER li;
        li.QuadPart = offset;
        SetFilePointerEx(fp, li, nullptr, FILE_BEGIN);
        return std::unique_ptr<IReadStream>(new ReadStream(fp));
    }
#else
    std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const
    {
        FILE* fp = fopen(filepath.c_str(), "rb");
        if (!fp)
        {
            LogModule.report(LogVisor::Error, _S("Unable to open '%s' for reading"), filepath.c_str());
            return std::unique_ptr<IReadStream>();
        }
        FSeek(fp, offset, SEEK_SET);
        return std::unique_ptr<IReadStream>(new ReadStream(fp));
    }
#endif

    class WriteStream : public IWriteStream
    {
        friend class DiscIOISO;
#if _WIN32
        HANDLE fp;
        WriteStream(HANDLE fpin)
        : fp(fpin) {}
        ~WriteStream() {CloseHandle(fp);}
#else
        FILE* fp;
        WriteStream(FILE* fpin)
        : fp(fpin) {}
        ~WriteStream() {fclose(fp);}
#endif
    public:
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
    };

#if _WIN32
    std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const
    {
        HANDLE fp = CreateFileW(filepath.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fp == INVALID_HANDLE_VALUE)
        {
            LogModule.report(LogVisor::Error, _S("Unable to open '%s' for writing"), filepath.c_str());
            return std::unique_ptr<IWriteStream>();
        }
        LARGE_INTEGER li;
        li.QuadPart = offset;
        SetFilePointerEx(fp, li, nullptr, FILE_BEGIN);
        return std::unique_ptr<IWriteStream>(new WriteStream(fp));
    }
#else
    std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const
    {
        FILE* fp = fopen(filepath.c_str(), "wb");
        if (!fp)
        {
            LogModule.report(LogVisor::Error, _S("Unable to open '%s' for writing"), filepath.c_str());
            return std::unique_ptr<IWriteStream>();
        }
        FSeek(fp, offset, SEEK_SET);
        return std::unique_ptr<IWriteStream>(new WriteStream(fp));
    }
#endif
};

std::unique_ptr<IDiscIO> NewDiscIOISO(const SystemChar* path)
{
    return std::unique_ptr<IDiscIO>(new DiscIOISO(path));
}

}

