#include <sys/stat.h>
#include "DiscBase.hpp"
#include "IFileIO.hpp"

namespace NOD
{

DiscBase::DiscBase(std::unique_ptr<IDiscIO>&& dio)
: m_discIO(std::move(dio)), m_header(*m_discIO.get())
{
}

void DiscBase::IPartition::parseFST(IPartReadStream& s)
{
    std::unique_ptr<uint8_t[]> fst(new uint8_t[m_fstSz]);
    s.seek(m_fstOff);
    s.read(fst.get(), m_fstSz);

    const FSTNode* nodes = (FSTNode*)fst.get();

    /* Root node indicates the count of all contained nodes */
    uint32_t nodeCount = nodes[0].getLength();
    const char* names = (char*)fst.get() + 12 * nodeCount;
    m_nodes.clear();
    m_nodes.reserve(nodeCount);

    /* Construct nodes */
    for (uint32_t n=0 ; n<nodeCount ; ++n)
    {
        const FSTNode& node = nodes[n];
        m_nodes.emplace_back(*this, node, n ? names + node.getNameOffset() : "");
    }

    /* Setup dir-child iterators */
    for (std::vector<Node>::iterator it=m_nodes.begin();
         it != m_nodes.end();
         ++it)
    {
        Node& node = *it;
        if (node.m_kind == Node::NODE_DIRECTORY)
        {
            node.m_childrenBegin = it + 1;
            node.m_childrenEnd = m_nodes.begin() + node.m_discLength;
        }
    }
}

void DiscBase::IPartition::Node::extractToDirectory(const std::string& basePath, bool force)
{
    std::string path = basePath + "/" + getName();
    if (m_kind == NODE_DIRECTORY)
    {
        if (mkdir(path.c_str(), 0755) && errno != EEXIST)
            throw std::runtime_error("unable to mkdir '" + path + "'");
        for (DiscBase::IPartition::Node& subnode : *this)
            subnode.extractToDirectory(path);
    }
    else if (m_kind == NODE_FILE)
    {
        struct stat theStat;
        if (force || stat(path.c_str(), &theStat))
        {
            m_hddFile = NewFileIO(path);
            std::unique_ptr<IPartReadStream> rs = beginReadStream();
            std::unique_ptr<IFileIO::IWriteStream> ws = m_hddFile->beginWriteStream();
            ws->copyFromDisc(*rs.get(), m_discLength);
        }
    }
}

}
