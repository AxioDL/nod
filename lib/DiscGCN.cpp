#include "NOD/DiscGCN.hpp"
#define BUFFER_SZ 0x8000

namespace NOD
{

class PartitionGCN : public DiscBase::IPartition
{
public:
    PartitionGCN(const DiscGCN& parent, Kind kind, uint64_t offset)
    : IPartition(parent, kind, offset)
    {
        /* GCN-specific header reads */
        std::unique_ptr<IPartReadStream> s = beginReadStream(0x420);
        uint32_t vals[3];
        s->read(vals, 12);
        m_dolOff = SBig(vals[0]);
        m_fstOff = SBig(vals[1]);
        m_fstSz = SBig(vals[2]);
        s->seek(0x2440 + 0x14);
        s->read(vals, 8);
        m_apploaderSz = 32 + SBig(vals[0]) + SBig(vals[1]);

        /* Yay files!! */
        parseFST(*s);

        /* Also make DOL header and size handy */
        s->seek(m_dolOff);
        parseDOL(*s);
    }

    class PartReadStream : public IPartReadStream
    {
        const PartitionGCN& m_parent;
        std::unique_ptr<IDiscIO::IReadStream> m_dio;

        uint64_t m_offset;
        size_t m_curBlock = SIZE_MAX;
        uint8_t m_buf[BUFFER_SZ];

    public:
        PartReadStream(const PartitionGCN& parent, uint64_t offset)
        : m_parent(parent), m_offset(offset)
        {
            size_t block = m_offset / BUFFER_SZ;
            m_dio = m_parent.m_parent.getDiscIO().beginReadStream(block * BUFFER_SZ);
            m_dio->read(m_buf, BUFFER_SZ);
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
            size_t block = m_offset / BUFFER_SZ;
            if (block != m_curBlock)
            {
                m_dio->seek(block * BUFFER_SZ);
                m_dio->read(m_buf, BUFFER_SZ);
                m_curBlock = block;
            }
        }
        uint64_t position() const {return m_offset;}
        uint64_t read(void* buf, uint64_t length)
        {
            size_t block = m_offset / BUFFER_SZ;
            size_t cacheOffset = m_offset % BUFFER_SZ;
            uint64_t cacheSize;
            uint64_t rem = length;
            uint8_t* dst = (uint8_t*)buf;

            while (rem)
            {
                if (block != m_curBlock)
                {
                    m_dio->read(m_buf, BUFFER_SZ);
                    m_curBlock = block;
                }

                cacheSize = rem;
                if (cacheSize + cacheOffset > BUFFER_SZ)
                    cacheSize = BUFFER_SZ - cacheOffset;

                memcpy(dst, m_buf + cacheOffset, cacheSize);
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
        return std::unique_ptr<IPartReadStream>(new PartReadStream(*this, offset));
    }
};

DiscGCN::DiscGCN(std::unique_ptr<IDiscIO>&& dio)
: DiscBase(std::move(dio))
{
    /* One lone partition for GCN */
    m_partitions.emplace_back(new PartitionGCN(*this, IPartition::Kind::Data, 0));
}

class PartitionBuilderGCN : public DiscBuilderBase::PartitionBuilderBase
{
    uint64_t m_curUser = 0x57058000;
    uint32_t m_fstMemoryAddr;
public:
    PartitionBuilderGCN(DiscBuilderBase& parent, Kind kind,
                        const char gameID[6], const char* gameTitle, uint32_t fstMemoryAddr)
    : DiscBuilderBase::PartitionBuilderBase(parent, kind, gameID, gameTitle), m_fstMemoryAddr(fstMemoryAddr) {}

    uint64_t userAllocate(uint64_t reqSz)
    {
        m_curUser -= reqSz;
        m_curUser &= 0xfffffffffffffff0;
        if (m_curUser < 0x30000)
        {
            LogModule.report(LogVisor::FatalError, "user area low mark reached");
            return -1;
        }
        return m_curUser;
    }

    uint32_t packOffset(uint64_t offset) const
    {
        return offset;
    }

    bool buildFromDirectory(const SystemChar* dirIn, const SystemChar* dolIn, const SystemChar* apploaderIn)
    {
        bool result = DiscBuilderBase::PartitionBuilderBase::buildFromDirectory(dirIn, dolIn, apploaderIn);
        if (!result)
            return false;

        std::unique_ptr<IFileIO::IWriteStream> ws = m_parent.getFileIO().beginWriteStream(0);
        Header header(m_gameID, m_gameTitle.c_str(), false);
        header.write(*ws);

        ws = m_parent.getFileIO().beginWriteStream(0x2440);
        FILE* fp = Fopen(apploaderIn, _S("rb"), FileLockType::Read);
        if (!fp)
            LogModule.report(LogVisor::FatalError, _S("unable to open %s for reading"), apploaderIn);
        char buf[8192];
        size_t xferSz = 0;
        SystemString apploaderName(apploaderIn);
        ++m_parent.m_progressIdx;
        while (true)
        {
            size_t rdSz = fread(buf, 1, 8192, fp);
            if (!rdSz)
                break;
            ws->write(buf, rdSz);
            xferSz += rdSz;
            if (0x2440 + xferSz >= m_curUser)
                LogModule.report(LogVisor::FatalError,
                                 "apploader flows into user area (one or the other is too big)");
            m_parent.m_progressCB(m_parent.m_progressIdx, apploaderName, xferSz);
        }
        fclose(fp);

        size_t fstOff = ROUND_UP_32(xferSz);
        size_t fstSz = sizeof(FSTNode) * m_buildNodes.size();
        for (size_t i=0 ; i<fstOff-xferSz ; ++i)
            ws->write("\xff", 1);
        fstOff += 0x2440;
        ws->write(m_buildNodes.data(), fstSz);
        for (const std::string& str : m_buildNames)
            ws->write(str.data(), str.size()+1);
        fstSz += m_buildNameOff;
        fstSz = ROUND_UP_32(fstSz);

        if (fstOff + fstSz >= m_curUser)
            LogModule.report(LogVisor::FatalError,
                             "FST flows into user area (one or the other is too big)");

        ws = m_parent.getFileIO().beginWriteStream(0x420);
        uint32_t vals[7];
        vals[0] = SBig(uint32_t(m_dolOffset));
        vals[1] = SBig(uint32_t(fstOff));
        vals[2] = SBig(uint32_t(fstSz));
        vals[3] = SBig(uint32_t(fstSz));
        vals[4] = SBig(uint32_t(m_fstMemoryAddr));
        vals[5] = SBig(uint32_t(m_curUser));
        vals[6] = SBig(uint32_t(0x57058000 - m_curUser));
        ws->write(vals, sizeof(vals));

        return true;
    }
};

bool DiscBuilderGCN::buildFromDirectory(const SystemChar* dirIn, const SystemChar* dolIn,
                                        const SystemChar* apploaderIn)
{    
    PartitionBuilderGCN& pb = static_cast<PartitionBuilderGCN&>(*m_partitions[0]);
    return pb.buildFromDirectory(dirIn, dolIn, apploaderIn);
}

DiscBuilderGCN::DiscBuilderGCN(const SystemChar* outPath, const char gameID[6], const char* gameTitle,
                               uint32_t fstMemoryAddr, std::function<void(size_t, const SystemString&, size_t)> progressCB)
: DiscBuilderBase(std::move(NewFileIO(outPath)), progressCB)
{
    PartitionBuilderGCN* partBuilder = new PartitionBuilderGCN(*this, PartitionBuilderBase::Kind::Data,
                                                               gameID, gameTitle, fstMemoryAddr);
    m_partitions.emplace_back(partBuilder);
}

}
