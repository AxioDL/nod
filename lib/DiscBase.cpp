#include "nod/DiscBase.hpp"
#include "nod/IFileIO.hpp"
#include "nod/DirectoryEnumerator.hpp"
#include "nod/nod.hpp"

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

namespace nod
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
        ++m_parent.m_curNodeIdx;
        if (ctx.verbose && ctx.progressCB && !getName().empty())
            ctx.progressCB(getName(), m_parent.m_curNodeIdx / float(m_parent.getNodeCount()));
        if (Mkdir(path.c_str(), 0755) && errno != EEXIST)
        {
            LogModule.report(logvisor::Error, _S("unable to mkdir '%s'"), path.c_str());
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
            ctx.progressCB(getName(), m_parent.m_curNodeIdx / float(m_parent.getNodeCount()));

        if (ctx.force || Stat(path.c_str(), &theStat))
        {
            std::unique_ptr<IPartReadStream> rs = beginReadStream();
            std::unique_ptr<IFileIO::IWriteStream> ws = NewFileIO(path)->beginWriteStream();
            if (!rs || !ws)
                return false;
            ws->copyFromDisc(*rs, m_discLength,
            [&](float prog)
            {
                if (ctx.verbose && ctx.progressCB)
                    ctx.progressCB(getName(), (m_parent.m_curNodeIdx + prog) / float(m_parent.getNodeCount()));
            });
        }
        ++m_parent.m_curNodeIdx;
    }
    return true;
}

bool DiscBase::IPartition::extractToDirectory(const SystemString& path,
                                              const ExtractionContext& ctx)
{
    m_curNodeIdx = 0;
    Sstat theStat;
    if (Mkdir(path.c_str(), 0755) && errno != EEXIST)
    {
        LogModule.report(logvisor::Error, _S("unable to mkdir '%s'"), path.c_str());
        return false;
    }

    if (Mkdir((path + _S("/sys")).c_str(), 0755) && errno != EEXIST)
    {
        LogModule.report(logvisor::Error, _S("unable to mkdir '%s/sys'"), path.c_str());
        return false;
    }

    /* Extract Apploader */
    SystemString apploaderPath = path + _S("/sys/apploader.img");
    if (ctx.force || Stat(apploaderPath.c_str(), &theStat))
    {
        if (ctx.verbose && ctx.progressCB)
            ctx.progressCB("apploader.bin", 0.f);
        std::unique_ptr<uint8_t[]> buf = getApploaderBuf();
        auto ws = NewFileIO(apploaderPath)->beginWriteStream();
        if (!ws)
            return false;
        ws->write(buf.get(), m_apploaderSz);
    }

    /* Extract Dol */
    SystemString dolPath = path + _S("/sys/main.dol");
    if (ctx.force || Stat(dolPath.c_str(), &theStat))
    {
        if (ctx.verbose && ctx.progressCB)
            ctx.progressCB("main.dol", 0.f);
        std::unique_ptr<uint8_t[]> buf = getDOLBuf();
        auto ws = NewFileIO(dolPath)->beginWriteStream();
        if (!ws)
            return false;
        ws->write(buf.get(), m_dolSz);
    }

    /* Extract Boot info */
    SystemString bootPath = path + _S("/sys/boot.bin");
    if (ctx.force || Stat(bootPath.c_str(), &theStat))
    {
        if (ctx.verbose && ctx.progressCB)
            ctx.progressCB("boot.bin", 0.f);
        auto ws = NewFileIO(bootPath)->beginWriteStream();
        if (!ws)
            return false;
        getHeader().write(*ws.get());
    }

    /* Extract BI2 info */
    SystemString bi2Path = path + _S("/sys/bi2.bin");
    if (ctx.force || Stat(bi2Path.c_str(), &theStat))
    {
        if (ctx.verbose && ctx.progressCB)
            ctx.progressCB("bi2.bin", 0.f);

        const uint8_t* buf = getBI2Buf();
        auto ws = NewFileIO(bi2Path)->beginWriteStream();
        if (!ws)
            return false;
        ws->write(buf, sizeof(BI2Header));
    }
    /* Extract Filesystem */
    SystemString fsPath = path + _S("/files");
    if (Mkdir(fsPath.c_str(), 0755) && errno != EEXIST)
    {
        LogModule.report(logvisor::Error, _S("unable to mkdir '%s'"), fsPath.c_str());
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
    {
        LogModule.report(logvisor::Error, _S("unable to open %s"), path);
        return 0;
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(fp, &info))
    {
        LogModule.report(logvisor::Error, _S("unable to GetFileInformationByHandle %s"), path);
        return 0;
    }
    inode = uint64_t(info.nFileIndexHigh) << 32;
    inode |= uint64_t(info.nFileIndexLow);
    CloseHandle(fp);
#else
    struct stat st;
    if (stat(path, &st))
    {
        LogModule.report(logvisor::Error, _S("unable to stat %s"), path);
        return 0;
    }
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
static void PatchDOL(std::unique_ptr<uint8_t[]>& buf, size_t sz, bool& patched)
{
    patched = false;
    uint8_t* found = static_cast<uint8_t*>(memmem(buf.get(), sz,
                            "\x3C\x03\xF8\x00\x28\x00\x00\x00\x40\x82\x00\x0C"
                            "\x38\x60\x00\x01\x48\x00\x02\x44\x38\x61\x00\x18\x48", 25));
    if (found)
    {
        found[11] = '\x04';
        patched = true;
    }
}

static size_t PatchDOL(IFileIO::IReadStream& in, IPartWriteStream& out, size_t sz, bool& patched)
{
    std::unique_ptr<uint8_t[]> buf(new uint8_t[sz]);
    sz = in.read(buf.get(), sz);
    PatchDOL(buf, sz, patched);
    return out.write(buf.get(), sz);
}

void DiscBuilderBase::PartitionBuilderBase::recursiveBuildNodesPre(const SystemChar* dirIn,
                                                                   uint64_t dolInode)
{
    DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
    for (const DirectoryEnumerator::Entry& e : dEnum)
    {
        if (e.m_isDir)
        {
            recursiveBuildNodesPre(e.m_path.c_str(), dolInode);
        }
        else
        {
            if (dolInode == GetInode(e.m_path.c_str()))
                continue;

            ++m_parent.m_progressTotal;
        }
    }
}

bool DiscBuilderBase::PartitionBuilderBase::recursiveBuildNodes(IPartWriteStream& ws,
                                                                bool system,
                                                                const SystemChar* dirIn,
                                                                uint64_t dolInode)
{
    DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
    for (const DirectoryEnumerator::Entry& e : dEnum)
    {
        if (e.m_isDir)
        {
            if (!recursiveBuildNodes(ws, system, e.m_path.c_str(), dolInode))
                return false;
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
            if (fileOff == -1)
                return false;
            m_fileOffsetsSizes[e.m_path] = std::make_pair(fileOff, fileSz);
            std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(e.m_path)->beginReadStream();
            if (!rs)
                return false;
            size_t xferSz = 0;
            if (isDol)
            {
                bool patched;
                xferSz = PatchDOL(*rs, ws, e.m_fileSz, patched);
                m_parent.m_progressCB(m_parent.getProgressFactor(), e.m_name + (patched ? _S(" [PATCHED]") : _S("")), xferSz);
                ++m_parent.m_progressIdx;
            }
            else
            {
                char buf[0x8000];
                while (xferSz < e.m_fileSz)
                {
                    size_t rdSz = rs->read(buf, nod::min(size_t(0x8000ul), e.m_fileSz - xferSz));
                    if (!rdSz)
                        break;
                    ws.write(buf, rdSz);
                    xferSz += rdSz;
                    m_parent.m_progressCB(m_parent.getProgressFactorMidFile(xferSz, e.m_fileSz), e.m_name, xferSz);
                }
                ++m_parent.m_progressIdx;
            }
            for (size_t i=0 ; i<fileSz-xferSz ; ++i)
                ws.write("\xff", 1);
        }
    }

    return true;
}

bool DiscBuilderBase::PartitionBuilderBase::recursiveBuildFST(const SystemChar* dirIn, uint64_t dolInode,
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
            if (!recursiveBuildFST(e.m_path.c_str(), dolInode, [&](){m_buildNodes[dirNodeIdx].incrementLength(); incParents();}))
                return false;
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

    return true;
}

void DiscBuilderBase::PartitionBuilderBase::recursiveMergeNodesPre(const DiscBase::IPartition::Node* nodeIn,
                                                                   const SystemChar* dirIn)
{
    /* Build map of existing nodes to write-through later */
    std::unordered_map<std::string, const Partition::Node*> fileNodes;
    std::unordered_map<std::string, const Partition::Node*> dirNodes;
    if (nodeIn)
    {
        fileNodes.reserve(nodeIn->size());
        dirNodes.reserve(nodeIn->size());
        for (const Partition::Node& ch : *nodeIn)
        {
            if (ch.getKind() == Partition::Node::Kind::File)
                fileNodes.insert(std::make_pair(ch.getName(), &ch));
            else if (ch.getKind() == Partition::Node::Kind::Directory)
                dirNodes.insert(std::make_pair(ch.getName(), &ch));
        }
    }

    /* Merge this directory's files */
    if (dirIn)
    {
        DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
        for (const DirectoryEnumerator::Entry& e : dEnum)
        {
            SystemUTF8View nameView(e.m_name);

            if (e.m_isDir)
            {
                auto search = dirNodes.find(nameView.utf8_str());
                if (search != dirNodes.cend())
                {
                    recursiveMergeNodesPre(search->second, e.m_path.c_str());
                    dirNodes.erase(search);
                }
                else
                {
                    recursiveMergeNodesPre(nullptr, e.m_path.c_str());
                }
            }
            else
            {
                fileNodes.erase(nameView.utf8_str());
                ++m_parent.m_progressTotal;
            }
        }
    }

    /* Write-through remaining dir nodes */
    for (const auto& p : dirNodes)
    {
        recursiveMergeNodesPre(p.second, nullptr);
    }

    /* Write-through remaining file nodes */
    m_parent.m_progressTotal += fileNodes.size();
}

bool DiscBuilderBase::PartitionBuilderBase::recursiveMergeNodes(IPartWriteStream& ws,
                                                                bool system,
                                                                const DiscBase::IPartition::Node* nodeIn,
                                                                const SystemChar* dirIn,
                                                                const SystemString& keyPath)
{
    /* Build map of existing nodes to write-through later */
    std::unordered_map<std::string, const Partition::Node*> fileNodes;
    std::unordered_map<std::string, const Partition::Node*> dirNodes;
    if (nodeIn)
    {
        fileNodes.reserve(nodeIn->size());
        dirNodes.reserve(nodeIn->size());
        for (const Partition::Node& ch : *nodeIn)
        {
            if (ch.getKind() == Partition::Node::Kind::File)
                fileNodes.insert(std::make_pair(ch.getName(), &ch));
            else if (ch.getKind() == Partition::Node::Kind::Directory)
                dirNodes.insert(std::make_pair(ch.getName(), &ch));
        }
    }

    /* Merge this directory's files */
    if (dirIn)
    {
        DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
        for (const DirectoryEnumerator::Entry& e : dEnum)
        {
            SystemUTF8View nameView(e.m_name);
            SystemString chKeyPath = keyPath + _S('/') + e.m_name;

            if (e.m_isDir)
            {
                auto search = dirNodes.find(nameView.utf8_str());
                if (search != dirNodes.cend())
                {
                    if (!recursiveMergeNodes(ws, system, search->second, e.m_path.c_str(), chKeyPath))
                        return false;
                    dirNodes.erase(search);
                }
                else
                {
                    if (!recursiveMergeNodes(ws, system, nullptr, e.m_path.c_str(), chKeyPath))
                        return false;
                }
            }
            else
            {
                bool isDol;
                bool isSys = IsSystemFile(e.m_name, isDol);
                if (system ^ isSys)
                    continue;

                fileNodes.erase(nameView.utf8_str());

                size_t fileSz = ROUND_UP_32(e.m_fileSz);
                uint64_t fileOff = userAllocate(fileSz, ws);
                if (fileOff == -1)
                    return false;
                m_fileOffsetsSizes[chKeyPath] = std::make_pair(fileOff, fileSz);
                std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(e.m_path)->beginReadStream();
                if (!rs)
                    return false;
                size_t xferSz = 0;
                if (isDol)
                {
                    bool patched;
                    xferSz = PatchDOL(*rs, ws, e.m_fileSz, patched);
                    m_parent.m_progressCB(m_parent.getProgressFactor(), e.m_name +
                                          (patched ? _S(" [PATCHED]") : _S("")), xferSz);
                    ++m_parent.m_progressIdx;
                }
                else
                {
                    char buf[0x8000];
                    while (xferSz < e.m_fileSz)
                    {
                        size_t rdSz = rs->read(buf, nod::min(size_t(0x8000ul), e.m_fileSz - xferSz));
                        if (!rdSz)
                            break;
                        ws.write(buf, rdSz);
                        xferSz += rdSz;
                        m_parent.m_progressCB(m_parent.getProgressFactorMidFile(xferSz, e.m_fileSz), e.m_name, xferSz);
                    }
                    ++m_parent.m_progressIdx;
                }
                for (size_t i=0 ; i<fileSz-xferSz ; ++i)
                    ws.write("\xff", 1);
            }
        }
    }

    /* Write-through remaining dir nodes */
    for (const auto& p : dirNodes)
    {
        SystemStringView sysName(p.second->getName());
        SystemString chKeyPath = keyPath + _S('/') + sysName.sys_str();
        if (!recursiveMergeNodes(ws, system, p.second, nullptr, chKeyPath))
            return false;
    }

    /* Write-through remaining file nodes */
    for (const auto& p : fileNodes)
    {
        const Partition::Node& ch = *p.second;
        SystemStringView sysName(ch.getName());
        SystemString chKeyPath = keyPath + _S('/') + sysName.sys_str();

        bool isDol;
        bool isSys = IsSystemFile(sysName.sys_str(), isDol);
        if (system ^ isSys)
            continue;

        size_t fileSz = ROUND_UP_32(ch.size());
        uint64_t fileOff = userAllocate(fileSz, ws);
        if (fileOff == -1)
            return false;
        m_fileOffsetsSizes[chKeyPath] = std::make_pair(fileOff, fileSz);
        std::unique_ptr<IPartReadStream> rs = ch.beginReadStream();
        if (!rs)
            return false;
        size_t xferSz = 0;
        if (isDol)
        {
            xferSz = ch.size();
            std::unique_ptr<uint8_t[]> dolBuf = ch.getBuf();
            bool patched;
            PatchDOL(dolBuf, xferSz, patched);
            ws.write(dolBuf.get(), xferSz);
            m_parent.m_progressCB(m_parent.getProgressFactor(), sysName.sys_str() +
                                  (patched ? _S(" [PATCHED]") : _S("")), xferSz);
            ++m_parent.m_progressIdx;
        }
        else
        {
            char buf[0x8000];
            while (xferSz < ch.size())
            {
                size_t rdSz = rs->read(buf, nod::min(size_t(0x8000), size_t(ch.size() - xferSz)));
                if (!rdSz)
                    break;
                ws.write(buf, rdSz);
                xferSz += rdSz;
                m_parent.m_progressCB(m_parent.getProgressFactorMidFile(xferSz, ch.size()), sysName.sys_str(), xferSz);
            }
            ++m_parent.m_progressIdx;
        }
        for (size_t i=0 ; i<fileSz-xferSz ; ++i)
            ws.write("\xff", 1);
    }

    return true;
}

bool DiscBuilderBase::PartitionBuilderBase::recursiveMergeFST(const Partition::Node* nodeIn,
                                                              const SystemChar* dirIn,
                                                              std::function<void(void)> incParents,
                                                              const SystemString& keyPath)
{
    /* Build map of existing nodes to write-through later */
    std::unordered_map<std::string, const Partition::Node*> fileNodes;
    std::unordered_map<std::string, const Partition::Node*> dirNodes;
    if (nodeIn)
    {
        fileNodes.reserve(nodeIn->size());
        dirNodes.reserve(nodeIn->size());
        for (const Partition::Node& ch : *nodeIn)
        {
            if (ch.getKind() == Partition::Node::Kind::File)
                fileNodes.insert(std::make_pair(ch.getName(), &ch));
            else if (ch.getKind() == Partition::Node::Kind::Directory)
                dirNodes.insert(std::make_pair(ch.getName(), &ch));
        }
    }

    /* Merge this directory's files */
    if (dirIn)
    {
        DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
        for (const DirectoryEnumerator::Entry& e : dEnum)
        {
            SystemUTF8View nameView(e.m_name);
            SystemString chKeyPath = keyPath + _S('/') + e.m_name;

            if (e.m_isDir)
            {
                size_t dirNodeIdx = m_buildNodes.size();
                m_buildNodes.emplace_back(true, m_buildNameOff, 0, dirNodeIdx+1);
                addBuildName(e.m_name);
                incParents();

                auto search = dirNodes.find(nameView.utf8_str());
                if (search != dirNodes.cend())
                {
                    if (!recursiveMergeFST(search->second, e.m_path.c_str(),
                                           [&](){m_buildNodes[dirNodeIdx].incrementLength(); incParents();},
                                           chKeyPath))
                        return false;
                    dirNodes.erase(search);
                }
                else
                {
                    if (!recursiveMergeFST(nullptr, e.m_path.c_str(),
                                           [&](){m_buildNodes[dirNodeIdx].incrementLength(); incParents();},
                                           chKeyPath))
                        return false;
                }
            }
            else
            {
                fileNodes.erase(nameView.utf8_str());
                std::pair<uint64_t,uint64_t> fileOffSz = m_fileOffsetsSizes.at(chKeyPath);
                m_buildNodes.emplace_back(false, m_buildNameOff, packOffset(fileOffSz.first), fileOffSz.second);
                addBuildName(e.m_name);
                incParents();
            }
        }
    }


    /* Write-through remaining dir nodes */
    for (const auto& p : dirNodes)
    {
        SystemStringView sysName(p.second->getName());
        SystemString chKeyPath = keyPath + _S('/') + sysName.sys_str();

        size_t dirNodeIdx = m_buildNodes.size();
        m_buildNodes.emplace_back(true, m_buildNameOff, 0, dirNodeIdx+1);
        addBuildName(sysName.sys_str());
        incParents();

        if (!recursiveMergeFST(p.second, nullptr,
                               [&](){m_buildNodes[dirNodeIdx].incrementLength(); incParents();},
                               chKeyPath))
            return false;
    }

    /* Write-through remaining file nodes */
    for (const auto& p : fileNodes)
    {
        const Partition::Node& ch = *p.second;
        SystemStringView sysName(ch.getName());
        SystemString chKeyPath = keyPath + _S('/') + sysName.sys_str();

        std::pair<uint64_t,uint64_t> fileOffSz = m_fileOffsetsSizes.at(chKeyPath);
        m_buildNodes.emplace_back(false, m_buildNameOff, packOffset(fileOffSz.first), fileOffSz.second);
        addBuildName(sysName.sys_str());
        incParents();
    }

    return true;
}

bool DiscBuilderBase::PartitionBuilderBase::RecursiveCalculateTotalSize(
        uint64_t& totalSz,
        const DiscBase::IPartition::Node* nodeIn,
        const SystemChar* dirIn)
{
    /* Build map of existing nodes to write-through later */
    std::unordered_map<std::string, const Partition::Node*> fileNodes;
    std::unordered_map<std::string, const Partition::Node*> dirNodes;
    if (nodeIn)
    {
        fileNodes.reserve(nodeIn->size());
        dirNodes.reserve(nodeIn->size());
        for (const Partition::Node& ch : *nodeIn)
        {
            if (ch.getKind() == Partition::Node::Kind::File)
                fileNodes.insert(std::make_pair(ch.getName(), &ch));
            else if (ch.getKind() == Partition::Node::Kind::Directory)
                dirNodes.insert(std::make_pair(ch.getName(), &ch));
        }
    }

    /* Merge this directory's files */
    if (dirIn)
    {
        DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
        for (const DirectoryEnumerator::Entry& e : dEnum)
        {
            SystemUTF8View nameView(e.m_name);

            if (e.m_isDir)
            {
                auto search = dirNodes.find(nameView.utf8_str());
                if (search != dirNodes.cend())
                {
                    if (!RecursiveCalculateTotalSize(totalSz, search->second, e.m_path.c_str()))
                        return false;
                    dirNodes.erase(search);
                }
                else
                {
                    if (!RecursiveCalculateTotalSize(totalSz, nullptr, e.m_path.c_str()))
                        return false;
                }
            }
            else
            {
                fileNodes.erase(nameView.utf8_str());
                totalSz += ROUND_UP_32(e.m_fileSz);
            }
        }
    }

    /* Write-through remaining dir nodes */
    for (const auto& p : dirNodes)
    {
        if (!RecursiveCalculateTotalSize(totalSz, p.second, nullptr))
            return false;
    }

    /* Write-through remaining file nodes */
    for (const auto& p : fileNodes)
    {
        const Partition::Node& ch = *p.second;
        totalSz += ROUND_UP_32(ch.size());
    }

    return true;
}

bool DiscBuilderBase::PartitionBuilderBase::buildFromDirectory(IPartWriteStream& ws,
                                                               const SystemChar* dirIn,
                                                               const SystemChar* dolIn,
                                                               const SystemChar* apploaderIn)
{
    if (!dirIn || !dolIn || !apploaderIn)
    {
        LogModule.report(logvisor::Error, _S("all arguments must be supplied to buildFromDirectory()"));
        return false;
    }

    /* 1st pass - Tally up total progress steps */
    uint64_t dolInode = GetInode(dolIn);
    m_parent.m_progressTotal += 2; /* Prep and DOL */
    recursiveBuildNodesPre(dirIn, dolInode);

    /* Clear file */
    m_parent.m_progressCB(m_parent.getProgressFactor(), _S("Preparing output image"), -1);
    ++m_parent.m_progressIdx;

    /* Add root node */
    m_buildNodes.emplace_back(true, m_buildNameOff, 0, 1);
    addBuildName(_S("<root>"));

    /* Write Boot DOL first (first thing seeked to after Apploader) */
    {
        Sstat dolStat;
        if (Stat(dolIn, &dolStat))
        {
            LogModule.report(logvisor::Error, _S("unable to stat %s"), dolIn);
            return false;
        }
        size_t fileSz = ROUND_UP_32(dolStat.st_size);
        uint64_t fileOff = userAllocate(fileSz, ws);
        if (fileOff == -1)
            return false;
        m_dolOffset = fileOff;
        m_dolSize = fileSz;
        std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(dolIn)->beginReadStream();
        if (!rs)
            return false;
        bool patched;
        size_t xferSz = PatchDOL(*rs, ws, dolStat.st_size, patched);
        m_parent.m_progressCB(m_parent.getProgressFactor(), SystemString(dolIn) +
                              (patched ? _S(" [PATCHED]") : _S("")), xferSz);
        ++m_parent.m_progressIdx;
        for (size_t i=0 ; i<fileSz-xferSz ; ++i)
            ws.write("\xff", 1);
    }

    /* Gather files in root directory */
    if (!recursiveBuildNodes(ws, true, dirIn, dolInode))
        return false;
    if (!recursiveBuildNodes(ws, false, dirIn, dolInode))
        return false;
    if (!recursiveBuildFST(dirIn, dolInode, [&](){m_buildNodes[0].incrementLength();}))
        return false;

    return true;
}

uint64_t DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeBuild(const SystemChar* dolIn,
                                                                        const SystemChar* dirIn)
{
    Sstat dolStat;
    if (Stat(dolIn, &dolStat))
    {
        LogModule.report(logvisor::Error, _S("unable to stat %s"), dolIn);
        return -1;
    }
    uint64_t totalSz = ROUND_UP_32(dolStat.st_size);
    if (!RecursiveCalculateTotalSize(totalSz, nullptr, dirIn))
        return -1;
    return totalSz;
}

bool DiscBuilderBase::PartitionBuilderBase::mergeFromDirectory(IPartWriteStream& ws,
                                                               const nod::Partition* partIn,
                                                               const SystemChar* dirIn)
{
    if (!dirIn)
    {
        LogModule.report(logvisor::Error, _S("all arguments must be supplied to mergeFromDirectory()"));
        return false;
    }

    /* 1st pass - Tally up total progress steps */
    m_parent.m_progressTotal += 2; /* Prep and DOL */
    recursiveMergeNodesPre(&partIn->getFSTRoot(), dirIn);

    /* Clear file */
    m_parent.m_progressCB(m_parent.getProgressFactor(), _S("Preparing output image"), -1);
    ++m_parent.m_progressIdx;

    /* Add root node */
    m_buildNodes.emplace_back(true, m_buildNameOff, 0, 1);
    addBuildName(_S("<root>"));

    /* Write Boot DOL first (first thing seeked to after Apploader) */
    {
        size_t xferSz = partIn->getDOLSize();
        size_t fileSz = ROUND_UP_32(xferSz);
        uint64_t fileOff = userAllocate(fileSz, ws);
        if (fileOff == -1)
            return false;
        m_dolOffset = fileOff;
        m_dolSize = fileSz;
        std::unique_ptr<uint8_t[]> dolBuf = partIn->getDOLBuf();
        bool patched;
        PatchDOL(dolBuf, xferSz, patched);
        ws.write(dolBuf.get(), xferSz);
        m_parent.m_progressCB(m_parent.getProgressFactor(), SystemString(_S("<boot-dol>")) +
                              (patched ? _S(" [PATCHED]") : _S("")), xferSz);
        ++m_parent.m_progressIdx;
        for (size_t i=0 ; i<fileSz-xferSz ; ++i)
            ws.write("\xff", 1);
    }

    /* Gather files in root directory */
    SystemString keyPath;
    if (!recursiveMergeNodes(ws, true, &partIn->getFSTRoot(), dirIn, keyPath))
        return false;
    if (!recursiveMergeNodes(ws, false, &partIn->getFSTRoot(), dirIn, keyPath))
        return false;
    if (!recursiveMergeFST(&partIn->getFSTRoot(), dirIn, [&](){m_buildNodes[0].incrementLength();}, keyPath))
        return false;

    return true;
}

uint64_t DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeMerge(const DiscBase::IPartition* partIn,
                                                                        const SystemChar* dirIn)
{
    uint64_t totalSz = ROUND_UP_32(partIn->getDOLSize());
    if (!RecursiveCalculateTotalSize(totalSz, &partIn->getFSTRoot(), dirIn))
        return -1;
    return totalSz;
}

}
