#include "nod/DiscGCN.hpp"
#include <inttypes.h>
#define BUFFER_SZ 0x8000

namespace nod
{

class PartitionGCN : public DiscBase::IPartition
{
public:
    PartitionGCN(const DiscGCN& parent, Kind kind, uint64_t offset, bool& err)
    : IPartition(parent, kind, offset)
    {
        /* GCN-specific header reads */
        std::unique_ptr<IPartReadStream> s = beginReadStream(0x420);
        if (!s)
        {
            err = true;
            return;
        }
        uint32_t vals[5];
        s->read(vals, 5 * 4);
        m_dolOff = SBig(vals[0]);
        m_fstOff = SBig(vals[1]);
        m_fstSz = SBig(vals[2]);
        m_fstMemoryAddr = SBig(vals[4]);
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
        PartReadStream(const PartitionGCN& parent, uint64_t offset, bool& err)
        : m_parent(parent), m_offset(offset)
        {
            size_t block = m_offset / BUFFER_SZ;
            m_dio = m_parent.m_parent.getDiscIO().beginReadStream(block * BUFFER_SZ);
            if (!m_dio)
            {
                err = true;
                return;
            }
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
        bool Err = false;
        auto ret = std::unique_ptr<IPartReadStream>(new PartReadStream(*this, offset, Err));
        if (Err)
            return {};
        return ret;
    }
};

DiscGCN::DiscGCN(std::unique_ptr<IDiscIO>&& dio, bool& err)
: DiscBase(std::move(dio), err)
{
    if (err)
        return;

    /* One lone partition for GCN */
    m_partitions.emplace_back(new PartitionGCN(*this, IPartition::Kind::Data, 0, err));
}

DiscBuilderGCN DiscGCN::makeMergeBuilder(const SystemChar* outPath, FProgress progressCB)
{
    IPartition* dataPart = getDataPartition();
    return DiscBuilderGCN(outPath, m_header.m_gameID, m_header.m_gameTitle,
                          dataPart->getFSTMemoryAddr(), progressCB);
}

class PartitionBuilderGCN : public DiscBuilderBase::PartitionBuilderBase
{
    uint64_t m_curUser = 0x57058000;
    uint32_t m_fstMemoryAddr;

public:
    class PartWriteStream : public IPartWriteStream
    {
        const PartitionBuilderGCN& m_parent;
        uint64_t m_offset;
        std::unique_ptr<IFileIO::IWriteStream> m_fio;

    public:
        PartWriteStream(const PartitionBuilderGCN& parent, uint64_t offset, bool& err)
        : m_parent(parent), m_offset(offset)
        {
            m_fio = m_parent.m_parent.getFileIO().beginWriteStream(offset);
            if (!m_fio)
                err = true;
        }
        void close() {m_fio.reset();}
        uint64_t position() const {return m_offset;}
        uint64_t write(const void* buf, uint64_t length)
        {
            uint64_t len = m_fio->write(buf, length);
            m_offset += len;
            return len;
        }
        void seek(size_t off)
        {
            m_offset = off;
            m_fio = m_parent.m_parent.getFileIO().beginWriteStream(off);
        }
    };

    PartitionBuilderGCN(DiscBuilderBase& parent, Kind kind,
                        const char gameID[6], const char* gameTitle, uint32_t fstMemoryAddr)
    : DiscBuilderBase::PartitionBuilderBase(parent, kind, gameID, gameTitle), m_fstMemoryAddr(fstMemoryAddr) {}

    uint64_t userAllocate(uint64_t reqSz, IPartWriteStream& ws)
    {
        m_curUser -= reqSz;
        m_curUser &= 0xfffffffffffffff0;
        if (m_curUser < 0x30000)
        {
            LogModule.report(logvisor::Error, "user area low mark reached");
            return -1;
        }
        static_cast<PartWriteStream&>(ws).seek(m_curUser);
        return m_curUser;
    }

    uint32_t packOffset(uint64_t offset) const
    {
        return offset;
    }

    std::unique_ptr<IPartWriteStream> beginWriteStream(uint64_t offset)
    {
        bool Err = false;
        std::unique_ptr<IPartWriteStream> ret = std::make_unique<PartWriteStream>(*this, offset, Err);
        if (Err)
            return {};
        return ret;
    }

    bool _build(const std::function<bool(IPartWriteStream&, size_t&)>& func)
    {
        std::unique_ptr<IPartWriteStream> ws = beginWriteStream(0);
        if (!ws)
            return false;
        Header header(m_gameID, m_gameTitle.c_str(), false);
        header.write(*ws);

        ws = beginWriteStream(0x2440);
        size_t xferSz = 0;
        if (!func(*ws, xferSz))
            return false;

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
        {
            LogModule.report(logvisor::Error,
                             "FST flows into user area (one or the other is too big)");
            return false;
        }

        ws = beginWriteStream(0x420);
        if (!ws)
            return false;
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

    bool buildFromDirectory(const SystemChar* dirIn, const SystemChar* dolIn, const SystemChar* apploaderIn)
    {
        std::unique_ptr<IPartWriteStream> ws = beginWriteStream(0);
        if (!ws)
            return false;
        bool result = DiscBuilderBase::PartitionBuilderBase::buildFromDirectory(*ws, dirIn, dolIn, apploaderIn);
        if (!result)
            return false;

        return _build([this, apploaderIn](IPartWriteStream& ws, size_t& xferSz) -> bool
        {
            std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(apploaderIn)->beginReadStream();
            if (!rs)
                return false;
            char buf[8192];
            SystemString apploaderName(apploaderIn);
            ++m_parent.m_progressIdx;
            while (true)
            {
                size_t rdSz = rs->read(buf, 8192);
                if (!rdSz)
                    break;
                ws.write(buf, rdSz);
                xferSz += rdSz;
                if (0x2440 + xferSz >= m_curUser)
                {
                    LogModule.report(logvisor::Error,
                                     "apploader flows into user area (one or the other is too big)");
                    return false;
                }
                m_parent.m_progressCB(m_parent.m_progressIdx, apploaderName, xferSz);
            }
            return true;
        });
    }

    bool mergeFromDirectory(const PartitionGCN* partIn, const SystemChar* dirIn)
    {
        std::unique_ptr<IPartWriteStream> ws = beginWriteStream(0);
        if (!ws)
            return false;
        bool result = DiscBuilderBase::PartitionBuilderBase::mergeFromDirectory(*ws, partIn, dirIn);
        if (!result)
            return false;

        return _build([this, partIn](IPartWriteStream& ws, size_t& xferSz) -> bool
        {
            std::unique_ptr<uint8_t[]> apploaderBuf = partIn->getApploaderBuf();
            size_t apploaderSz = partIn->getApploaderSize();
            SystemString apploaderName(_S("<apploader>"));
            ++m_parent.m_progressIdx;
            ws.write(apploaderBuf.get(), apploaderSz);
            xferSz += apploaderSz;
            if (0x2440 + xferSz >= m_curUser)
            {
                LogModule.report(logvisor::Error,
                                 "apploader flows into user area (one or the other is too big)");
                return false;
            }
            m_parent.m_progressCB(m_parent.m_progressIdx, apploaderName, xferSz);
            return true;
        });
    }
};

bool DiscBuilderGCN::buildFromDirectory(const SystemChar* dirIn, const SystemChar* dolIn,
                                        const SystemChar* apploaderIn)
{
    if (!m_fileIO->beginWriteStream())
        return false;
    if (!CheckFreeSpace(m_outPath.c_str(), 0x57058000))
    {
        LogModule.report(logvisor::Error, _S("not enough free disk space for %s"), m_outPath.c_str());
        return false;
    }
    ++m_progressIdx;
    m_progressCB(m_progressIdx, _S("Preallocating image"), -1);
    auto ws = m_fileIO->beginWriteStream(0x57058000 - 1);
    if (!ws)
        return false;
    ws->write("", 1);

    PartitionBuilderGCN& pb = static_cast<PartitionBuilderGCN&>(*m_partitions[0]);
    return pb.buildFromDirectory(dirIn, dolIn, apploaderIn);
}

uint64_t DiscBuilderGCN::CalculateTotalSizeRequired(const SystemChar* dirIn, const SystemChar* dolIn)
{
    uint64_t sz = DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeBuild(dolIn, dirIn);
    if (sz == -1)
        return -1;
    sz += 0x30000;
    if (sz > 0x57058000)
    {
        LogModule.report(logvisor::Error, _S("disc capacity exceeded [%" PRIu64 " / %" PRIu64 "]"), sz, 0x57058000);
        return -1;
    }
    return sz;
}

DiscBuilderGCN::DiscBuilderGCN(const SystemChar* outPath, const char gameID[6], const char* gameTitle,
                               uint32_t fstMemoryAddr, FProgress progressCB)
: DiscBuilderBase(outPath, 0x57058000, progressCB)
{
    PartitionBuilderGCN* partBuilder = new PartitionBuilderGCN(*this, PartitionBuilderBase::Kind::Data,
                                                               gameID, gameTitle, fstMemoryAddr);
    m_partitions.emplace_back(partBuilder);
}

DiscMergerGCN::DiscMergerGCN(const SystemChar* outPath, DiscGCN& sourceDisc, FProgress progressCB)
: m_sourceDisc(sourceDisc), m_builder(sourceDisc.makeMergeBuilder(outPath, progressCB))
{}

bool DiscMergerGCN::mergeFromDirectory(const SystemChar* dirIn)
{
    if (!m_builder.getFileIO().beginWriteStream())
        return false;
    if (!CheckFreeSpace(m_builder.m_outPath.c_str(), 0x57058000))
    {
        LogModule.report(logvisor::Error, _S("not enough free disk space for %s"), m_builder.m_outPath.c_str());
        return false;
    }
    ++m_builder.m_progressIdx;
    m_builder.m_progressCB(m_builder.m_progressIdx, _S("Preallocating image"), -1);
    auto ws = m_builder.m_fileIO->beginWriteStream(0x57058000 - 1);
    if (!ws)
        return false;
    ws->write("", 1);

    PartitionBuilderGCN& pb = static_cast<PartitionBuilderGCN&>(*m_builder.m_partitions[0]);
    return pb.mergeFromDirectory(static_cast<PartitionGCN*>(m_sourceDisc.getDataPartition()), dirIn);
}

uint64_t DiscMergerGCN::CalculateTotalSizeRequired(DiscGCN& sourceDisc, const SystemChar* dirIn)
{
    uint64_t sz = DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeMerge(
                  sourceDisc.getDataPartition(), dirIn);
    if (sz == -1)
        return -1;
    sz += 0x30000;
    if (sz > 0x57058000)
    {
        LogModule.report(logvisor::Error, _S("disc capacity exceeded [%" PRIu64 " / %" PRIu64 "]"), sz, 0x57058000);
        return -1;
    }
    return sz;
}

}
