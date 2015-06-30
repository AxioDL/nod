#include <sys/stat.h>
#include "DiscBase.hpp"
#include "IFileIO.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

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
        for (Node& subnode : *this)
            subnode.extractToDirectory(path, force);
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

bool DiscBase::IPartition::_recursivePathOfNode(const std::string& basePath,
                                                const Node& refNode,
                                                const Node& curNode,
                                                std::string& result)
{
    std::string path = basePath + "/" + curNode.getName();
    if (&refNode == &curNode)
    {
        result = path;
        return true;
    }
    else if (curNode.m_kind == Node::NODE_DIRECTORY)
    {
        for (const Node& subnode : curNode)
            if (_recursivePathOfNode(path, refNode, subnode, result))
                break;
    }
    return false;
}

std::string DiscBase::IPartition::pathOfNode(const Node& node)
{
    std::string result;
    _recursivePathOfNode("", node, m_nodes[0], result);
    return result;
}

void DiscBase::IPartition::extractToDirectory(const std::string& path, bool force)
{
    struct stat theStat;
    if (mkdir(path.c_str(), 0755) && errno != EEXIST)
        throw std::runtime_error("unable to mkdir '" + path + "'");

    /* Extract Apploader */
    std::string apploaderPath = path + "/apploader.bin";
    if (force || stat(apploaderPath.c_str(), &theStat))
    {
        std::unique_ptr<uint8_t[]> buf(new uint8_t[m_apploaderSz]);
        std::unique_ptr<IPartReadStream> rs = beginReadStream(0x2440);
        rs->read(buf.get(), m_apploaderSz);
        std::unique_ptr<IFileIO::IWriteStream> ws = NewFileIO(path + "/apploader.bin")->beginWriteStream();
        ws->write(buf.get(), m_apploaderSz);
    }

    /* Extract Filesystem */
    m_nodes[0].extractToDirectory(path, force);
}

}
