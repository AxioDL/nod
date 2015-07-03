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

void DiscBase::IPartition::Node::extractToDirectory(const SystemString& basePath, bool force)
{
    SystemStringView nameView(getName());
    SystemString path = basePath + _S("/") + nameView.sys_str();
    if (m_kind == NODE_DIRECTORY)
    {
        if (Mkdir(path.c_str(), 0755) && errno != EEXIST)
        {
            SystemUTF8View pathView(path);
            throw std::runtime_error("unable to mkdir '" + pathView.utf8_str() + "'");
        }
        for (Node& subnode : *this)
            subnode.extractToDirectory(path, force);
    }
    else if (m_kind == NODE_FILE)
    {
        Sstat theStat;
        if (force || Stat(path.c_str(), &theStat))
        {
            m_hddFile = NewFileIO(path);
            std::unique_ptr<IPartReadStream> rs = beginReadStream();
            std::unique_ptr<IFileIO::IWriteStream> ws = m_hddFile->beginWriteStream();
            ws->copyFromDisc(*rs.get(), m_discLength);
        }
    }
}

void DiscBase::IPartition::extractToDirectory(const SystemString& path, bool force)
{
    Sstat theStat;
    if (Mkdir(path.c_str(), 0755) && errno != EEXIST)
    {
        SystemUTF8View pathView(path);
        throw std::runtime_error("unable to mkdir '" + pathView.utf8_str() + "'");
    }

    /* Extract Apploader */
    SystemString apploaderPath = path + _S("/apploader.bin");
    if (force || Stat(apploaderPath.c_str(), &theStat))
    {
        std::unique_ptr<uint8_t[]> buf(new uint8_t[m_apploaderSz]);
        std::unique_ptr<IPartReadStream> rs = beginReadStream(0x2440);
        rs->read(buf.get(), m_apploaderSz);
        std::unique_ptr<IFileIO::IWriteStream> ws = NewFileIO(apploaderPath)->beginWriteStream();
        ws->write(buf.get(), m_apploaderSz);
    }

    /* Extract Dol */
    SystemString dolPath = path + _S("/main.dol");
    if (force || Stat(dolPath.c_str(), &theStat))
    {
        std::unique_ptr<IPartReadStream> rs = beginReadStream(m_dolOff);
        /* Read Dol header */
        DOLHeader hdr;
        rs->read(&hdr, sizeof(DOLHeader));
        std::unique_ptr<IFileIO::IWriteStream> ws = NewFileIO(dolPath)->beginWriteStream();
        ws->write(&hdr, sizeof(DOLHeader));
        /* Calculate Dol size */
        uint32_t dolSize = SBig(hdr.textOff[0]);
        for (uint32_t i = 0 ; i < 7 ; i++)
            dolSize += SBig(hdr.textSizes[i]);
        for (uint32_t i = 0 ; i < 11 ; i++)
            dolSize += SBig(hdr.dataSizes[i]);
        std::unique_ptr<uint8_t[]> buf(new uint8_t[dolSize]);
        rs->read(buf.get(), dolSize);
        ws->write(buf.get(), dolSize);
    }

    /* Extract Filesystem */
    m_nodes[0].extractToDirectory(path, force);
}

}
