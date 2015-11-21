#include "NOD/DiscBase.hpp"
#include "NOD/IFileIO.hpp"
#include "NOD/NOD.hpp"

#include <errno.h>
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
        if (node.m_kind == Node::Kind::Directory)
        {
            node.m_childrenBegin = it + 1;
            node.m_childrenEnd = m_nodes.begin() + node.m_discLength;
        }
    }
}

void DiscBase::IPartition::parseDOL(IPartReadStream& s)
{
    /* Read Dol header */
    DOLHeader dolHeader;
    s.read(&dolHeader, sizeof(DOLHeader));

    /* Calculate Dol size */
    uint32_t dolSize = SBig(dolHeader.textOff[0]);
    for (uint32_t i = 0 ; i < 7 ; i++)
        dolSize += SBig(dolHeader.textSizes[i]);
    for (uint32_t i = 0 ; i < 11 ; i++)
        dolSize += SBig(dolHeader.dataSizes[i]);

    m_dolSz = dolSize;
}

bool DiscBase::IPartition::Node::extractToDirectory(const SystemString& basePath, const ExtractionContext& ctx) const
{
    SystemStringView nameView(getName());
    SystemString path = basePath + _S("/") + nameView.sys_str();

    if (m_kind == Kind::Directory)
    {
        if (ctx.verbose && ctx.progressCB && !getName().empty())
            ctx.progressCB(getName());
        if (Mkdir(path.c_str(), 0755) && errno != EEXIST)
        {
            LogModule.report(LogVisor::Error, _S("unable to mkdir '%s'"), path.c_str());
            return false;
        }
        for (Node& subnode : *this)
            if (!subnode.extractToDirectory(path, ctx))
                return false;
    }
    else if (m_kind == Kind::File)
    {
        Sstat theStat;
        if (ctx.verbose && ctx.progressCB)
            ctx.progressCB(getName());

        if (ctx.force || Stat(path.c_str(), &theStat))
        {
            std::unique_ptr<IPartReadStream> rs = beginReadStream();
            std::unique_ptr<IFileIO::IWriteStream> ws = NewFileIO(path)->beginWriteStream();
            ws->copyFromDisc(*rs, m_discLength);
        }
    }
    return true;
}

bool DiscBase::IPartition::extractToDirectory(const SystemString& path, const ExtractionContext& ctx)
{
    Sstat theStat;
    if (Mkdir(path.c_str(), 0755) && errno != EEXIST)
    {
        LogModule.report(LogVisor::Error, _S("unable to mkdir '%s'"), path.c_str());
        return false;
    }

    /* Extract Apploader */
    SystemString apploaderPath = path + _S("/apploader.bin");
    if (ctx.force || Stat(apploaderPath.c_str(), &theStat))
    {
        if (ctx.verbose && ctx.progressCB)
            ctx.progressCB("apploader.bin");
        std::unique_ptr<uint8_t[]> buf = getApploaderBuf();
        NewFileIO(apploaderPath)->beginWriteStream()->write(buf.get(), m_apploaderSz);
    }

    /* Extract Dol */
    SystemString dolPath = path + _S("/main.dol");
    if (ctx.force || Stat(dolPath.c_str(), &theStat))
    {
        if (ctx.verbose && ctx.progressCB)
            ctx.progressCB("main.dol");
        std::unique_ptr<uint8_t[]> buf = getDOLBuf();
        NewFileIO(dolPath)->beginWriteStream()->write(buf.get(), m_dolSz);
    }

    /* Extract Filesystem */
    return m_nodes[0].extractToDirectory(path, ctx);
}

}
