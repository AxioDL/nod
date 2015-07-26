#include "NOD/DiscBase.hpp"
#include "NOD/IFileIO.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace NOD
{

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

void DiscBase::IPartition::parseDOL(IPartReadStream& s)
{
    /* Read Dol header */
    s.read(&m_dolHead, sizeof(DOLHeader));

    /* Calculate Dol size */
    uint32_t dolSize = SBig(m_dolHead.textOff[0]) - sizeof(DOLHeader);
    for (uint32_t i = 0 ; i < 7 ; i++)
        dolSize += SBig(m_dolHead.textSizes[i]);
    for (uint32_t i = 0 ; i < 11 ; i++)
        dolSize += SBig(m_dolHead.dataSizes[i]);
    m_dolSz = dolSize;
}

bool DiscBase::IPartition::Node::extractToDirectory(const SystemString& basePath, bool force) const
{
    SystemStringView nameView(getName());
    SystemString path = basePath + _S("/") + nameView.sys_str();
    if (m_kind == NODE_DIRECTORY)
    {
        if (Mkdir(path.c_str(), 0755) && errno != EEXIST)
        {
            LogModule.report(LogVisor::Error, _S("unable to mkdir '%s'"), path.c_str());
            return false;
        }
        for (Node& subnode : *this)
            if (!subnode.extractToDirectory(path, force))
                return false;
    }
    else if (m_kind == NODE_FILE)
    {
        Sstat theStat;
        if (force || Stat(path.c_str(), &theStat))
        {
            std::unique_ptr<IPartReadStream> rs = beginReadStream();
            std::unique_ptr<IFileIO::IWriteStream> ws = NewFileIO(path)->beginWriteStream();
            ws->copyFromDisc(*rs.get(), m_discLength);
        }
    }
    return true;
}

bool DiscBase::IPartition::extractToDirectory(const SystemString& path, bool force)
{
    Sstat theStat;
    if (Mkdir(path.c_str(), 0755) && errno != EEXIST)
    {
        LogModule.report(LogVisor::Error, _S("unable to mkdir '%s'"), path.c_str());
        return false;
    }

    /* Extract Apploader */
    SystemString apploaderPath = path + _S("/apploader.bin");
    if (force || Stat(apploaderPath.c_str(), &theStat))
    {
        std::unique_ptr<uint8_t[]> buf = getApploaderBuf();
        NewFileIO(apploaderPath)->beginWriteStream()->write(buf.get(), m_apploaderSz);
    }

    /* Extract Dol */
    SystemString dolPath = path + _S("/main.dol");
    if (force || Stat(dolPath.c_str(), &theStat))
    {
        std::unique_ptr<uint8_t[]> buf = getDOLBuf();
        NewFileIO(dolPath)->beginWriteStream()->write(buf.get(), m_dolSz);
    }

    /* Extract Filesystem */
    return m_nodes[0].extractToDirectory(path, force);
}

}
