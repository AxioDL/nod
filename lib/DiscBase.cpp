#include "NOD/DiscBase.hpp"
#include "NOD/IFileIO.hpp"
#include "NOD/DirectoryEnumerator.hpp"
#include "NOD/NOD.hpp"

#include <stdio.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <algorithm>

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

bool DiscBase::IPartition::Node::extractToDirectory(const SystemString& basePath,
                                                    const ExtractionContext& ctx) const
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

bool DiscBase::IPartition::extractToDirectory(const SystemString& path,
                                              const ExtractionContext& ctx)
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
    SystemString dolPath = path + _S("/boot.dol");
    if (ctx.force || Stat(dolPath.c_str(), &theStat))
    {
        if (ctx.verbose && ctx.progressCB)
            ctx.progressCB("boot.dol");
        std::unique_ptr<uint8_t[]> buf = getDOLBuf();
        NewFileIO(dolPath)->beginWriteStream()->write(buf.get(), m_dolSz);
    }

    /* Extract Filesystem */
    return m_nodes[0].extractToDirectory(path, ctx);
}

static uint64_t GetInode(const SystemChar* path)
{
    uint64_t inode;
#if _WIN32
    OFSTRUCT ofs;
    HFILE fp = OpenFile(path, &ofs, OF_READ);
    if (fp == HFILE_ERROR)
        LogModule.report(LogVisor::FatalError, _S("unable to open %s"), path);
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(fp, &info))
        LogModule.report(LogVisor::FatalError, _S("unable to GetFileInformationByHandle %s"), path);
    inode = uint64_t(info.nFileIndexHigh) << 32;
    inode |= uint64_t(info.nFileIndexLow);
    CloseHandle(fp);
#else
    struct stat st;
    if (stat(path, &st))
        LogModule.report(LogVisor::FatalError, _S("unable to stat %s"), path);
    inode = uint64_t(st.st_ino);
#endif
    return inode;
}

static bool IsSystemFile(const SystemString& name)
{
    if (name.size() < 4)
        return false;

    if (!StrCaseCmp((&*name.cend()) - 4, _S(".dol")))
        return true;
    if (!StrCaseCmp((&*name.cend()) - 4, _S(".rel")))
        return true;
    if (!StrCaseCmp((&*name.cend()) - 4, _S(".rso")))
        return true;
    if (!StrCaseCmp((&*name.cend()) - 4, _S(".sel")))
        return true;
    if (!StrCaseCmp((&*name.cend()) - 4, _S(".bnr")))
        return true;
    if (!StrCaseCmp((&*name.cend()) - 4, _S(".elf")))
        return true;
    if (!StrCaseCmp((&*name.cend()) - 4, _S(".wad")))
        return true;

    return false;
}

void DiscBuilderBase::PartitionBuilderBase::recursiveBuildNodes(bool system, const SystemChar* dirIn,
                                                                uint64_t dolInode)
{
    DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
    for (const DirectoryEnumerator::Entry& e : dEnum)
    {
        if (e.m_isDir)
        {
            size_t dirNodeIdx = m_buildNodes.size();
            recursiveBuildNodes(system, e.m_path.c_str(), dolInode);
        }
        else
        {
            bool isSys = IsSystemFile(e.m_name);
            if (system ^ isSys)
                continue;

            if (dolInode == GetInode(e.m_path.c_str()))
                continue;

            size_t fileSz = ROUND_UP_32(e.m_fileSz);
            uint64_t fileOff = userAllocate(fileSz);
            m_fileOffsetsSizes[e.m_path] = std::make_pair(fileOff, fileSz);
            std::unique_ptr<IFileIO::IWriteStream> ws = m_parent.getFileIO().beginWriteStream(fileOff);
            FILE* fp = Fopen(e.m_path.c_str(), _S("rb"), FileLockType::Read);
            if (!fp)
                LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for reading"), e.m_path.c_str());
            char buf[0x8000];
            size_t xferSz = 0;
            ++m_parent.m_progressIdx;
            while (xferSz < e.m_fileSz)
            {
                size_t rdSz = fread(buf, 1, std::min(0x8000ul, e.m_fileSz - xferSz), fp);
                if (!rdSz)
                    break;
                ws->write(buf, rdSz);
                xferSz += rdSz;
                m_parent.m_progressCB(m_parent.m_progressIdx, e.m_name, xferSz);
            }
            fclose(fp);
            for (size_t i=0 ; i<fileSz-xferSz ; ++i)
                ws->write("\xff", 1);
        }
    }
}

void DiscBuilderBase::PartitionBuilderBase::recursiveBuildFST(const SystemChar* dirIn, uint64_t dolInode,
                                                              std::function<void(void)> incParents)
{
    DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
    for (const DirectoryEnumerator::Entry& e : dEnum)
    {
        if (e.m_isDir)
        {
            size_t dirNodeIdx = m_buildNodes.size();
            m_buildNodes.emplace_back(true, m_buildNameOff, 0, dirNodeIdx+1);
            addBuildName(e.m_name);
            incParents();
            recursiveBuildFST(e.m_path.c_str(), dolInode, [&](){m_buildNodes[dirNodeIdx].incrementLength(); incParents();});
        }
        {
            if (dolInode == GetInode(e.m_path.c_str()))
            {
                m_buildNodes.emplace_back(false, m_buildNameOff, packOffset(m_dolOffset), m_dolSize);
                addBuildName(e.m_name);
                incParents();
                continue;
            }

            std::pair<uint64_t,uint64_t> fileOffSz = m_fileOffsetsSizes.at(e.m_path);
            m_buildNodes.emplace_back(false, m_buildNameOff, packOffset(fileOffSz.first), fileOffSz.second);
            addBuildName(e.m_name);
            incParents();
        }
    }
}

bool DiscBuilderBase::PartitionBuilderBase::buildFromDirectory(const SystemChar* dirIn,
                                                            const SystemChar* dolIn,
                                                            const SystemChar* apploaderIn)
{
    if (!dirIn || !dolIn || !apploaderIn)
        LogModule.report(LogVisor::FatalError, "all arguments must be supplied to buildFromDirectory()");

    /* Clear file */
    m_parent.getFileIO().beginWriteStream();
    ++m_parent.m_progressIdx;
    m_parent.m_progressCB(m_parent.m_progressIdx, "Preparing output image", -1);

    /* Add root node */
    m_buildNodes.emplace_back(true, m_buildNameOff, 0, 1);
    addBuildName(_S("<root>"));

    /* Write DOL first (ensures that it's within a 32-bit offset for Wii apploaders) */
    {
        Sstat dolStat;
        if (Stat(dolIn, &dolStat))
            LogModule.report(LogVisor::FatalError, _S("unable to stat %s"), dolIn);
        size_t fileSz = ROUND_UP_32(dolStat.st_size);
        uint64_t fileOff = userAllocate(fileSz);
        m_dolOffset = fileOff;
        m_dolSize = fileSz;
        std::unique_ptr<IFileIO::IWriteStream> ws = m_parent.getFileIO().beginWriteStream(fileOff);
        FILE* fp = Fopen(dolIn, _S("rb"), FileLockType::Read);
        if (!fp)
            LogModule.report(LogVisor::FatalError, _S("unable to open '%s' for reading"), dolIn);
        char buf[8192];
        size_t xferSz = 0;
        SystemString dolName(dolIn);
        ++m_parent.m_progressIdx;
        while (xferSz < dolStat.st_size)
        {
            size_t rdSz = fread(buf, 1, std::min(8192ul, dolStat.st_size - xferSz), fp);
            if (!rdSz)
                break;
            ws->write(buf, rdSz);
            xferSz += rdSz;
            m_parent.m_progressCB(m_parent.m_progressIdx, dolName, xferSz);
        }
        fclose(fp);
        for (size_t i=0 ; i<fileSz-xferSz ; ++i)
            ws->write("\xff", 1);
    }

    /* Gather files in root directory */
    uint64_t dolInode = GetInode(dolIn);
    recursiveBuildNodes(true, dirIn, dolInode);
    recursiveBuildNodes(false, dirIn, dolInode);
    recursiveBuildFST(dirIn, dolInode, [&](){m_buildNodes[0].incrementLength();});

    return true;
}

}
