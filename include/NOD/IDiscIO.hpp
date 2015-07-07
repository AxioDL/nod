#ifndef __NOD_IDISC_IO__
#define __NOD_IDISC_IO__

#include <memory>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

namespace NOD
{

class IDiscIO
{
public:
    virtual ~IDiscIO() {}

    struct IReadStream
    {
        virtual uint64_t read(void* buf, uint64_t length)=0;
        virtual void seek(int64_t offset, int whence=SEEK_SET)=0;
        virtual uint64_t position() const=0;
    };
    virtual std::unique_ptr<IReadStream> beginReadStream(uint64_t offset=0) const=0;

    struct IWriteStream
    {
        virtual uint64_t write(void* buf, uint64_t length)=0;
    };
    virtual std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset=0) const=0;
};

struct IPartReadStream
{
    virtual void seek(int64_t offset, int whence=SEEK_SET)=0;
    virtual uint64_t position() const=0;
    virtual uint64_t read(void* buf, uint64_t length)=0;
};

struct IPartWriteStream
{
    virtual void seek(int64_t offset, int whence=SEEK_SET)=0;
    virtual uint64_t position() const=0;
    virtual uint64_t write(void* buf, uint64_t length)=0;
};

#if NOD_ATHENA
#include <Athena/IStreamReader.hpp>
#include <Athena/IStreamWriter.hpp>

class AthenaPartReadStream : public Athena::io::IStreamReader
{
    std::unique_ptr<IPartReadStream> m_rs;
    Athena::Endian m_endian;
public:
    AthenaPartReadStream(std::unique_ptr<IPartReadStream>&& rs) : m_rs(std::move(rs)) {}

    inline void setEndian(Athena::Endian endian) {m_endian = endian;}
    inline Athena::Endian endian()      const {return m_endian;}
    inline bool isBigEndian()   const {return m_endian == Athena::BigEndian;}
    inline bool isLittleEndian()const {return m_endian == Athena::LittleEndian;}
    inline bool isOpen()        const {return true;}
    inline void seek(atInt64 off, Athena::SeekOrigin origin)
    {
        if (origin == Athena::Begin)
            m_rs->seek(off, SEEK_SET);
        else if (origin == Athena::Current)
            m_rs->seek(off, SEEK_CUR);
    }
    inline void seekAlign32() {AthenaPartReadStream::seek(ROUND_UP_32(m_rs->position()), Athena::Begin);}
    inline bool atEnd()         const {return false;}
    inline atUint64 position()    const {return m_rs->position();}
    inline atUint64 length()      const {return 0;}
    inline void seekBit(int) {}
    inline bool     readBit() {return false;}
    inline atUint8  readUByte() {atUint8 val; m_rs->read(&val, 1); return val;}
    inline atInt8   readByte() {return AthenaPartReadStream::readUByte();}
    inline atUint8* readUBytes(atUint64 sz) {atUint8* buf = new atUint8[sz]; m_rs->read(buf, sz); return buf;}
    inline atInt8*  readBytes(atUint64 sz) {return (atInt8*)AthenaPartReadStream::readUBytes(sz);}
    inline atUint64 readUBytesToBuf(void* buf, atUint64 sz) {m_rs->read(buf, sz); return sz;}
    inline atUint64 readBytesToBuf(void* buf, atUint64 sz) {return AthenaPartReadStream::readUBytesToBuf(buf, sz);}
    inline atUint16 readUint16()
    {atUint16 val; m_rs->read(&val, 2); return (m_endian == Athena::BigEndian) ? Athena::utility::BigUint16(val) : Athena::utility::LittleUint16(val);}
    inline atInt16  readInt16() {return AthenaPartReadStream::readUint16();}
    inline atUint32 readUint32()
    {atUint32 val; m_rs->read(&val, 4); return (m_endian == Athena::BigEndian) ? Athena::utility::BigUint32(val) : Athena::utility::LittleUint32(val);}
    inline atInt32  readInt32() {return AthenaPartReadStream::readUint32();}
    inline atUint64 readUint64()
    {atUint64 val; m_rs->read(&val, 8); return (m_endian == Athena::BigEndian) ? Athena::utility::BigUint64(val) : Athena::utility::LittleUint64(val);}
    inline atInt64  readInt64() {return AthenaPartReadStream::readUint64();}
    inline double   readDouble()
    {double val; m_rs->read(&val, 8); return (m_endian == Athena::BigEndian) ? Athena::utility::BigDouble(val) : Athena::utility::LittleDouble(val);}
    inline float    readFloat()
    {float val; m_rs->read(&val, 4); return (m_endian == Athena::BigEndian) ? Athena::utility::BigFloat(val) : Athena::utility::LittleFloat(val);}
    inline bool     readBool() {bool val; m_rs->read(&val, 1); return val;}
    inline atVec3f  readVec3f()
    {
        atVec3f val = {};
        m_rs->read(&val, 12);
        if (m_endian == Athena::BigEndian)
        {
            Athena::utility::BigFloat(val.vec[0]);
            Athena::utility::BigFloat(val.vec[1]);
            Athena::utility::BigFloat(val.vec[2]);
        }
        else
        {
            Athena::utility::LittleFloat(val.vec[0]);
            Athena::utility::LittleFloat(val.vec[1]);
            Athena::utility::LittleFloat(val.vec[2]);
        }
        return val;
    }
    inline atVec4f  readVec4f()
    {
        atVec4f val;
        m_rs->read(&val, 16);
        if (m_endian == Athena::BigEndian)
        {
            Athena::utility::BigFloat(val.vec[0]);
            Athena::utility::BigFloat(val.vec[1]);
            Athena::utility::BigFloat(val.vec[2]);
            Athena::utility::BigFloat(val.vec[3]);
        }
        else
        {
            Athena::utility::LittleFloat(val.vec[0]);
            Athena::utility::LittleFloat(val.vec[1]);
            Athena::utility::LittleFloat(val.vec[2]);
            Athena::utility::LittleFloat(val.vec[3]);
        }
        return val;
    }
    inline std::string readUnicode(atInt32 = -1)
    {return std::string();}
    inline std::string readString(atInt32 len = -1)
    {
        if (len < 0)
        {
            std::string result;
            for (;;)
            {
                char aChar;
                m_rs->read(&aChar, 1);
                if (!aChar)
                    break;
                result += aChar;
            }
            return result;
        }
        atUint8* chars = AthenaPartReadStream::readUBytes(len);
        std::string result((char*)chars, len);
        delete[] chars;
        return result;
    }
    inline std::wstring readWString(atInt32 len = -1)
    {
        if (len < 0)
        {
            std::wstring result;
            for (;;)
            {
                atUint16 aChar = AthenaPartReadStream::readUint16();
                if (!aChar)
                    break;
                result += aChar;
            }
            return result;
        }
        std::wstring result;
        result.reserve(len);
        for (atInt32 c=0 ; c<len ; ++c)
        {
            atUint16 aChar = AthenaPartReadStream::readUint16();
            result += aChar;
        }
        return result;
    }
};

class AthenaPartWriteStream : public Athena::io::IStreamWriter
{
    std::unique_ptr<IPartWriteStream> m_ws;
    Athena::Endian m_endian;
public:
    AthenaPartWriteStream(std::unique_ptr<IPartWriteStream>&& ws) : m_ws(std::move(ws)) {}

    inline void setEndian(Athena::Endian endian) {m_endian = endian;}
    inline Athena::Endian endian()      const {return m_endian;}
    inline bool isBigEndian()   const {return m_endian == Athena::BigEndian;}
    inline bool isLittleEndian()const {return m_endian == Athena::LittleEndian;}
    inline bool isOpen()        const {return true;}
    inline void seek(atInt64 off, Athena::SeekOrigin origin)
    {
        if (origin == Athena::Begin)
            m_ws->seek(off, SEEK_SET);
        else if (origin == Athena::Current)
            m_ws->seek(off, SEEK_CUR);
    }
    inline void seekAlign32() {AthenaPartWriteStream::seek(ROUND_UP_32(m_ws->position()), Athena::Begin);}
    inline bool atEnd()         const {return false;}
    inline atUint64 position()    const {return m_ws->position();}
    inline atUint64 length()      const {return 0;}
    inline void seekBit(int) {}
    inline void writeBit(bool) {}
    inline void writeUByte(atUint8 val) {m_ws->write(&val, 1);}
    inline void writeByte(atInt8 val) {m_ws->write(&val, 1);}
    inline void writeUBytes(const atUint8* buf, atUint64 len) {m_ws->write((void*)buf, len);}
    inline void writeBytes(const atInt8* buf, atUint64 len)  {m_ws->write((void*)buf, len);}
    inline void writeUint16(atUint16 val)
    {if (m_endian == Athena::BigEndian) Athena::utility::BigUint16(val); else Athena::utility::LittleUint16(val); m_ws->write(&val, 2);}
    inline void writeInt16(atInt16 val) {AthenaPartWriteStream::writeUint16(val);}
    inline void writeUint32(atUint32 val)
    {if (m_endian == Athena::BigEndian) Athena::utility::BigUint32(val); else Athena::utility::LittleUint32(val); m_ws->write(&val, 4);}
    inline void writeInt32(atInt32 val) {AthenaPartWriteStream::writeUint32(val);}
    inline void writeUint64(atUint64 val)
    {if (m_endian == Athena::BigEndian) Athena::utility::BigUint64(val); else Athena::utility::LittleUint64(val); m_ws->write(&val, 8);}
    inline void writeInt64(atInt64 val) {AthenaPartWriteStream::writeUint64(val);}
    inline void writeDouble(double val)
    {if (m_endian == Athena::BigEndian) Athena::utility::BigDouble(val); else Athena::utility::LittleDouble(val); m_ws->write(&val, 8);}
    inline void writeFloat(float val)
    {if (m_endian == Athena::BigEndian) Athena::utility::BigFloat(val); else Athena::utility::LittleFloat(val); m_ws->write(&val, 4);}
    inline void writeBool(bool val) {m_ws->write(&val, 1);}
    inline void writeVec3f(atVec3f vec)
    {
        if (m_endian == Athena::BigEndian)
        {
            Athena::utility::BigFloat(vec.vec[0]);
            Athena::utility::BigFloat(vec.vec[1]);
            Athena::utility::BigFloat(vec.vec[2]);
        }
        else
        {
            Athena::utility::LittleFloat(vec.vec[0]);
            Athena::utility::LittleFloat(vec.vec[1]);
            Athena::utility::LittleFloat(vec.vec[2]);
        }
        m_ws->write(&vec, 12);
    }
    inline void writeVec4f(atVec4f vec)
    {
        if (m_endian == Athena::BigEndian)
        {
            Athena::utility::BigFloat(vec.vec[0]);
            Athena::utility::BigFloat(vec.vec[1]);
            Athena::utility::BigFloat(vec.vec[2]);
            Athena::utility::BigFloat(vec.vec[3]);
        }
        else
        {
            Athena::utility::LittleFloat(vec.vec[0]);
            Athena::utility::LittleFloat(vec.vec[1]);
            Athena::utility::LittleFloat(vec.vec[2]);
            Athena::utility::LittleFloat(vec.vec[3]);
        }
        m_ws->write(&vec, 16);
    }
    inline void writeString(const std::string& str, atInt32 len = -1)
    {
        if (len < 0)
            m_ws->write((void*)str.c_str(), str.size() + 1);
        else
            m_ws->write((void*)str.c_str(), len);
    }
    inline void writeWString(const std::wstring& str, atInt32 len = -1)
    {
        if (len < 0)
        {
            for (atUint16 ch : str)
                AthenaPartWriteStream::writeUint16(ch);
            AthenaPartWriteStream::writeUint16(0);
        }
        else
        {
            for (atInt32 c=0 ; c<len ; ++c)
            {
                if (c >= (atInt32)str.size())
                    AthenaPartWriteStream::writeUint16(0);
                else
                    AthenaPartWriteStream::writeUint16(str[c]);
            }
        }
    }
    inline void writeUnicode(const std::string&, atInt32 = -1) {}
    inline void fill(atUint8 val, atUint64 len)
    {
        for (atUint64 b=0 ; b<len ; ++b)
            AthenaPartWriteStream::writeUByte(val);
    }
    inline void fill(atInt8 val, atUint64 len)
    {
        for (atUint64 b=0 ; b<len ; ++b)
            AthenaPartWriteStream::writeByte(val);
    }
};

#endif

}

#endif // __NOD_IDISC_IO__
