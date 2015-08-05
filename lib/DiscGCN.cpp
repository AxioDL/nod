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
    m_partitions.emplace_back(new PartitionGCN(*this, IPartition::PART_DATA, 0));
}

bool DiscGCN::commit()
{
    return false;
}

}
