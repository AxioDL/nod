#include "NOD/DiscBase.hpp"
#include "NOD/IFileIO.hpp"
#include "NOD/DirectoryEnumerator.hpp"
#include "NOD/NOD.hpp"

#include <stdio.h>
#include <errno.h>

#ifndef _WIN32
#include <unistd.h>
#else
static void* memmem(const void *haystack, size_t hlen, const void *needle, size_t nlen)
{
    int needle_first;
    const uint8_t *p = static_cast<const uint8_t*>(haystack);
    size_t plen = hlen;

    if (!nlen)
        return NULL;

    needle_first = *(unsigned char *)needle;

    while (plen >= nlen && (p = static_cast<const uint8_t*>(memchr(p, needle_first, plen - nlen + 1))))
    {
        if (!memcmp(p, needle, nlen))
            return (void *)p;

        p++;
        plen = hlen - (p - static_cast<const uint8_t*>(haystack));
    }

    return NULL;
}
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
    SystemString fsPath = path + _S("/fsroot");
    if (Mkdir(fsPath.c_str(), 0755) && errno != EEXIST)
    {
        LogModule.report(LogVisor::Error, _S("unable to mkdir '%s'"), fsPath.c_str());
        return false;
    }

    return m_nodes[0].extractToDirectory(fsPath, ctx);
}

static uint64_t GetInode(const SystemChar* path)
{
    uint64_t inode;
#if _WIN32
    HANDLE fp = CreateFileW(path,
                            GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (!fp)
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

static bool IsSystemFile(const SystemString& name, bool& isDol)
{
    isDol = false;
    if (name.size() < 4)
        return false;

    if (!StrCaseCmp((&*(name.cend() - 4)), _S(".dol")))
    {
        isDol = true;
        return true;
    }
    if (!StrCaseCmp((&*(name.cend() - 4)), _S(".rel")))
        return true;
    if (!StrCaseCmp((&*(name.cend() - 4)), _S(".rso")))
        return true;
    if (!StrCaseCmp((&*(name.cend() - 4)), _S(".sel")))
        return true;
    if (!StrCaseCmp((&*(name.cend() - 4)), _S(".bnr")))
        return true;
    if (!StrCaseCmp((&*(name.cend() - 4)), _S(".elf")))
        return true;
    if (!StrCaseCmp((&*(name.cend() - 4)), _S(".wad")))
        return true;

    return false;
}

/** Patches out pesky #001 integrity check performed by game's OSInit.
 *  This is required for multi-DOL games, but doesn't harm functionality otherwise */
static size_t PatchDOL(IFileIO::IReadStream& in, IPartWriteStream& out, size_t sz, bool& patched)
{
    patched = false;
    std::unique_ptr<uint8_t[]> buf(new uint8_t[sz]);
    sz = in.read(buf.get(), sz);
    uint8_t* found = static_cast<uint8_t*>(memmem(buf.get(), sz,
                            "\x3C\x03\xF8\x00\x28\x00\x00\x00\x40\x82\x00\x0C"
                            "\x38\x60\x00\x01\x48\x00\x02\x44\x38\x61\x00\x18\x48", 25));
    if (found)
    {
        found[11] = '\x04';
        patched = true;
    }
    return out.write(buf.get(), sz);
}

void DiscBuilderBase::PartitionBuilderBase::recursiveBuildNodes(IPartWriteStream& ws,
                                                                bool system,
                                                                const SystemChar* dirIn,
                                                                uint64_t dolInode)
{
    DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
    for (const DirectoryEnumerator::Entry& e : dEnum)
    {
        if (e.m_isDir)
        {
            recursiveBuildNodes(ws, system, e.m_path.c_str(), dolInode);
        }
        else
        {
            bool isDol;
            bool isSys = IsSystemFile(e.m_name, isDol);
            if (system ^ isSys)
                continue;

            if (dolInode == GetInode(e.m_path.c_str()))
                continue;

            size_t fileSz = ROUND_UP_32(e.m_fileSz);
            uint64_t fileOff = userAllocate(fileSz, ws);
            m_fileOffsetsSizes[e.m_path] = std::make_pair(fileOff, fileSz);
            std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(e.m_path)->beginReadStream();
            size_t xferSz = 0;
            if (isDol)
            {
                bool patched;
                xferSz = PatchDOL(*rs, ws, e.m_fileSz, patched);
                m_parent.m_progressCB(++m_parent.m_progressIdx, e.m_name + (patched ? _S(" [PATCHED]") : _S("")), xferSz);
            }
            else
            {
                char buf[0x8000];
                ++m_parent.m_progressIdx;
                while (xferSz < e.m_fileSz)
                {
                    size_t rdSz = rs->read(buf, NOD::min(size_t(0x8000ul), e.m_fileSz - xferSz));
                    if (!rdSz)
                        break;
                    ws.write(buf, rdSz);
                    xferSz += rdSz;
                    m_parent.m_progressCB(m_parent.m_progressIdx, e.m_name, xferSz);
                }
            }
            for (size_t i=0 ; i<fileSz-xferSz ; ++i)
                ws.write("\xff", 1);
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
        else
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

bool DiscBuilderBase::PartitionBuilderBase::buildFromDirectory(IPartWriteStream& ws,
                                                               const SystemChar* dirIn,
                                                               const SystemChar* dolIn,
                                                               const SystemChar* apploaderIn)
{
    if (!dirIn || !dolIn || !apploaderIn)
        LogModule.report(LogVisor::FatalError, _S("all arguments must be supplied to buildFromDirectory()"));

    /* Clear file */
    ++m_parent.m_progressIdx;
    m_parent.m_progressCB(m_parent.m_progressIdx, _S("Preparing output image"), -1);

    /* Add root node */
    m_buildNodes.emplace_back(true, m_buildNameOff, 0, 1);
    addBuildName(_S("<root>"));

    /* Write Boot DOL first (first thing seeked to after Apploader) */
    {
        Sstat dolStat;
        if (Stat(dolIn, &dolStat))
            LogModule.report(LogVisor::FatalError, _S("unable to stat %s"), dolIn);
        size_t fileSz = ROUND_UP_32(dolStat.st_size);
        uint64_t fileOff = userAllocate(fileSz, ws);
        m_dolOffset = fileOff;
        m_dolSize = fileSz;
        std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(dolIn)->beginReadStream();
        bool patched;
        size_t xferSz = PatchDOL(*rs, ws, dolStat.st_size, patched);
        m_parent.m_progressCB(++m_parent.m_progressIdx, SystemString(dolIn) + (patched ? _S(" [PATCHED]") : _S("")), xferSz);
        for (size_t i=0 ; i<fileSz-xferSz ; ++i)
            ws.write("\xff", 1);
    }

    /* Gather files in root directory */
    uint64_t dolInode = GetInode(dolIn);
    recursiveBuildNodes(ws, true, dirIn, dolInode);
    recursiveBuildNodes(ws, false, dirIn, dolInode);
    recursiveBuildFST(dirIn, dolInode, [&](){m_buildNodes[0].incrementLength();});

    return true;
}

}
