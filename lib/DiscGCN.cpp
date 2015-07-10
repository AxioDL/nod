#include "NOD/DiscGCN.hpp"

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
        parseFST(*s.get());

        /* Also make DOL header and size handy */
        s->seek(m_dolOff);
        parseDOL(*s.get());
    }

    class PartReadStream : public IPartReadStream
    {
        const PartitionGCN& m_parent;
        std::unique_ptr<IDiscIO::IReadStream> m_dio;

    public:
        PartReadStream(const PartitionGCN& parent, uint64_t offset)
        : m_parent(parent)
        {m_dio = m_parent.m_parent.getDiscIO().beginReadStream(offset);}
        void seek(int64_t offset, int whence)
        {m_dio->seek(offset, whence);}
        uint64_t position() const {return m_dio->position();}
        uint64_t read(void* buf, uint64_t length)
        {return m_dio->read(buf, length);}
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
    m_partitions.emplace_back(new PartitionGCN(*this, IPartition::PART_DATA, 0));
}

bool DiscGCN::commit()
{
    return false;
}

}
