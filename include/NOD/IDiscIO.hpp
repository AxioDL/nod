#ifndef __NOD_IDISC_IO__
#define __NOD_IDISC_IO__

#include <memory>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#if NOD_ATHENA
#include <Athena/IStreamReader.hpp>
#include <Athena/IStreamWriter.hpp>
#endif

namespace NOD
{

class IDiscIO
{
public:
    virtual ~IDiscIO() {}

    struct IReadStream
    {
        virtual ~IReadStream() {}
        virtual uint64_t read(void* buf, uint64_t length)=0;
        virtual void seek(int64_t offset, int whence=SEEK_SET)=0;
        virtual uint64_t position() const=0;
    };
    virtual std::unique_ptr<IReadStream> beginReadStream(uint64_t offset=0) const=0;

    struct IWriteStream
    {
        virtual ~IWriteStream() {}
        virtual uint64_t write(void* buf, uint64_t length)=0;
    };
    virtual std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset=0) const=0;
};

struct IPartReadStream
{
    virtual ~IPartReadStream() {}
    virtual void seek(int64_t offset, int whence=SEEK_SET)=0;
    virtual uint64_t position() const=0;
    virtual uint64_t read(void* buf, uint64_t length)=0;
};

struct IPartWriteStream
{
    virtual ~IPartWriteStream() {}
    virtual void close()=0;
    virtual uint64_t position() const=0;
    virtual uint64_t write(const void* buf, uint64_t length)=0;
};

#if NOD_ATHENA

class AthenaPartReadStream : public Athena::io::IStreamReader
{
    std::unique_ptr<IPartReadStream> m_rs;
public:
    AthenaPartReadStream(std::unique_ptr<IPartReadStream>&& rs) : m_rs(std::move(rs)) {}

    inline void seek(atInt64 off, Athena::SeekOrigin origin)
    {
        if (origin == Athena::Begin)
            m_rs->seek(off, SEEK_SET);
        else if (origin == Athena::Current)
            m_rs->seek(off, SEEK_CUR);
    }
    inline atUint64 position()    const {return m_rs->position();}
    inline atUint64 length()      const {return 0;}
    inline atUint64 readUBytesToBuf(void* buf, atUint64 sz) {m_rs->read(buf, sz); return sz;}
};

#endif

}

#endif // __NOD_IDISC_IO__
