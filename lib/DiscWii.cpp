#include <stdio.h>
#include "DiscWii.hpp"

namespace NOD
{

class PartitionWii : public DiscBase::IPartition
{
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
        uint32_t sigType;
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
        uint32_t titleIdMinor;
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
            sigType = SBig(sigType);
            iosIdMajor = SBig(iosIdMajor);
            iosIdMinor = SBig(iosIdMinor);
            titleIdMajor = SBig(titleIdMajor);
            titleIdMinor = SBig(titleIdMinor);
            titleType = SBig(titleType);
            groupId = SBig(groupId);
            accessFlags = SBig(accessFlags);
            titleVersion = SBig(titleVersion);
            numContents = SBig(numContents);
            bootIdx = SBig(bootIdx);
            for (size_t c=0 ; c<numContents ; ++c)
            {
                contents.emplace_back();
                contents.back().read(s);
            }
        }
    } m_tmd;

    struct Certificate
    {
        uint32_t sigType;
        char sig[512];
        char issuer[64];
        uint32_t keyType;
        char subject[64];
        char key[512];
        uint32_t modulus;
        uint32_t pubExp;

        void read(IDiscIO::IReadStream& s)
        {
            s.read(&sigType, 4);
            sigType = SBig(sigType);
            if (sigType == 0x00010000)
                s.read(sig, 512);
            else if (sigType == 0x00010001)
                s.read(sig, 256);
            else if (sigType == 0x00010002)
                s.read(sig, 64);
            s.seek(60, SEEK_CUR);

            s.read(issuer, 64);
            s.read(&keyType, 4);
            s.read(subject, 64);
            keyType = SBig(keyType);
            if (keyType == 0x00000000)
                s.read(key, 512);
            else if (keyType == 0x00000001)
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

public:
    PartitionWii(const DiscWii& parent, Kind kind, size_t offset)
    : IPartition(parent, kind, offset)
    {
        std::unique_ptr<IDiscIO::IReadStream> s = parent.getDiscIO().beginReadStream(offset);
        m_ticket.read(*s.get());

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
        uint32_t dataSize;
        s->read(&dataSize, 4);
        dataSize = SBig(dataSize) << 2;

        s->seek(offset + tmdOff);
        m_tmd.read(*s.get());

        s->seek(offset + certChainOff);
        m_caCert.read(*s.get());
        m_tmdCert.read(*s.get());
        m_ticketCert.read(*s.get());

    }
};

DiscWii::DiscWii(std::unique_ptr<IDiscIO>&& dio)
: DiscBase(std::move(dio))
{
    /* Read partition info */
    struct PartInfo {
        uint32_t partCount;
        uint32_t partInfoOff;
        struct Part
        {
            uint32_t partDataOff;
            enum Type : uint32_t
            {
                PART_DATA = 0,
                PART_UPDATE = 1,
                PART_CHANNEL = 2
            } partType;
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
                parts[p].partType = (Part::Type)SBig(parts[p].partType);
            }
        }
    } partInfo(*m_discIO.get());

    /* Iterate for data partition */
    m_partitions.reserve(partInfo.partCount);
    for (uint32_t p=0 ; p<partInfo.partCount && p<4 ; ++p)
    {
        PartInfo::Part& part = partInfo.parts[p];
        IPartition::Kind kind;
        if (part.partType == PartInfo::Part::PART_DATA)
            kind = IPartition::PART_DATA;
        else if (part.partType == PartInfo::Part::PART_UPDATE)
            kind = IPartition::PART_UPDATE;
        else if (part.partType == PartInfo::Part::PART_CHANNEL)
            kind = IPartition::PART_CHANNEL;
        else
            throw std::runtime_error("Invalid partition type");
        m_partitions.emplace_back(new PartitionWii(*this, kind, part.partDataOff << 2));
    }
}

bool DiscWii::commit()
{
    return false;
}

}
