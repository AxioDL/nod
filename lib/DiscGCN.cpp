#include "DiscGCN.hpp"

namespace NOD
{

class PartitionGCN : public DiscBase::IPartition
{
public:
    PartitionGCN(const DiscGCN& parent, Kind kind, size_t offset)
    : IPartition(parent, kind, offset)
    {
        /* GCN-specific header reads */
        std::unique_ptr<IPartReadStream> s = beginReadStream(0x420);
        uint32_t vals[3];
        s->read(vals, 12);
        m_dolOff = SBig(vals[0]);
        m_fstOff = SBig(vals[1]);
        m_fstSz = SBig(vals[2]);

        /* Yay files!! */
        parseFST(*s.get());
    }

    class PartReadStream : public IPartReadStream
    {
        const PartitionGCN& m_parent;
        std::unique_ptr<IDiscIO::IReadStream> m_dio;

    public:
        PartReadStream(const PartitionGCN& parent, size_t offset)
        : m_parent(parent)
        {m_dio = m_parent.m_parent.getDiscIO().beginReadStream(offset);}
        void seek(size_t offset, int whence)
        {m_dio->seek(offset, whence);}
        size_t read(void* buf, size_t length)
        {return m_dio->read(buf, length);}
    };

    std::unique_ptr<IPartReadStream> beginReadStream(size_t offset) const
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
