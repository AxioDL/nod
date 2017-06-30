#include <stdio.h>
#include <string.h>
#include <cstdlib>
#include <inttypes.h>
#include "nod/DiscWii.hpp"
#include "nod/aes.hpp"
#include "nod/sha1.h"

namespace nod
{

static const uint8_t COMMON_KEYS[2][16] =
{
    /* Normal */
    {0xeb, 0xe4, 0x2a, 0x22,
     0x5e, 0x85, 0x93, 0xe4,
     0x48, 0xd9, 0xc5, 0x45,
     0x73, 0x81, 0xaa, 0xf7},
    /* Korean */
    {0x63, 0xb8, 0x2b, 0xb4,
     0xf4, 0x61, 0x4e, 0x2e,
     0x13, 0xf2, 0xfe, 0xfb,
     0xba, 0x4c, 0x9b, 0x7e}
};

class PartitionWii : public DiscBase::IPartition
{
    enum class SigType : uint32_t
    {
        RSA_4096 = 0x00010000,
        RSA_2048 = 0x00010001,
        ELIPTICAL_CURVE = 0x00010002
    };

    enum class KeyType : uint32_t
    {
        RSA_4096 = 0x00000000,
        RSA_2048 = 0x00000001
    };

    struct Ticket
    {
        uint32_t sigType;
        char sig[256];
        char padding[60];
        char sigIssuer[64];
        char ecdh[60];
        char padding1[3];
        unsigned char encKey[16];
        char padding2;
        char ticketId[8];
        char consoleId[4];
        char titleId[8];
        char padding3[2];
        uint16_t ticketVersion;
        uint32_t permittedTitlesMask;
        uint32_t permitMask;
        char titleExportAllowed;
        char commonKeyIdx;
        char padding4[48];
        char contentAccessPermissions[64];
        char padding5[2];
        struct TimeLimit
        {
            uint32_t enableTimeLimit;
            uint32_t timeLimit;
        } timeLimits[8];

        void read(IDiscIO::IReadStream& s)
        {
            s.read(this, 676);
            sigType = SBig(sigType);
            ticketVersion = SBig(ticketVersion);
            permittedTitlesMask = SBig(permittedTitlesMask);
            permitMask = SBig(permitMask);
            for (size_t t=0 ; t<8 ; ++t)
            {
                timeLimits[t].enableTimeLimit = SBig(timeLimits[t].enableTimeLimit);
                timeLimits[t].timeLimit = SBig(timeLimits[t].timeLimit);
            }
        }
    } m_ticket;

    struct TMD
    {
        SigType sigType;
        char sig[256];
        char padding[60];
        char sigIssuer[64];
        char version;
        char caCrlVersion;
        char signerCrlVersion;
        char padding1;
        uint32_t iosIdMajor;
        uint32_t iosIdMinor;
        uint32_t titleIdMajor;
        char titleIdMinor[4];
        uint32_t titleType;
        uint16_t groupId;
        char padding2[62];
        uint32_t accessFlags;
        uint16_t titleVersion;
        uint16_t numContents;
        uint16_t bootIdx;
        uint16_t padding3;

        struct Content
        {
            uint32_t id;
            uint16_t index;
            uint16_t type;
            uint64_t size;
            char hash[20];

            void read(IDiscIO::IReadStream& s)
            {
                s.read(this, 36);
                id = SBig(id);
                index = SBig(index);
                type = SBig(type);
                size = SBig(size);
            }
        };
        std::vector<Content> contents;

        void read(IDiscIO::IReadStream& s)
        {
            s.read(this, 484);
            sigType = SigType(SBig(uint32_t(sigType)));
            iosIdMajor = SBig(iosIdMajor);
            iosIdMinor = SBig(iosIdMinor);
            titleIdMajor = SBig(titleIdMajor);
            titleType = SBig(titleType);
            groupId = SBig(groupId);
            accessFlags = SBig(accessFlags);
            titleVersion = SBig(titleVersion);
            numContents = SBig(numContents);
            bootIdx = SBig(bootIdx);
            contents.clear();
            contents.reserve(numContents);
            for (uint16_t c=0 ; c<numContents ; ++c)
            {
                contents.emplace_back();
                contents.back().read(s);
            }
        }
    } m_tmd;

    struct Certificate
    {
        SigType sigType;
        char sig[512];
        char issuer[64];
        KeyType keyType;
        char subject[64];
        char key[512];
        uint32_t modulus;
        uint32_t pubExp;

        void read(IDiscIO::IReadStream& s)
        {
            s.read(&sigType, 4);
            sigType = SigType(SBig(uint32_t(sigType)));
            if (sigType == SigType::RSA_4096)
                s.read(sig, 512);
            else if (sigType == SigType::RSA_2048)
                s.read(sig, 256);
            else if (sigType == SigType::ELIPTICAL_CURVE)
                s.read(sig, 64);
            s.seek(60, SEEK_CUR);

            s.read(issuer, 64);
            s.read(&keyType, 4);
            s.read(subject, 64);
            keyType = KeyType(SBig(uint32_t(keyType)));
            if (keyType == KeyType::RSA_4096)
                s.read(key, 512);
            else if (keyType == KeyType::RSA_2048)
                s.read(key, 256);

            s.read(&modulus, 8);
            modulus = SBig(modulus);
            pubExp = SBig(pubExp);

            s.seek(52, SEEK_CUR);
        }
    };
    Certificate m_caCert;
    Certificate m_tmdCert;
    Certificate m_ticketCert;

    uint64_t m_dataOff;
    uint8_t m_decKey[16];

public:
    PartitionWii(const DiscWii& parent, Kind kind, uint64_t offset, bool& err)
    : IPartition(parent, kind, offset)
    {
        std::unique_ptr<IDiscIO::IReadStream> s = parent.getDiscIO().beginReadStream(offset);
        if (!s)
        {
            err = true;
            return;
        }

        m_ticket.read(*s);

        uint32_t tmdSize;
        s->read(&tmdSize, 4);
        tmdSize = SBig(tmdSize);
        uint32_t tmdOff;
        s->read(&tmdOff, 4);
        tmdOff = SBig(tmdOff) << 2;

        uint32_t certChainSize;
        s->read(&certChainSize, 4);
        certChainSize = SBig(certChainSize);
        uint32_t certChainOff;
        s->read(&certChainOff, 4);
        certChainOff = SBig(certChainOff) << 2;

        uint32_t globalHashTableOff;
        s->read(&globalHashTableOff, 4);
        globalHashTableOff = SBig(globalHashTableOff) << 2;

        uint32_t dataOff;
        s->read(&dataOff, 4);
        dataOff = SBig(dataOff) << 2;
        m_dataOff = offset + dataOff;
        uint32_t dataSize;
        s->read(&dataSize, 4);
        dataSize = SBig(dataSize) << 2;

        s->seek(offset + tmdOff);
        m_tmd.read(*s);

        s->seek(offset + certChainOff);
        m_caCert.read(*s);
        m_tmdCert.read(*s);
        m_ticketCert.read(*s);

        /* Decrypt title key */
        std::unique_ptr<IAES> aes = NewAES();
        uint8_t iv[16] = {};
        memcpy(iv, m_ticket.titleId, 8);
        aes->setKey(COMMON_KEYS[(int)m_ticket.commonKeyIdx]);
        aes->decrypt(iv, m_ticket.encKey, m_decKey, 16);

        /* Wii-specific header reads (now using title key to decrypt) */
        std::unique_ptr<IPartReadStream> ds = beginReadStream(0x420);
        if (!ds)
        {
            err = true;
            return;
        }

        s->read(&m_bi2Header, sizeof(BI2Header));
        m_dolOff = SBig(m_bi2Header.dolOff) << 2;
        m_fstOff = SBig(m_bi2Header.fstOff) << 2;
        m_fstSz = SBig(m_bi2Header.fstSz) << 2;
        ds->seek(0x2440 + 0x14);
        uint32_t vals[2];
        ds->read(vals, 8);
        m_apploaderSz = ((32 + SBig(vals[0]) + SBig(vals[1])) + 31) & ~31;

        /* Yay files!! */
        parseFST(*ds);

        /* Also make DOL header and size handy */
        ds->seek(m_dolOff);
        parseDOL(*ds);
    }

    class PartReadStream : public IPartReadStream
    {
        std::unique_ptr<IAES> m_aes;
        const PartitionWii& m_parent;
        uint64_t m_baseOffset;
        uint64_t m_offset;
        std::unique_ptr<IDiscIO::IReadStream> m_dio;

        size_t m_curBlock = SIZE_MAX;
        uint8_t m_encBuf[0x8000];
        uint8_t m_decBuf[0x7c00];

        void decryptBlock()
        {
            m_dio->read(m_encBuf, 0x8000);
            m_aes->decrypt(&m_encBuf[0x3d0], &m_encBuf[0x400], m_decBuf, 0x7c00);
        }
    public:
        PartReadStream(const PartitionWii& parent, uint64_t baseOffset, uint64_t offset, bool& err)
        : m_aes(NewAES()), m_parent(parent), m_baseOffset(baseOffset), m_offset(offset)
        {
            m_aes->setKey(parent.m_decKey);
            size_t block = m_offset / 0x7c00;
            m_dio = m_parent.m_parent.getDiscIO().beginReadStream(m_baseOffset + block * 0x8000);
            if (!m_dio)
            {
                err = true;
                return;
            }
            decryptBlock();
            m_curBlock = block;
        }
        void seek(int64_t offset, int whence)
        {
            if (whence == SEEK_SET)
                m_offset = offset;
            else if (whence == SEEK_CUR)
                m_offset += offset;
            else
                return;
            size_t block = m_offset / 0x7c00;
            if (block != m_curBlock)
            {
                m_dio->seek(m_baseOffset + block * 0x8000);
                decryptBlock();
                m_curBlock = block;
            }
        }
        uint64_t position() const {return m_offset;}
        uint64_t read(void* buf, uint64_t length)
        {
            size_t block = m_offset / 0x7c00;
            size_t cacheOffset = m_offset % 0x7c00;
            uint64_t cacheSize;
            uint64_t rem = length;
            uint8_t* dst = (uint8_t*)buf;

            while (rem)
            {
                if (block != m_curBlock)
                {
                    decryptBlock();
                    m_curBlock = block;
                }

                cacheSize = rem;
                if (cacheSize + cacheOffset > 0x7c00)
                    cacheSize = 0x7c00 - cacheOffset;

                memcpy(dst, m_decBuf + cacheOffset, cacheSize);
                dst += cacheSize;
                rem -= cacheSize;
                cacheOffset = 0;
                ++block;
            }

            m_offset += length;
            return dst - (uint8_t*)buf;
        }
    };

    std::unique_ptr<IPartReadStream> beginReadStream(uint64_t offset) const
    {
        bool Err = false;
        auto ret = std::unique_ptr<IPartReadStream>(new PartReadStream(*this, m_dataOff, offset, Err));
        if (Err)
            return {};
        return ret;
    }

    uint64_t normalizeOffset(uint64_t anOffset) const {return anOffset << 2;}

    std::unique_ptr<uint8_t[]> readPartitionHeaderBuf(size_t& szOut) const
    {
        {
            std::unique_ptr<IDiscIO::IReadStream> rs = m_parent.getDiscIO().beginReadStream(m_offset + 0x2B4);
            if (!rs)
                return {};

            uint32_t h3;
            if (rs->read(&h3, 4) != 4)
            {
                LogModule.report(logvisor::Error, _S("unable to read H3 offset apploader"));
                return {};
            }
            h3 = SBig(h3);
            szOut = uint64_t(h3) << 2;
        }

        std::unique_ptr<IDiscIO::IReadStream> rs = m_parent.getDiscIO().beginReadStream(m_offset);
        if (!rs)
            return {};

        std::unique_ptr<uint8_t[]> buf(new uint8_t[szOut]);
        rs->read(buf.get(), szOut);

        return buf;
    }

    bool writeOutPartitionHeader(const SystemChar* pathOut) const
    {
        std::unique_ptr<IFileIO::IWriteStream> ws = NewFileIO(pathOut)->beginWriteStream();
        if (!ws)
            return false;
        uint64_t h3Off;
        {
            std::unique_ptr<IDiscIO::IReadStream> rs = m_parent.getDiscIO().beginReadStream(m_offset + 0x2B4);
            if (!rs)
                return false;

            uint32_t h3;
            if (rs->read(&h3, 4) != 4)
            {
                LogModule.report(logvisor::Error, _S("unable to read H3 offset to %s"), pathOut);
                return false;
            }
            h3 = SBig(h3);
            h3Off = uint64_t(h3) << 2;
        }

        char buf[8192];
        size_t rem = h3Off;
        std::unique_ptr<IDiscIO::IReadStream> rs = m_parent.getDiscIO().beginReadStream(m_offset);
        if (!rs)
            return false;

        while (rem)
        {
            size_t rdSz = nod::min(rem, size_t(8192ul));
            rs->read(buf, rdSz);
            ws->write(buf, rdSz);
            rem -= rdSz;
        }

        return true;
    }
};

DiscWii::DiscWii(std::unique_ptr<IDiscIO>&& dio, bool& err)
: DiscBase(std::move(dio), err)
{
    if (err)
        return;

    /* Read partition info */
    struct PartInfo
    {
        uint32_t partCount;
        uint32_t partInfoOff;
        struct Part
        {
            uint32_t partDataOff;
            IPartition::Kind partType;
        } parts[4];
        PartInfo(IDiscIO& dio, bool& err)
        {
            std::unique_ptr<IDiscIO::IReadStream> s = dio.beginReadStream(0x40000);
            if (!s)
            {
                err = true;
                return;
            }

            s->read(this, 32);
            partCount = SBig(partCount);
            partInfoOff = SBig(partInfoOff);

            s->seek(partInfoOff << 2);
            for (uint32_t p=0 ; p<partCount && p<4 ; ++p)
            {
                s->read(&parts[p], 8);
                parts[p].partDataOff = SBig(parts[p].partDataOff);
                parts[p].partType = IPartition::Kind(SBig(uint32_t(parts[p].partType)));
            }
        }
    } partInfo(*m_discIO, err);
    if (err)
        return;

    /* Iterate for data partition */
    m_partitions.reserve(partInfo.partCount);
    for (uint32_t p=0 ; p<partInfo.partCount && p<4 ; ++p)
    {
        PartInfo::Part& part = partInfo.parts[p];
        IPartition::Kind kind;
        switch (part.partType)
        {
        case IPartition::Kind::Data:
        case IPartition::Kind::Update:
        case IPartition::Kind::Channel:
            kind = part.partType;
            break;
        default:
            LogModule.report(logvisor::Error, "invalid partition type %d", part.partType);
            err = true;
            return;
        }
        m_partitions.emplace_back(new PartitionWii(*this, kind, part.partDataOff << 2, err));
        if (err)
            return;
    }
}

DiscBuilderWii DiscWii::makeMergeBuilder(const SystemChar* outPath, bool dualLayer, FProgress progressCB)
{
    return DiscBuilderWii(outPath, m_header.m_gameID, m_header.m_gameTitle,
                          dualLayer, progressCB);
}

bool DiscWii::writeOutDataPartitionHeader(const SystemChar* pathOut) const
{
    for (const std::unique_ptr<IPartition>& part : m_partitions)
    {
        if (part->getKind() == IPartition::Kind::Data)
        {
            return static_cast<PartitionWii&>(*part).writeOutPartitionHeader(pathOut);
        }
    }
    return false;
}

static const uint8_t ZEROIV[16] = {0};

class PartitionBuilderWii : public DiscBuilderBase::PartitionBuilderBase
{
    friend class DiscBuilderWii;
    friend class DiscMergerWii;

    uint64_t m_baseOffset;
    uint64_t m_userOffset = 0;
    uint64_t m_curUser = 0x1F0000;
    std::unique_ptr<IAES> m_aes;
    uint8_t m_h3[4916][20] = {};

public:
    class PartWriteStream : public IPartWriteStream
    {
        friend class PartitionBuilderWii;
        PartitionBuilderWii& m_parent;
        uint64_t m_baseOffset;
        uint64_t m_offset;
        std::unique_ptr<IFileIO::IWriteStream> m_fio;
        bool m_closed = false;

        size_t m_curGroup = SIZE_MAX;
        char m_buf[0x200000];

        void encryptGroup(uint8_t h3Out[20])
        {
            sha1nfo sha;
            uint8_t h2[8][20];

            for (int s=0 ; s<8 ; ++s)
            {
                char* ptr1 = m_buf + s*0x40000;
                uint8_t h1[8][20];

                for (int c=0 ; c<8 ; ++c)
                {
                    char* ptr0 = ptr1 + c*0x8000;
                    uint8_t h0[31][20];

                    for (int j=0 ; j<31 ; ++j)
                    {
                        sha1_init(&sha);
                        sha1_write(&sha, ptr0 + (j+1)*0x400, 0x400);
                        memcpy(h0[j], sha1_result(&sha), 20);
                    }

                    sha1_init(&sha);
                    sha1_write(&sha, (char*)h0, 0x26C);
                    memcpy(h1[c], sha1_result(&sha), 20);

                    memcpy(ptr0, h0, 0x26C);
                    memset(ptr0+0x26C, 0, 0x014);
                }

                sha1_init(&sha);
                sha1_write(&sha, (char*)h1, 0x0A0);
                memcpy(h2[s], sha1_result(&sha), 20);

                for (int c=0 ; c<8 ; ++c)
                {
                    char* ptr0 = ptr1 + c*0x8000;
                    memcpy(ptr0+0x280, h1, 0x0A0);
                    memset(ptr0+0x320, 0, 0x020);
                }
            }

            sha1_init(&sha);
            sha1_write(&sha, (char*)h2, 0x0A0);
            memcpy(h3Out, sha1_result(&sha), 20);

            for (int s=0 ; s<8 ; ++s)
            {
                char* ptr1 = m_buf + s*0x40000;
                for (int c=0 ; c<8 ; ++c)
                {
                    char* ptr0 = ptr1 + c*0x8000;
                    memcpy(ptr0+0x340, h2, 0x0A0);
                    memset(ptr0+0x3E0, 0, 0x020);
                    m_parent.m_aes->encrypt(ZEROIV, (uint8_t*)ptr0, (uint8_t*)ptr0, 0x400);
                    m_parent.m_aes->encrypt((uint8_t*)(ptr0+0x3D0), (uint8_t*)(ptr0+0x400), (uint8_t*)(ptr0+0x400), 0x7c00);
                }
            }

            if (m_fio->write(m_buf, 0x200000) != 0x200000)
            {
                LogModule.report(logvisor::Error, "unable to write full disc group");
                return;
            }
        }

    public:
        PartWriteStream(PartitionBuilderWii& parent, uint64_t baseOffset, uint64_t offset, bool& err)
        : m_parent(parent), m_baseOffset(baseOffset), m_offset(offset)
        {
            if (offset % 0x1F0000)
            {
                LogModule.report(logvisor::Error, "partition write stream MUST begin on 0x1F0000-aligned boundary");
                err = true;
                return;
            }
            size_t group = m_offset / 0x1F0000;
            m_fio = m_parent.m_parent.getFileIO().beginWriteStream(m_baseOffset + group * 0x200000);
            if (!m_fio)
                err = true;
            m_curGroup = group;
        }
        ~PartWriteStream() {close();}
        void close()
        {
            if (m_closed)
                return;
            m_closed = true;
            size_t rem = m_offset % 0x1F0000;
            if (rem)
            {
                rem = 0x1F0000 - rem;
                write(nullptr, rem);
            }
            encryptGroup(m_parent.m_h3[m_curGroup]);
            m_fio.reset();
        }
        uint64_t position() const {return m_offset;}
        uint64_t write(const void* buf, uint64_t length)
        {
            size_t group = m_offset / 0x1F0000;
            size_t block = (m_offset - group * 0x1F0000) / 0x7c00;
            size_t cacheOffset = m_offset % 0x7c00;
            uint64_t cacheSize;
            uint64_t rem = length;
            const uint8_t* src = (uint8_t*)buf;

            while (rem)
            {
                if (group != m_curGroup)
                {
                    encryptGroup(m_parent.m_h3[m_curGroup]);
                    m_curGroup = group;
                }

                cacheSize = rem;
                if (cacheSize + cacheOffset > 0x7c00)
                    cacheSize = 0x7c00 - cacheOffset;

                if (src)
                {
                    memcpy(m_buf + block * 0x8000 + 0x400 + cacheOffset, src, cacheSize);
                    src += cacheSize;
                }
                else
                    memset(m_buf + block * 0x8000 + 0x400 + cacheOffset, 0, cacheSize);

                rem -= cacheSize;
                cacheOffset = 0;
                ++block;
                if (block == 64)
                {
                    block = 0;
                    ++group;
                }
            }

            m_offset += length;
            return length;
        }
    };

    PartitionBuilderWii(DiscBuilderBase& parent, Kind kind,
                        const char gameID[6], const char* gameTitle, uint64_t baseOffset)
    : DiscBuilderBase::PartitionBuilderBase(parent, kind, gameID, gameTitle),
      m_baseOffset(baseOffset), m_aes(NewAES()) {}

    uint64_t getCurUserEnd() const {return m_curUser;}

    uint64_t userAllocate(uint64_t reqSz, IPartWriteStream& ws)
    {
        reqSz = ROUND_UP_32(reqSz);
        if (m_curUser + reqSz >= 0x1FB450000)
        {
            LogModule.report(logvisor::Error, "partition exceeds maximum single-partition capacity");
            return -1;
        }
        uint64_t ret = m_curUser;
        PartWriteStream& cws = static_cast<PartWriteStream&>(ws);
        if (cws.m_offset > ret)
        {
            LogModule.report(logvisor::Error, "partition overwrite error");
            return -1;
        }
        while (cws.m_offset < ret)
            cws.write("\xff", 1);
        m_curUser += reqSz;
        return ret;
    }

    uint32_t packOffset(uint64_t offset) const
    {
        return uint32_t(offset >> uint64_t(2));
    }

    std::unique_ptr<IPartWriteStream> beginWriteStream(uint64_t offset)
    {
        bool Err = false;
        std::unique_ptr<IPartWriteStream> ret =
            std::make_unique<PartWriteStream>(*this, m_baseOffset + m_userOffset, offset, Err);
        if (Err)
            return {};
        return ret;
    }

    uint64_t _build(const std::function<bool(IPartWriteStream&)>& contentFunc,
                    const std::function<bool(IPartWriteStream&, size_t&)>& apploaderFunc,
                    const uint8_t* phBuf, size_t phSz, size_t apploaderSz)
    {
        /* Read head and validate key members */
        uint8_t tkey[16];
        {
            if (0x1BF + 16 > phSz)
            {
                LogModule.report(logvisor::Error, _S("unable to read title key"));
                return -1;
            }
            memmove(tkey, phBuf + 0x1BF, 16);
        }

        uint8_t tkeyiv[16] = {};
        {
            if (0x1DC + 8 > phSz)
            {
                LogModule.report(logvisor::Error, _S("unable to read title key IV"));
                return -1;
            }
            memmove(tkeyiv, phBuf + 0x1DC, 8);
        }

        uint8_t ccIdx;
        {
            if (0x1F1 + 1 > phSz)
            {
                LogModule.report(logvisor::Error, _S("unable to read common key index"));
                return -1;
            }
            memmove(&ccIdx, phBuf + 0x1F1, 1);
            if (ccIdx > 1)
            {
                LogModule.report(logvisor::Error, _S("common key index may only be 0 or 1"));
                return -1;
            }
        }

        uint32_t tmdSz;
        {
            if (0x2A4 + 4 > phSz)
            {
                LogModule.report(logvisor::Error, _S("unable to read TMD size"));
                return -1;
            }
            memmove(&tmdSz, phBuf + 0x2A4, 4);
            tmdSz = SBig(tmdSz);
        }

        uint64_t h3Off;
        {
            uint32_t h3Ptr;
            if (0x2B4 + 4 > phSz)
            {
                LogModule.report(logvisor::Error, _S("unable to read H3 pointer"));
                return -1;
            }
            memmove(&h3Ptr, phBuf + 0x2B4, 4);
            h3Off = uint64_t(SBig(h3Ptr)) << 2;
        }

        uint64_t dataOff;
        {
            uint32_t dataPtr;
            if (0x2B8 + 4 > phSz)
            {
                LogModule.report(logvisor::Error, _S("unable to read data pointer"));
                return -1;
            }
            memmove(&dataPtr, phBuf + 0x2B8, 4);
            dataOff = uint64_t(SBig(dataPtr)) << 2;
        }
        m_userOffset = dataOff;

        std::unique_ptr<uint8_t[]> tmdData(new uint8_t[tmdSz]);
        {
            if (0x2C0 + tmdSz > phSz)
            {
                LogModule.report(logvisor::Error, _S("unable to read TMD"));
                return -1;
            }
            memmove(tmdData.get(), phBuf + 0x2C0, tmdSz);
        }

        /* Copy partition head up to H3 table */
        std::unique_ptr<IFileIO::IWriteStream> ws = m_parent.getFileIO().beginWriteStream(m_baseOffset);
        if (!ws)
            return -1;
        size_t copySz = std::min(phSz, size_t(h3Off));
        ws->write(phBuf, copySz);
        size_t remCopy = (h3Off > phSz) ? (h3Off - copySz) : 0;
        for (size_t i=0 ; i<remCopy ; ++i)
            ws->write("", 1);

        /* Prepare crypto pass */
        m_aes->setKey(COMMON_KEYS[ccIdx]);
        m_aes->decrypt(tkeyiv, tkey, tkey, 16);
        m_aes->setKey(tkey);

        {
            /* Assemble partition data */
            std::unique_ptr<IPartWriteStream> cws = beginWriteStream(0x1F0000);
            if (!cws)
                return -1;
            if (!contentFunc(*cws))
                return -1;

            /* Pad out user area to nearest cleartext sector */
            m_curUser = cws->position();
            uint64_t curUserRem = m_curUser % 0x1F0000;
            if (curUserRem)
            {
                curUserRem = 0x1F0000 - curUserRem;
                for (size_t i=0 ; i<curUserRem ; ++i)
                    cws->write("\xff", 1);
                m_curUser += curUserRem;
            }

            /* Begin crypto write and add content header */
            cws = beginWriteStream(0);
            if (!cws)
                return -1;
            Header header(m_gameID, m_gameTitle.c_str(), true, 0, 0, 0);
            header.write(*cws);

            /* Compute boot table members and write */
            size_t fstOff = 0x2440 + ROUND_UP_32(apploaderSz);
            size_t fstSz = sizeof(FSTNode) * m_buildNodes.size();
            fstSz += m_buildNameOff;
            fstSz = ROUND_UP_32(fstSz);

            if (fstOff + fstSz >= 0x1F0000)
            {
                LogModule.report(logvisor::Error,
                                 "FST flows into user area (one or the other is too big)");
                return -1;
            }

            cws->write(nullptr, 0x420 - sizeof(Header));
            uint32_t vals[4];
            vals[0] = SBig(uint32_t(m_dolOffset >> uint64_t(2)));
            vals[1] = SBig(uint32_t(fstOff >> uint64_t(2)));
            vals[2] = SBig(uint32_t(fstSz));
            vals[3] = SBig(uint32_t(fstSz));
            cws->write(vals, 16);

            size_t xferSz = 0;
            if (!apploaderFunc(*cws, xferSz))
                return -1;

            size_t fstOffRel = fstOff - 0x2440;
            if (xferSz > fstOffRel)
            {
                LogModule.report(logvisor::Error, "apploader unexpectedly flows into FST");
                return -1;
            }
            for (size_t i=0 ; i<fstOffRel-xferSz ; ++i)
                cws->write("\xff", 1);

            /* Write FST */
            cws->write(m_buildNodes.data(), m_buildNodes.size() * sizeof(FSTNode));
            for (const std::string& str : m_buildNames)
                cws->write(str.data(), str.size()+1);
        }

        /* Write new crypto content size */
        uint64_t groupCount = m_curUser / 0x1F0000;
        uint64_t cryptContentSize = (groupCount * 0x200000) >> uint64_t(2);
        uint32_t cryptContentSizeBig = SBig(uint32_t(cryptContentSize));
        ws = m_parent.getFileIO().beginWriteStream(m_baseOffset + 0x2BC);
        if (!ws)
            return -1;
        ws->write(&cryptContentSizeBig, 0x4);

        /* Write new H3 */
        ws = m_parent.getFileIO().beginWriteStream(m_baseOffset + h3Off);
        if (!ws)
            return -1;
        ws->write(m_h3, 0x18000);

        /* Compute content hash and replace in TMD */
        sha1nfo sha;
        sha1_init(&sha);
        sha1_write(&sha, (char*)m_h3, 0x18000);
        memmove(tmdData.get() + 0x1F4, sha1_result(&sha), 20);

        /* Same for content size */
        uint64_t contentSize = groupCount * 0x1F0000;
        uint64_t contentSizeBig = SBig(contentSize);
        memmove(tmdData.get() + 0x1EC, &contentSizeBig, 8);

        /* Zero-out TMD signature to simplify brute-force */
        memset(tmdData.get() + 0x4, 0, 0x100);

        /* Brute-force zero-starting hash */
        size_t tmdCheckSz = tmdSz - 0x140;
        struct BFWindow
        {
            uint64_t word[7];
        }* bfWindow = (BFWindow*)(tmdData.get() + 0x19A);
        bool good = false;
        uint64_t attempts = 0;
        SystemString bfName(_S("Brute force attempts"));
        for (int w=0 ; w<7 ; ++w)
        {
            for (uint64_t i=0 ; i<UINT64_MAX ; ++i)
            {
                bfWindow->word[w] = i;
                sha1_init(&sha);
                sha1_write(&sha, (char*)(tmdData.get() + 0x140), tmdCheckSz);
                uint8_t* hash = sha1_result(&sha);
                ++attempts;
                if (hash[0] == 0)
                {
                    good = true;
                    break;
                }
                m_parent.m_progressCB(m_parent.getProgressFactor(), bfName, attempts);
            }
            if (good)
                break;
        }
        m_parent.m_progressCB(m_parent.getProgressFactor(), bfName, attempts);
        ++m_parent.m_progressIdx;

        ws = m_parent.getFileIO().beginWriteStream(m_baseOffset + 0x2C0);
        if (!ws)
            return -1;
        ws->write(tmdData.get(), tmdSz);

        return m_baseOffset + dataOff + groupCount * 0x200000;
    }

    uint64_t buildFromDirectory(const SystemChar* dirIn,
                                const SystemChar* dolIn,
                                const SystemChar* apploaderIn,
                                const SystemChar* partHeadIn)
    {
        std::unique_ptr<IFileIO> ph = NewFileIO(partHeadIn);
        size_t phSz = ph->size();
        std::unique_ptr<uint8_t[]> phBuf(new uint8_t[phSz]);
        {
            auto rs = ph->beginReadStream();
            if (!rs)
                return -1;
            rs->read(phBuf.get(), phSz);
        }

        /* Get Apploader Size */
        Sstat theStat;
        if (Stat(apploaderIn, &theStat))
        {
            LogModule.report(logvisor::Error, _S("unable to stat %s"), apploaderIn);
            return -1;
        }

        return _build(
        [this, dirIn, dolIn, apploaderIn](IPartWriteStream& cws) -> bool
        {
            return DiscBuilderBase::PartitionBuilderBase::buildFromDirectory(cws, dirIn, dolIn, apploaderIn);
        },
        [this, apploaderIn](IPartWriteStream& cws, size_t& xferSz) -> bool
        {
            cws.write(nullptr, 0x2440 - 0x430);
            std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(apploaderIn)->beginReadStream();
            if (!rs)
                return false;
            char buf[8192];
            SystemString apploaderName(apploaderIn);
            while (true)
            {
                size_t rdSz = rs->read(buf, 8192);
                if (!rdSz)
                    break;
                cws.write(buf, rdSz);
                xferSz += rdSz;
                if (0x2440 + xferSz >= 0x1F0000)
                {
                    LogModule.report(logvisor::Error,
                                     "apploader flows into user area (one or the other is too big)");
                    return false;
                }
                m_parent.m_progressCB(m_parent.getProgressFactor(), apploaderName, xferSz);
            }
            ++m_parent.m_progressIdx;
            return true;
        }, phBuf.get(), phSz, theStat.st_size);
    }

    bool mergeFromDirectory(const PartitionWii* partIn, const SystemChar* dirIn)
    {
        size_t phSz;
        std::unique_ptr<uint8_t[]> phBuf = partIn->readPartitionHeaderBuf(phSz);

        return _build(
        [this, partIn, dirIn](IPartWriteStream& cws) -> bool
        {
            return DiscBuilderBase::PartitionBuilderBase::mergeFromDirectory(cws, partIn, dirIn);
        },
        [this, partIn](IPartWriteStream& cws, size_t& xferSz) -> bool
        {
            cws.write(nullptr, 0x2440 - 0x430);
            std::unique_ptr<uint8_t[]> apploaderBuf = partIn->getApploaderBuf();
            size_t apploaderSz = partIn->getApploaderSize();
            SystemString apploaderName(_S("<apploader>"));
            cws.write(apploaderBuf.get(), apploaderSz);
            xferSz += apploaderSz;
            if (0x2440 + xferSz >= 0x1F0000)
            {
                LogModule.report(logvisor::Error,
                                 "apploader flows into user area (one or the other is too big)");
                return false;
            }
            m_parent.m_progressCB(m_parent.getProgressFactor(), apploaderName, xferSz);
            ++m_parent.m_progressIdx;
            return true;
        }, phBuf.get(), phSz, partIn->getApploaderSize());
    }
};

EBuildResult DiscBuilderWii::buildFromDirectory(const SystemChar* dirIn, const SystemChar* dolIn,
                                                const SystemChar* apploaderIn, const SystemChar* partHeadIn)
{
    PartitionBuilderWii& pb = static_cast<PartitionBuilderWii&>(*m_partitions[0]);
    uint64_t filledSz = pb.m_baseOffset;
    if (!m_fileIO->beginWriteStream())
        return EBuildResult::Failed;

    if (!CheckFreeSpace(m_outPath.c_str(), m_discCapacity))
    {
        LogModule.report(logvisor::Error, _S("not enough free disk space for %s"), m_outPath.c_str());
        return EBuildResult::DiskFull;
    }
    m_progressCB(getProgressFactor(), _S("Preallocating image"), -1);
    ++m_progressIdx;
    std::unique_ptr<IFileIO::IWriteStream> ws = m_fileIO->beginWriteStream(m_discCapacity - 1);
    if (!ws)
        return EBuildResult::Failed;
    ws->write("", 1);

    /* Assemble image */
    filledSz = pb.buildFromDirectory(dirIn, dolIn, apploaderIn, partHeadIn);
    if (filledSz == -1)
        return EBuildResult::Failed;
    else if (filledSz >= uint64_t(m_discCapacity))
    {
        LogModule.report(logvisor::Error, "data partition exceeds disc capacity");
        return EBuildResult::Failed;
    }

    m_progressCB(getProgressFactor(), _S("Finishing Disc"), -1);
    ++m_progressIdx;

    /* Populate disc header */
    ws = m_fileIO->beginWriteStream(0);
    if (!ws)
        return EBuildResult::Failed;
    Header header(pb.getGameID(), pb.getGameTitle().c_str(), true, 0, 0, 0);
    header.write(*ws);

    /* Populate partition info */
    ws = m_fileIO->beginWriteStream(0x40000);
    if (!ws)
        return EBuildResult::Failed;
    uint32_t vals[2] = {SBig(uint32_t(1)), SBig(uint32_t(0x40020 >> uint64_t(2)))};
    ws->write(vals, 8);

    ws = m_fileIO->beginWriteStream(0x40020);
    if (!ws)
        return EBuildResult::Failed;
    vals[0] = SBig(uint32_t(pb.m_baseOffset >> uint64_t(2)));
    ws->write(vals, 4);

    /* Populate region info */
    ws = m_fileIO->beginWriteStream(0x4E000);
    if (!ws)
        return EBuildResult::Failed;
    const char* gameID = pb.getGameID();
    if (gameID[3] == 'P')
        vals[0] = SBig(uint32_t(2));
    else if (gameID[3] == 'J')
        vals[0] = SBig(uint32_t(0));
    else
        vals[0] = SBig(uint32_t(1));
    ws->write(vals, 4);

    /* Make disc unrated */
    ws = m_fileIO->beginWriteStream(0x4E010);
    if (!ws)
        return EBuildResult::Failed;
    for (int i=0 ; i<16 ; ++i)
        ws->write("\x80", 1);

    /* Fill image to end */
    ws = m_fileIO->beginWriteStream(filledSz);
    if (!ws)
        return EBuildResult::Failed;
    uint8_t fillBuf[512];
    memset(fillBuf, 0xff, 512);
    for (size_t i=m_discCapacity-filledSz ; i>0 ;)
    {
        if (i >= 512)
        {
            ws->write(fillBuf, 512);
            i -= 512;
            continue;
        }
        ws->write(fillBuf, i);
        break;
    }

    return EBuildResult::Success;
}

uint64_t DiscBuilderWii::CalculateTotalSizeRequired(const SystemChar* dirIn, const SystemChar* dolIn,
                                                    bool& dualLayer)
{
    uint64_t sz = DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeBuild(dolIn, dirIn);
    if (sz == -1)
        return -1;
    auto szDiv = std::lldiv(sz, 0x1F0000);
    if (szDiv.rem) ++szDiv.quot;
    sz = szDiv.quot * 0x200000;
    sz += 0x200000;
    dualLayer = (sz > 0x118240000);
    if (sz > 0x1FB4E0000)
    {
        LogModule.report(logvisor::Error, _S("disc capacity exceeded [%" PRIu64 " / %" PRIu64 "]"), sz, 0x1FB4E0000);
        return -1;
    }
    return sz;
}

DiscBuilderWii::DiscBuilderWii(const SystemChar* outPath, const char gameID[6], const char* gameTitle,
                               bool dualLayer, FProgress progressCB)
: DiscBuilderBase(outPath, dualLayer ? 0x1FB4E0000 : 0x118240000, progressCB), m_dualLayer(dualLayer)
{
    PartitionBuilderWii* partBuilder = new PartitionBuilderWii(*this, PartitionBuilderBase::Kind::Data,
                                                               gameID, gameTitle, 0x200000);
    m_partitions.emplace_back(partBuilder);
}

DiscMergerWii::DiscMergerWii(const SystemChar* outPath, DiscWii& sourceDisc,
                             bool dualLayer, FProgress progressCB)
: m_sourceDisc(sourceDisc), m_builder(sourceDisc.makeMergeBuilder(outPath, dualLayer, progressCB))
{}

EBuildResult DiscMergerWii::mergeFromDirectory(const SystemChar* dirIn)
{
    PartitionBuilderWii& pb = static_cast<PartitionBuilderWii&>(*m_builder.m_partitions[0]);
    uint64_t filledSz = pb.m_baseOffset;
    if (!m_builder.m_fileIO->beginWriteStream())
        return EBuildResult::Failed;

    if (!CheckFreeSpace(m_builder.m_outPath.c_str(), m_builder.m_discCapacity))
    {
        LogModule.report(logvisor::Error, _S("not enough free disk space for %s"), m_builder.m_outPath.c_str());
        return EBuildResult::DiskFull;
    }
    m_builder.m_progressCB(m_builder.getProgressFactor(), _S("Preallocating image"), -1);
    ++m_builder.m_progressIdx;
    std::unique_ptr<IFileIO::IWriteStream> ws = m_builder.m_fileIO->beginWriteStream(m_builder.m_discCapacity - 1);
    if (!ws)
        return EBuildResult::Failed;
    ws->write("", 1);

    /* Assemble image */
    filledSz = pb.mergeFromDirectory(static_cast<PartitionWii*>(m_sourceDisc.getDataPartition()), dirIn);
    if (filledSz == -1)
        return EBuildResult::Failed;
    else if (filledSz >= uint64_t(m_builder.m_discCapacity))
    {
        LogModule.report(logvisor::Error, "data partition exceeds disc capacity");
        return EBuildResult::Failed;
    }

    m_builder.m_progressCB(m_builder.getProgressFactor(), _S("Finishing Disc"), -1);
    ++m_builder.m_progressIdx;

    /* Populate disc header */
    ws = m_builder.m_fileIO->beginWriteStream(0);
    if (!ws)
        return EBuildResult::Failed;
    m_sourceDisc.getHeader().write(*ws);

    /* Populate partition info */
    ws = m_builder.m_fileIO->beginWriteStream(0x40000);
    if (!ws)
        return EBuildResult::Failed;
    uint32_t vals[2] = {SBig(uint32_t(1)), SBig(uint32_t(0x40020 >> uint64_t(2)))};
    ws->write(vals, 8);

    ws = m_builder.m_fileIO->beginWriteStream(0x40020);
    if (!ws)
        return EBuildResult::Failed;
    vals[0] = SBig(uint32_t(pb.m_baseOffset >> uint64_t(2)));
    ws->write(vals, 4);

    /* Populate region info */
    ws = m_builder.m_fileIO->beginWriteStream(0x4E000);
    if (!ws)
        return EBuildResult::Failed;
    const char* gameID = pb.getGameID();
    if (gameID[3] == 'P')
        vals[0] = SBig(uint32_t(2));
    else if (gameID[3] == 'J')
        vals[0] = SBig(uint32_t(0));
    else
        vals[0] = SBig(uint32_t(1));
    ws->write(vals, 4);

    /* Make disc unrated */
    ws = m_builder.m_fileIO->beginWriteStream(0x4E010);
    if (!ws)
        return EBuildResult::Failed;
    for (int i=0 ; i<16 ; ++i)
        ws->write("\x80", 1);

    /* Fill image to end */
    ws = m_builder.m_fileIO->beginWriteStream(filledSz);
    if (!ws)
        return EBuildResult::Failed;
    uint8_t fillBuf[512];
    memset(fillBuf, 0xff, 512);
    for (size_t i=m_builder.m_discCapacity-filledSz ; i>0 ;)
    {
        if (i >= 512)
        {
            ws->write(fillBuf, 512);
            i -= 512;
            continue;
        }
        ws->write(fillBuf, i);
        break;
    }

    return EBuildResult::Success;
}

uint64_t DiscMergerWii::CalculateTotalSizeRequired(DiscWii& sourceDisc,
                                                   const SystemChar* dirIn, bool& dualLayer)
{
    uint64_t sz = DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeMerge(
                  sourceDisc.getDataPartition(), dirIn);
    if (sz == -1)
        return -1;
    auto szDiv = std::lldiv(sz, 0x1F0000);
    if (szDiv.rem) ++szDiv.quot;
    sz = szDiv.quot * 0x200000;
    sz += 0x200000;
    dualLayer = (sz > 0x118240000);
    if (sz > 0x1FB4E0000)
    {
        LogModule.report(logvisor::Error, _S("disc capacity exceeded [%" PRIu64 " / %" PRIu64 "]"), sz, 0x1FB4E0000);
        return -1;
    }
    return sz;
}

}
