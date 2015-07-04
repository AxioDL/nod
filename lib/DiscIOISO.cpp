#include <stdio.h>
#include <stdexcept>
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
        FILE* fp;
        ReadStream(FILE* fpin)
        : fp(fpin) {}
        ~ReadStream() {fclose(fp);}
    public:
        uint64_t read(void* buf, uint64_t length)
        {return fread(buf, 1, length, fp);}
        void seek(int64_t offset, int whence)
        {fseeko(fp, offset, whence);}
    };
    std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const
    {
#if NOD_UCS2
        FILE* fp = _wfopen(filepath.c_str(), L"rb");
#else
        FILE* fp = fopen(filepath.c_str(), "rb");
#endif
        if (!fp)
        {
#if NOD_UCS2
            LogModule.report(LogVisor::Error, L"Unable to open '%s' for reading\n", filepath.c_str());
#else
            LogModule.report(LogVisor::Error, "Unable to open '%s' for reading\n", filepath.c_str());
#endif
            return std::unique_ptr<IReadStream>();
        }
        fseeko(fp, offset, SEEK_SET);
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
        uint64_t write(void* buf, uint64_t length)
        {return fwrite(buf, 1, length, fp);}
    };
    std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const
    {
#if NOD_UCS2
        FILE* fp = _wfopen(filepath.c_str(), L"wb");
#else
        FILE* fp = fopen(filepath.c_str(), "wb");
#endif
        if (!fp)
        {
#if NOD_UCS2
            LogModule.report(LogVisor::Error, L"Unable to open '%s' for writing\n", filepath.c_str());
#else
            LogModule.report(LogVisor::Error, "Unable to open '%s' for writing\n", filepath.c_str());
#endif
            return std::unique_ptr<IWriteStream>();
        }
        fseeko(fp, offset, SEEK_SET);
        return std::unique_ptr<IWriteStream>(new WriteStream(fp));
    }
};

std::unique_ptr<IDiscIO> NewDiscIOISO(const SystemChar* path)
{
    return std::unique_ptr<IDiscIO>(new DiscIOISO(path));
}

}

