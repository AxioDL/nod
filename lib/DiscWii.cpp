#include <stdio.h>
#include <string.h>
#include "NOD/DiscWii.hpp"
#include "NOD/aes.hpp"
#include "NOD/sha1.h"

namespace NOD
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
    PartitionWii(const DiscWii& parent, Kind kind, uint64_t offset)
    : IPartition(parent, kind, offset)
    {
        std::unique_ptr<IDiscIO::IReadStream> s = parent.getDiscIO().beginReadStream(offset);
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
        uint32_t vals[3];
        ds->read(vals, 12);
        m_dolOff = SBig(vals[0]) << 2;
        m_fstOff = SBig(vals[1]) << 2;
        m_fstSz = SBig(vals[2]) << 2;
        ds->seek(0x2440 + 0x14);
        ds->read(vals, 8);
        m_apploaderSz = 32 + SBig(vals[0]) + SBig(vals[1]);

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
        PartReadStream(const PartitionWii& parent, uint64_t baseOffset, uint64_t offset)
        : m_aes(NewAES()), m_parent(parent), m_baseOffset(baseOffset), m_offset(offset)
        {
            m_aes->setKey(parent.m_decKey);
            size_t block = m_offset / 0x7c00;
            m_dio = m_parent.m_parent.getDiscIO().beginReadStream(m_baseOffset + block * 0x8000);
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
        return std::unique_ptr<IPartReadStream>(new PartReadStream(*this, m_dataOff, offset));
    }

    uint64_t normalizeOffset(uint64_t anOffset) const {return anOffset << 2;}

    void writeOutPartitionHeader(const SystemChar* pathOut) const
    {
        FILE* fp = Fopen(pathOut, _S("wb"), FileLockType::Write);
        if (!fp)
            LogModule.report(LogVisor::FatalError, _S("unable to open %s for writing"), pathOut);

        uint64_t h3Off;
        {
            std::unique_ptr<IDiscIO::IReadStream> rs = m_parent.getDiscIO().beginReadStream(m_offset + 0x2B4);
            uint32_t h3;
            if (rs->read(&h3, 4) != 4)
                LogModule.report(LogVisor::FatalError, _S("unable to read H3 offset from %s"), pathOut);
            h3 = SBig(h3);
            h3Off = uint64_t(h3) << 2;
        }

        char buf[8192];
        size_t rem = h3Off;
        std::unique_ptr<IDiscIO::IReadStream> rs = m_parent.getDiscIO().beginReadStream(m_offset);
        while (rem)
        {
            size_t rdSz = std::min(rem, 8192ul);
            rs->read(buf, rdSz);
            fwrite(buf, 1, rdSz, fp);
            rem -= rdSz;
        }

        fclose(fp);
    }
};

DiscWii::DiscWii(std::unique_ptr<IDiscIO>&& dio)
: DiscBase(std::move(dio))
{
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
        PartInfo(IDiscIO& dio)
        {
            std::unique_ptr<IDiscIO::IReadStream> s = dio.beginReadStream(0x40000);
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
    } partInfo(*m_discIO);

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
            LogModule.report(LogVisor::FatalError, "invalid partition type %d", part.partType);
        }
        m_partitions.emplace_back(new PartitionWii(*this, kind, part.partDataOff << 2));
    }
}

void DiscWii::writeOutDataPartitionHeader(const SystemChar* pathOut) const
{
    for (const std::unique_ptr<IPartition>& part : m_partitions)
    {
        if (part->getKind() == IPartition::Kind::Data)
        {
            static_cast<PartitionWii&>(*part).writeOutPartitionHeader(pathOut);
            break;
        }
    }
}

static const uint8_t ZEROIV[16] = {0};

class PartitionBuilderWii : public DiscBuilderBase::PartitionBuilderBase
{
    friend class DiscBuilderWii;
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
                LogModule.report(LogVisor::FatalError, "unable to write full disc group");
        }

    public:
        PartWriteStream(PartitionBuilderWii& parent, uint64_t baseOffset, uint64_t offset)
        : m_parent(parent), m_baseOffset(baseOffset), m_offset(offset)
        {
            if (offset % 0x1F0000)
                LogModule.report(LogVisor::FatalError, "partition write stream MUST begin on 0x1F0000-aligned boundary");
            size_t group = m_offset / 0x1F0000;
            m_fio = m_parent.m_parent.getFileIO().beginWriteStream(m_baseOffset + group * 0x200000);
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
            LogModule.report(LogVisor::FatalError, "partition exceeds maximum single-partition capacity");
            return -1;
        }
        uint64_t ret = m_curUser;
        PartWriteStream& cws = static_cast<PartWriteStream&>(ws);
        if (cws.m_offset > ret)
        {
            LogModule.report(LogVisor::FatalError, "partition overwrite error");
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
        return std::make_unique<PartWriteStream>(*this, m_baseOffset + m_userOffset, offset);
    }

    uint64_t buildFromDirectory(const SystemChar* dirIn,
                                const SystemChar* dolIn,
                                const SystemChar* apploaderIn,
                                const SystemChar* partHeadIn)
    {
        /* Read head and validate key members */
        std::unique_ptr<IFileIO> ph = NewFileIO(partHeadIn);

        uint8_t tkey[16];
        {
            if (ph->beginReadStream(0x1BF)->read(tkey, 16) != 16)
                LogModule.report(LogVisor::FatalError, _S("unable to read title key from %s"), partHeadIn);
        }

        uint8_t tkeyiv[16] = {};
        {
            if (ph->beginReadStream(0x1DC)->read(tkeyiv, 8) != 8)
                LogModule.report(LogVisor::FatalError, _S("unable to read title key IV from %s"), partHeadIn);
        }

        uint8_t ccIdx;
        {
            if (ph->beginReadStream(0x1F1)->read(&ccIdx, 1) != 1)
                LogModule.report(LogVisor::FatalError, _S("unable to read common key index from %s"), partHeadIn);
            if (ccIdx > 1)
                LogModule.report(LogVisor::FatalError, _S("common key index may only be 0 or 1"));
        }

        uint32_t tmdSz;
        {
            if (ph->beginReadStream(0x2A4)->read(&tmdSz, 4) != 4)
                LogModule.report(LogVisor::FatalError, _S("unable to read TMD size from %s"), partHeadIn);
            tmdSz = SBig(tmdSz);
        }

        uint64_t h3Off;
        {
            uint32_t h3Ptr;
            if (ph->beginReadStream(0x2B4)->read(&h3Ptr, 4) != 4)
                LogModule.report(LogVisor::FatalError, _S("unable to read H3 pointer from %s"), partHeadIn);
            h3Off = uint64_t(SBig(h3Ptr)) << 2;
        }

        uint64_t dataOff;
        {
            uint32_t dataPtr;
            if (ph->beginReadStream(0x2B8)->read(&dataPtr, 4) != 4)
                LogModule.report(LogVisor::FatalError, _S("unable to read data pointer from %s"), partHeadIn);
            dataOff = uint64_t(SBig(dataPtr)) << 2;
        }
        m_userOffset = dataOff;

        std::unique_ptr<uint8_t[]> tmdData(new uint8_t[tmdSz]);
        if (ph->beginReadStream(0x2C0)->read(tmdData.get(), tmdSz) != tmdSz)
            LogModule.report(LogVisor::FatalError, _S("unable to read TMD from %s"), partHeadIn);

        /* Copy partition head up to H3 table */
        std::unique_ptr<IFileIO::IWriteStream> ws = m_parent.getFileIO().beginWriteStream(m_baseOffset);
        {
            uint64_t remCopy = h3Off;

            uint8_t copyBuf[8192];
            std::unique_ptr<IFileIO::IReadStream> rs = ph->beginReadStream();
            while (remCopy)
            {
                size_t rdBytes = rs->read(copyBuf, std::min(size_t(8192), size_t(remCopy)));
                if (rdBytes)
                {
                    ws->write(copyBuf, rdBytes);
                    remCopy -= rdBytes;
                    continue;
                }
                for (size_t i=0 ; i<remCopy ; ++i)
                    ws->write("", 1);
                break;
            }
        }

        /* Prepare crypto pass */
        m_aes->setKey(COMMON_KEYS[ccIdx]);
        m_aes->decrypt(tkeyiv, tkey, tkey, 16);
        m_aes->setKey(tkey);

        {
            /* Assemble partition data */
            std::unique_ptr<IPartWriteStream> cws = beginWriteStream(0x1F0000);
            bool result = DiscBuilderBase::PartitionBuilderBase::buildFromDirectory(*cws, dirIn, dolIn, apploaderIn);
            if (!result)
                return 0;

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
            Header header(m_gameID, m_gameTitle.c_str(), true, 0, 0, 0);
            header.write(*cws);

            /* Get Apploader Size */
            Sstat theStat;
            if (Stat(apploaderIn, &theStat))
                LogModule.report(LogVisor::FatalError, _S("unable to stat %s"), apploaderIn);

            /* Compute boot table members and write */
            size_t fstOff = 0x2440 + ROUND_UP_32(theStat.st_size);
            size_t fstSz = sizeof(FSTNode) * m_buildNodes.size();
            fstSz += m_buildNameOff;
            fstSz = ROUND_UP_32(fstSz);

            if (fstOff + fstSz >= 0x1F0000)
                LogModule.report(LogVisor::FatalError,
                                 "FST flows into user area (one or the other is too big)");

            cws->write(nullptr, 0x420 - sizeof(Header));
            uint32_t vals[4];
            vals[0] = SBig(uint32_t(m_dolOffset >> uint64_t(2)));
            vals[1] = SBig(uint32_t(fstOff >> uint64_t(2)));
            vals[2] = SBig(uint32_t(fstSz));
            vals[3] = SBig(uint32_t(fstSz));
            cws->write(vals, 16);

            /* Write Apploader */
            cws->write(nullptr, 0x2440 - 0x430);
            std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(apploaderIn)->beginReadStream();
            char buf[8192];
            size_t xferSz = 0;
            SystemString apploaderName(apploaderIn);
            ++m_parent.m_progressIdx;
            while (true)
            {
                size_t rdSz = rs->read(buf, 8192);
                if (!rdSz)
                    break;
                cws->write(buf, rdSz);
                xferSz += rdSz;
                if (0x2440 + xferSz >= 0x1F0000)
                    LogModule.report(LogVisor::FatalError,
                                     "apploader flows into user area (one or the other is too big)");
                m_parent.m_progressCB(m_parent.m_progressIdx, apploaderName, xferSz);
            }

            size_t fstOffRel = fstOff - 0x2440;
            if (xferSz > fstOffRel)
                LogModule.report(LogVisor::FatalError, "apploader unexpectedly flows into FST");
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
        ws->write(&cryptContentSizeBig, 0x4);

        /* Write new H3 */
        ws = m_parent.getFileIO().beginWriteStream(m_baseOffset + h3Off);
        ws->write(m_h3, 0x18000);

        /* Compute content hash and replace in TMD */
        sha1nfo sha;
        sha1_init(&sha);
        sha1_write(&sha, (char*)m_h3, 0x18000);
        memcpy(tmdData.get() + 0x1F4, sha1_result(&sha), 20);

        /* Same for content size */
        uint64_t contentSize = groupCount * 0x1F0000;
        uint64_t contentSizeBig = SBig(contentSize);
        memcpy(tmdData.get() + 0x1EC, &contentSizeBig, 8);

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
        ++m_parent.m_progressIdx;
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
                m_parent.m_progressCB(m_parent.m_progressIdx, bfName, attempts);
            }
            if (good)
                break;
        }
        m_parent.m_progressCB(m_parent.m_progressIdx, bfName, attempts);

        ws = m_parent.getFileIO().beginWriteStream(m_baseOffset + 0x2C0);
        ws->write(tmdData.get(), tmdSz);

        return m_baseOffset + dataOff + groupCount * 0x200000;
    }
};

bool DiscBuilderWii::buildFromDirectory(const SystemChar* dirIn, const SystemChar* dolIn,
                                        const SystemChar* apploaderIn, const SystemChar* partHeadIn)
{
    size_t DISC_CAPACITY = m_dualLayer ? 0x1FB4E0000 : 0x118240000;

    PartitionBuilderWii& pb = static_cast<PartitionBuilderWii&>(*m_partitions[0]);
    uint64_t filledSz = pb.m_baseOffset;
    m_fileIO->beginWriteStream();

    if (!CheckFreeSpace(m_outPath, DISC_CAPACITY))
    {
        LogModule.report(LogVisor::Error, _S("not enough free disk space for %s"), m_outPath);
        return false;
    }

    /* Assemble image */
    filledSz = pb.buildFromDirectory(dirIn, dolIn, apploaderIn, partHeadIn);
    if (filledSz >= DISC_CAPACITY)
    {
        LogModule.report(LogVisor::FatalError, "data partition exceeds disc capacity");
        return false;
    }

    ++m_progressIdx;
    m_progressCB(m_progressIdx, "Finishing Disc", -1);

    /* Populate disc header */
    std::unique_ptr<IFileIO::IWriteStream> ws = m_fileIO->beginWriteStream(0);
    Header header(pb.getGameID(), pb.getGameTitle().c_str(), true, 0, 0, 0);
    header.write(*ws);

    /* Populate partition info */
    ws = m_fileIO->beginWriteStream(0x40000);
    uint32_t vals[2] = {SBig(uint32_t(1)), SBig(uint32_t(0x40020 >> uint64_t(2)))};
    ws->write(vals, 8);

    ws = m_fileIO->beginWriteStream(0x40020);
    vals[0] = SBig(uint32_t(pb.m_baseOffset >> uint64_t(2)));
    ws->write(vals, 4);

    /* Populate region info */
    ws = m_fileIO->beginWriteStream(0x4E000);
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
    for (int i=0 ; i<16 ; ++i)
        ws->write("\x80", 1);

    /* Fill image to end */
    ws = m_fileIO->beginWriteStream(filledSz);
    for (size_t i=0 ; i<DISC_CAPACITY-filledSz ; ++i)
        ws->write("\xff", 1);

    return true;
}

DiscBuilderWii::DiscBuilderWii(const SystemChar* outPath, const char gameID[6], const char* gameTitle, bool dualLayer,
                               std::function<void(size_t, const SystemString&, size_t)> progressCB)
: DiscBuilderBase(outPath, progressCB), m_dualLayer(dualLayer)
{
    PartitionBuilderWii* partBuilder = new PartitionBuilderWii(*this, PartitionBuilderBase::Kind::Data,
                                                               gameID, gameTitle, 0x200000);
    m_partitions.emplace_back(partBuilder);
}

}
