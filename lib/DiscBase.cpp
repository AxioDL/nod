#include "nod/DiscBase.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "nod/DirectoryEnumerator.hpp"
#include "nod/IFileIO.hpp"
#include "nod/nod.hpp"
#include "nod/Util.hpp"

#ifndef _WIN32
#include <unistd.h>
#else

#include <logvisor/logvisor.hpp>

static void* memmem(const void* haystack, size_t hlen, const void* needle, size_t nlen) {
  int needle_first;
  const uint8_t* p = static_cast<const uint8_t*>(haystack);
  size_t plen = hlen;

  if (!nlen)
    return NULL;

  needle_first = *(unsigned char*)needle;

  while (plen >= nlen && (p = static_cast<const uint8_t*>(memchr(p, needle_first, plen - nlen + 1)))) {
    if (!memcmp(p, needle, nlen))
      return (void*)p;

    p++;
    plen = hlen - (p - static_cast<const uint8_t*>(haystack));
  }

  return NULL;
}
#endif

#include <algorithm>

namespace nod {

const SystemChar* getKindString(PartitionKind kind) {
  switch (kind) {
  case PartitionKind::Data:
    return _SYS_STR("DATA");
  case PartitionKind::Update:
    return _SYS_STR("UPDATE");
  case PartitionKind::Channel:
    return _SYS_STR("CHANNEL");
  default:
    return nullptr;
  }
}

void IPartition::parseFST(IPartReadStream& s) {
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
  for (uint32_t n = 0; n < nodeCount; ++n) {
    const FSTNode& node = nodes[n];
    m_nodes.emplace_back(*this, node, n ? names + node.getNameOffset() : "");
  }

  /* Setup dir-child iterators */
  for (std::vector<Node>::iterator it = m_nodes.begin(); it != m_nodes.end(); ++it) {
    Node& node = *it;
    if (node.m_kind == Node::Kind::Directory) {
      node.m_childrenBegin = it + 1;
      node.m_childrenEnd = m_nodes.begin() + node.m_discLength;
    }
  }
}

void IPartition::parseDOL(IPartReadStream& s) {
  /* Read Dol header */
  DOLHeader dolHeader;
  s.read(&dolHeader, sizeof(DOLHeader));

  /* Calculate Dol size */
  uint32_t dolSize = SBig(dolHeader.textOff[0]);
  for (uint32_t i = 0; i < 7; i++)
    dolSize += SBig(dolHeader.textSizes[i]);
  for (uint32_t i = 0; i < 11; i++)
    dolSize += SBig(dolHeader.dataSizes[i]);

  m_dolSz = dolSize;
}

Node::Node(const IPartition& parent, const FSTNode& node, std::string_view name)
: m_parent(parent)
, m_kind(node.isDir() ? Kind::Directory : Kind::File)
, m_discOffset(parent.normalizeOffset(node.getOffset()))
, m_discLength(node.getLength())
, m_name(name) {}

std::unique_ptr<IPartReadStream> Node::beginReadStream(uint64_t offset) const {
  if (m_kind != Kind::File) {
    LogModule.report(logvisor::Error, FMT_STRING("unable to stream a non-file {}"), m_name);
    return std::unique_ptr<IPartReadStream>();
  }
  return m_parent.beginReadStream(m_discOffset + offset);
}

std::unique_ptr<uint8_t[]> Node::getBuf() const {
  if (m_kind != Kind::File) {
    LogModule.report(logvisor::Error, FMT_STRING("unable to buffer a non-file {}"), m_name);
    return nullptr;
  }

  auto buf = std::unique_ptr<uint8_t[]>(new uint8_t[m_discLength]);
  beginReadStream()->read(buf.get(), m_discLength);
  return buf;
}

bool Node::extractToDirectory(SystemStringView basePath, const ExtractionContext& ctx) const {
  SystemStringConv nameView(getName());
  SystemString path = SystemString(basePath) + _SYS_STR('/') + nameView.sys_str().data();

  if (m_kind == Kind::Directory) {
    ++m_parent.m_curNodeIdx;
    if (ctx.progressCB && !getName().empty())
      ctx.progressCB(getName(), m_parent.m_curNodeIdx / float(m_parent.getNodeCount()));
    if (Mkdir(path.c_str(), 0755) && errno != EEXIST) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to mkdir '{}'")), path);
      return false;
    }
    for (Node& subnode : *this)
      if (!subnode.extractToDirectory(path, ctx))
        return false;
  } else if (m_kind == Kind::File) {
    Sstat theStat;
    if (ctx.progressCB)
      ctx.progressCB(getName(), m_parent.m_curNodeIdx / float(m_parent.getNodeCount()));

    if (ctx.force || Stat(path.c_str(), &theStat)) {
      std::unique_ptr<IPartReadStream> rs = beginReadStream();
      std::unique_ptr<IFileIO::IWriteStream> ws = NewFileIO(path)->beginWriteStream();
      if (!rs || !ws)
        return false;
      ws->copyFromDisc(*rs, m_discLength, [&](float prog) {
        if (ctx.progressCB)
          ctx.progressCB(getName(), (m_parent.m_curNodeIdx + prog) / float(m_parent.getNodeCount()));
      });
    }
    ++m_parent.m_curNodeIdx;
  }
  return true;
}

bool IPartition::extractToDirectory(SystemStringView path, const ExtractionContext& ctx) {
  m_curNodeIdx = 0;
  if (Mkdir(path.data(), 0755) && errno != EEXIST) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to mkdir '{}'")), path);
    return false;
  }

  SystemString basePath = m_isWii ? SystemString(path) + _SYS_STR("/") + getKindString(m_kind) : SystemString(path);
  if (m_isWii) {
    if (Mkdir(basePath.c_str(), 0755) && errno != EEXIST) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to mkdir '{}'")), basePath);
      return false;
    }
  }

  /* Extract Disc Files */
  if (!m_parent.extractDiscHeaderFiles(basePath, ctx))
    return false;

  /* Extract Crypto Files */
  if (!extractCryptoFiles(basePath, ctx))
    return false;

  if (!extractSysFiles(basePath, ctx))
    return false;

  /* Extract Filesystem */
  SystemString fsPath = basePath + _SYS_STR("/files");
  if (Mkdir(fsPath.c_str(), 0755) && errno != EEXIST) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to mkdir '{}'")), fsPath);
    return false;
  }

  return m_nodes[0].extractToDirectory(fsPath, ctx);
}

bool IPartition::extractSysFiles(SystemStringView basePath, const ExtractionContext& ctx) const {
  SystemString basePathStr(basePath);
  if (Mkdir((basePathStr + _SYS_STR("/sys")).c_str(), 0755) && errno != EEXIST) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to mkdir '{}/sys'")), basePath);
    return false;
  }

  Sstat theStat;
  /* Extract Apploader */
  SystemString apploaderPath = basePathStr + _SYS_STR("/sys/apploader.img");
  if (ctx.force || Stat(apploaderPath.c_str(), &theStat)) {
    if (ctx.progressCB)
      ctx.progressCB("apploader.bin", 0.f);
    std::unique_ptr<uint8_t[]> buf = getApploaderBuf();
    auto ws = NewFileIO(apploaderPath)->beginWriteStream();
    if (!ws)
      return false;
    ws->write(buf.get(), m_apploaderSz);
  }

  /* Extract Dol */
  SystemString dolPath = basePathStr + _SYS_STR("/sys/main.dol");
  if (ctx.force || Stat(dolPath.c_str(), &theStat)) {
    if (ctx.progressCB)
      ctx.progressCB("main.dol", 0.f);
    std::unique_ptr<uint8_t[]> buf = getDOLBuf();
    auto ws = NewFileIO(dolPath)->beginWriteStream();
    if (!ws)
      return false;
    ws->write(buf.get(), m_dolSz);
  }

  /* Extract Boot info */
  SystemString bootPath = basePathStr + _SYS_STR("/sys/boot.bin");
  if (ctx.force || Stat(bootPath.c_str(), &theStat)) {
    if (ctx.progressCB)
      ctx.progressCB("boot.bin", 0.f);
    auto ws = NewFileIO(bootPath)->beginWriteStream();
    if (!ws)
      return false;
    m_header.write(*ws.get());
  }

  /* Extract BI2 info */
  SystemString bi2Path = basePathStr + _SYS_STR("/sys/bi2.bin");
  if (ctx.force || Stat(bi2Path.c_str(), &theStat)) {
    if (ctx.progressCB)
      ctx.progressCB("bi2.bin", 0.f);

    auto ws = NewFileIO(bi2Path)->beginWriteStream();
    if (!ws)
      return false;
    m_bi2Header.write(*ws);
  }

  return true;
}

static bool IsSystemFile(SystemStringView name, bool& isDol) {
  isDol = false;
  if (name.size() < 4)
    return false;

  if (!StrCaseCmp((&*(name.cend() - 4)), _SYS_STR(".dol"))) {
    isDol = true;
    return true;
  }
  if (!StrCaseCmp((&*(name.cend() - 4)), _SYS_STR(".rel")))
    return true;
  if (!StrCaseCmp((&*(name.cend() - 4)), _SYS_STR(".rso")))
    return true;
  if (!StrCaseCmp((&*(name.cend() - 4)), _SYS_STR(".sel")))
    return true;
  if (!StrCaseCmp((&*(name.cend() - 4)), _SYS_STR(".bnr")))
    return true;
  if (!StrCaseCmp((&*(name.cend() - 4)), _SYS_STR(".elf")))
    return true;
  if (!StrCaseCmp((&*(name.cend() - 4)), _SYS_STR(".wad")))
    return true;

  return false;
}

/** Patches out pesky #001 integrity check performed by game's OSInit.
 *  This is required for multi-DOL games, but doesn't harm functionality otherwise */
static void PatchDOL(std::unique_ptr<uint8_t[]>& buf, size_t sz, bool& patched) {
  patched = false;
  uint8_t* found = static_cast<uint8_t*>(memmem(buf.get(), sz,
                                                "\x3C\x03\xF8\x00\x28\x00\x00\x00\x40\x82\x00\x0C"
                                                "\x38\x60\x00\x01\x48\x00\x02\x44\x38\x61\x00\x18\x48",
                                                25));
  if (found) {
    found[11] = '\x04';
    patched = true;
  }
}

static size_t PatchDOL(IFileIO::IReadStream& in, IPartWriteStream& out, size_t sz, bool& patched) {
  std::unique_ptr<uint8_t[]> buf(new uint8_t[sz]);
  sz = in.read(buf.get(), sz);
  PatchDOL(buf, sz, patched);
  return out.write(buf.get(), sz);
}

void DiscBuilderBase::PartitionBuilderBase::recursiveBuildNodesPre(SystemStringView filesIn) {
  DirectoryEnumerator dEnum(filesIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
  for (const DirectoryEnumerator::Entry& e : dEnum) {
    if (e.m_isDir)
      recursiveBuildNodesPre(e.m_path.c_str());
    else
      ++m_parent.m_progressTotal;
  }
}

bool DiscBuilderBase::PartitionBuilderBase::recursiveBuildNodes(IPartWriteStream& ws, bool system,
                                                                SystemStringView filesIn) {
  DirectoryEnumerator dEnum(filesIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
  for (const DirectoryEnumerator::Entry& e : dEnum) {
    if (e.m_isDir) {
      if (!recursiveBuildNodes(ws, system, e.m_path.c_str()))
        return false;
    } else {
      bool isDol;
      bool isSys = IsSystemFile(e.m_name, isDol);
      if (system ^ isSys)
        continue;

      size_t fileSz = ROUND_UP_32(e.m_fileSz);
      uint64_t fileOff = userAllocate(fileSz, ws);
      if (fileOff == UINT64_MAX)
        return false;
      m_fileOffsetsSizes[e.m_path] = std::make_pair(fileOff, fileSz);
      std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(e.m_path)->beginReadStream();
      if (!rs)
        return false;
      size_t xferSz = 0;
      if (isDol) {
        bool patched;
        xferSz = PatchDOL(*rs, ws, e.m_fileSz, patched);
        m_parent.m_progressCB(m_parent.getProgressFactor(),
                              e.m_name + (patched ? _SYS_STR(" [PATCHED]") : _SYS_STR("")), xferSz);
        ++m_parent.m_progressIdx;
      } else {
        char buf[0x8000];
        while (xferSz < e.m_fileSz) {
          size_t rdSz = rs->read(buf, nod::min(size_t(0x8000ul), e.m_fileSz - xferSz));
          if (!rdSz)
            break;
          ws.write(buf, rdSz);
          xferSz += rdSz;
          m_parent.m_progressCB(m_parent.getProgressFactorMidFile(xferSz, e.m_fileSz), e.m_name, xferSz);
        }
        ++m_parent.m_progressIdx;
      }
      for (size_t i = 0; i < fileSz - xferSz; ++i)
        ws.write("\xff", 1);
    }
  }

  return true;
}

bool DiscBuilderBase::PartitionBuilderBase::recursiveBuildFST(SystemStringView filesIn,
                                                              std::function<void(void)> incParents,
                                                              size_t parentDirIdx) {
  DirectoryEnumerator dEnum(filesIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
  for (const DirectoryEnumerator::Entry& e : dEnum) {
    if (e.m_isDir) {
      size_t dirNodeIdx = m_buildNodes.size();
      m_buildNodes.emplace_back(true, m_buildNameOff, parentDirIdx, dirNodeIdx + 1);
      addBuildName(e.m_name);
      incParents();
      if (!recursiveBuildFST(e.m_path.c_str(),
                             [&]() {
                               m_buildNodes[dirNodeIdx].incrementLength();
                               incParents();
                             },
                             dirNodeIdx))
        return false;
    } else {
      std::pair<uint64_t, uint64_t> fileOffSz = m_fileOffsetsSizes.at(e.m_path);
      m_buildNodes.emplace_back(false, m_buildNameOff, packOffset(fileOffSz.first), fileOffSz.second);
      addBuildName(e.m_name);
      incParents();
    }
  }

  return true;
}

void DiscBuilderBase::PartitionBuilderBase::recursiveMergeNodesPre(const Node* nodeIn, SystemStringView dirIn) {
  /* Build map of existing nodes to write-through later */
  std::unordered_map<std::string, const Node*> fileNodes;
  std::unordered_map<std::string, const Node*> dirNodes;
  if (nodeIn) {
    fileNodes.reserve(nodeIn->size());
    dirNodes.reserve(nodeIn->size());
    for (const Node& ch : *nodeIn) {
      if (ch.getKind() == Node::Kind::File)
        fileNodes.insert(std::make_pair(ch.getName(), &ch));
      else if (ch.getKind() == Node::Kind::Directory)
        dirNodes.insert(std::make_pair(ch.getName(), &ch));
    }
  }

  /* Merge this directory's files */
  if (!dirIn.empty()) {
    DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
    for (const DirectoryEnumerator::Entry& e : dEnum) {
      SystemUTF8Conv nameView(e.m_name);

      if (e.m_isDir) {
        auto search = dirNodes.find(nameView.utf8_str().data());
        if (search != dirNodes.cend()) {
          recursiveMergeNodesPre(search->second, e.m_path.c_str());
          dirNodes.erase(search);
        } else {
          recursiveMergeNodesPre(nullptr, e.m_path.c_str());
        }
      } else {
        fileNodes.erase(nameView.utf8_str().data());
        ++m_parent.m_progressTotal;
      }
    }
  }

  /* Write-through remaining dir nodes */
  for (const auto& p : dirNodes) {
    recursiveMergeNodesPre(p.second, {});
  }

  /* Write-through remaining file nodes */
  m_parent.m_progressTotal += fileNodes.size();
}

bool DiscBuilderBase::PartitionBuilderBase::recursiveMergeNodes(IPartWriteStream& ws, bool system, const Node* nodeIn,
                                                                SystemStringView dirIn, SystemStringView keyPath) {
  /* Build map of existing nodes to write-through later */
  std::unordered_map<std::string, const Node*> fileNodes;
  std::unordered_map<std::string, const Node*> dirNodes;
  if (nodeIn) {
    fileNodes.reserve(nodeIn->size());
    dirNodes.reserve(nodeIn->size());
    for (const Node& ch : *nodeIn) {
      if (ch.getKind() == Node::Kind::File)
        fileNodes.insert(std::make_pair(ch.getName(), &ch));
      else if (ch.getKind() == Node::Kind::Directory)
        dirNodes.insert(std::make_pair(ch.getName(), &ch));
    }
  }

  /* Merge this directory's files */
  if (!dirIn.empty()) {
    DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
    for (const DirectoryEnumerator::Entry& e : dEnum) {
      SystemUTF8Conv nameView(e.m_name);
      SystemString chKeyPath = SystemString(keyPath) + _SYS_STR('/') + e.m_name;

      if (e.m_isDir) {
        auto search = dirNodes.find(nameView.utf8_str().data());
        if (search != dirNodes.cend()) {
          if (!recursiveMergeNodes(ws, system, search->second, e.m_path.c_str(), chKeyPath))
            return false;
          dirNodes.erase(search);
        } else {
          if (!recursiveMergeNodes(ws, system, nullptr, e.m_path.c_str(), chKeyPath))
            return false;
        }
      } else {
        bool isDol;
        bool isSys = IsSystemFile(e.m_name, isDol);
        if (system ^ isSys)
          continue;

        fileNodes.erase(nameView.utf8_str().data());

        size_t fileSz = ROUND_UP_32(e.m_fileSz);
        uint64_t fileOff = userAllocate(fileSz, ws);
        if (fileOff == UINT64_MAX)
          return false;
        m_fileOffsetsSizes[chKeyPath] = std::make_pair(fileOff, fileSz);
        std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(e.m_path)->beginReadStream();
        if (!rs)
          return false;
        size_t xferSz = 0;
        if (isDol) {
          bool patched;
          xferSz = PatchDOL(*rs, ws, e.m_fileSz, patched);
          m_parent.m_progressCB(m_parent.getProgressFactor(),
                                e.m_name + (patched ? _SYS_STR(" [PATCHED]") : _SYS_STR("")), xferSz);
          ++m_parent.m_progressIdx;
        } else {
          char buf[0x8000];
          while (xferSz < e.m_fileSz) {
            size_t rdSz = rs->read(buf, nod::min(size_t(0x8000ul), e.m_fileSz - xferSz));
            if (!rdSz)
              break;
            ws.write(buf, rdSz);
            xferSz += rdSz;
            m_parent.m_progressCB(m_parent.getProgressFactorMidFile(xferSz, e.m_fileSz), e.m_name, xferSz);
          }
          ++m_parent.m_progressIdx;
        }
        for (size_t i = 0; i < fileSz - xferSz; ++i)
          ws.write("\xff", 1);
      }
    }
  }

  /* Write-through remaining dir nodes */
  for (const auto& p : dirNodes) {
    SystemStringConv sysName(p.second->getName());
    SystemString chKeyPath = SystemString(keyPath) + _SYS_STR('/') + sysName.c_str();
    if (!recursiveMergeNodes(ws, system, p.second, {}, chKeyPath))
      return false;
  }

  /* Write-through remaining file nodes */
  for (const auto& p : fileNodes) {
    const Node& ch = *p.second;
    SystemStringConv sysName(ch.getName());
    SystemString chKeyPath = SystemString(keyPath) + _SYS_STR('/') + sysName.c_str();

    bool isDol;
    bool isSys = IsSystemFile(sysName.sys_str(), isDol);
    if (system ^ isSys)
      continue;

    size_t fileSz = ROUND_UP_32(ch.size());
    uint64_t fileOff = userAllocate(fileSz, ws);
    if (fileOff == UINT64_MAX)
      return false;
    m_fileOffsetsSizes[chKeyPath] = std::make_pair(fileOff, fileSz);
    std::unique_ptr<IPartReadStream> rs = ch.beginReadStream();
    if (!rs)
      return false;
    size_t xferSz = 0;
    if (isDol) {
      xferSz = ch.size();
      std::unique_ptr<uint8_t[]> dolBuf = ch.getBuf();
      bool patched;
      PatchDOL(dolBuf, xferSz, patched);
      ws.write(dolBuf.get(), xferSz);
      m_parent.m_progressCB(m_parent.getProgressFactor(),
                            SystemString(sysName.sys_str()) + (patched ? _SYS_STR(" [PATCHED]") : _SYS_STR("")),
                            xferSz);
      ++m_parent.m_progressIdx;
    } else {
      char buf[0x8000];
      while (xferSz < ch.size()) {
        size_t rdSz = rs->read(buf, nod::min(size_t(0x8000), size_t(ch.size() - xferSz)));
        if (!rdSz)
          break;
        ws.write(buf, rdSz);
        xferSz += rdSz;
        m_parent.m_progressCB(m_parent.getProgressFactorMidFile(xferSz, ch.size()), sysName.sys_str(), xferSz);
      }
      ++m_parent.m_progressIdx;
    }
    for (size_t i = 0; i < fileSz - xferSz; ++i)
      ws.write("\xff", 1);
  }

  return true;
}

bool DiscBuilderBase::PartitionBuilderBase::recursiveMergeFST(const Node* nodeIn, SystemStringView dirIn,
                                                              std::function<void(void)> incParents,
                                                              SystemStringView keyPath) {
  /* Build map of existing nodes to write-through later */
  std::unordered_map<std::string, const Node*> fileNodes;
  std::unordered_map<std::string, const Node*> dirNodes;
  if (nodeIn) {
    fileNodes.reserve(nodeIn->size());
    dirNodes.reserve(nodeIn->size());
    for (const Node& ch : *nodeIn) {
      if (ch.getKind() == Node::Kind::File)
        fileNodes.insert(std::make_pair(ch.getName(), &ch));
      else if (ch.getKind() == Node::Kind::Directory)
        dirNodes.insert(std::make_pair(ch.getName(), &ch));
    }
  }

  /* Merge this directory's files */
  if (!dirIn.empty()) {
    DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
    for (const DirectoryEnumerator::Entry& e : dEnum) {
      SystemUTF8Conv nameView(e.m_name);
      SystemString chKeyPath = SystemString(keyPath) + _SYS_STR('/') + e.m_name;

      if (e.m_isDir) {
        size_t dirNodeIdx = m_buildNodes.size();
        m_buildNodes.emplace_back(true, m_buildNameOff, 0, dirNodeIdx + 1);
        addBuildName(e.m_name);
        incParents();

        auto search = dirNodes.find(nameView.utf8_str().data());
        if (search != dirNodes.cend()) {
          if (!recursiveMergeFST(search->second, e.m_path.c_str(),
                                 [&]() {
                                   m_buildNodes[dirNodeIdx].incrementLength();
                                   incParents();
                                 },
                                 chKeyPath))
            return false;
          dirNodes.erase(search);
        } else {
          if (!recursiveMergeFST(nullptr, e.m_path.c_str(),
                                 [&]() {
                                   m_buildNodes[dirNodeIdx].incrementLength();
                                   incParents();
                                 },
                                 chKeyPath))
            return false;
        }
      } else {
        fileNodes.erase(nameView.utf8_str().data());
        std::pair<uint64_t, uint64_t> fileOffSz = m_fileOffsetsSizes.at(chKeyPath);
        m_buildNodes.emplace_back(false, m_buildNameOff, packOffset(fileOffSz.first), fileOffSz.second);
        addBuildName(e.m_name);
        incParents();
      }
    }
  }

  /* Write-through remaining dir nodes */
  for (const auto& p : dirNodes) {
    SystemStringConv sysName(p.second->getName());
    SystemString chKeyPath = SystemString(keyPath) + _SYS_STR('/') + sysName.sys_str().data();

    size_t dirNodeIdx = m_buildNodes.size();
    m_buildNodes.emplace_back(true, m_buildNameOff, 0, dirNodeIdx + 1);
    addBuildName(sysName.sys_str());
    incParents();

    if (!recursiveMergeFST(p.second, {},
                           [&]() {
                             m_buildNodes[dirNodeIdx].incrementLength();
                             incParents();
                           },
                           chKeyPath))
      return false;
  }

  /* Write-through remaining file nodes */
  for (const auto& p : fileNodes) {
    const Node& ch = *p.second;
    SystemStringConv sysName(ch.getName());
    SystemString chKeyPath = SystemString(keyPath) + _SYS_STR('/') + sysName.sys_str().data();

    std::pair<uint64_t, uint64_t> fileOffSz = m_fileOffsetsSizes.at(chKeyPath);
    m_buildNodes.emplace_back(false, m_buildNameOff, packOffset(fileOffSz.first), fileOffSz.second);
    addBuildName(sysName.sys_str());
    incParents();
  }

  return true;
}

bool DiscBuilderBase::PartitionBuilderBase::RecursiveCalculateTotalSize(uint64_t& totalSz, const Node* nodeIn,
                                                                        SystemStringView dirIn) {
  /* Build map of existing nodes to write-through later */
  std::unordered_map<std::string, const Node*> fileNodes;
  std::unordered_map<std::string, const Node*> dirNodes;
  if (nodeIn) {
    fileNodes.reserve(nodeIn->size());
    dirNodes.reserve(nodeIn->size());
    for (const Node& ch : *nodeIn) {
      if (ch.getKind() == Node::Kind::File)
        fileNodes.insert(std::make_pair(ch.getName(), &ch));
      else if (ch.getKind() == Node::Kind::Directory)
        dirNodes.insert(std::make_pair(ch.getName(), &ch));
    }
  }

  /* Merge this directory's files */
  if (!dirIn.empty()) {
    DirectoryEnumerator dEnum(dirIn, DirectoryEnumerator::Mode::DirsThenFilesSorted, false, false, true);
    for (const DirectoryEnumerator::Entry& e : dEnum) {
      SystemUTF8Conv nameView(e.m_name);

      if (e.m_isDir) {
        auto search = dirNodes.find(nameView.utf8_str().data());
        if (search != dirNodes.cend()) {
          if (!RecursiveCalculateTotalSize(totalSz, search->second, e.m_path.c_str()))
            return false;
          dirNodes.erase(search);
        } else {
          if (!RecursiveCalculateTotalSize(totalSz, nullptr, e.m_path.c_str()))
            return false;
        }
      } else {
        fileNodes.erase(nameView.utf8_str().data());
        totalSz += ROUND_UP_32(e.m_fileSz);
      }
    }
  }

  /* Write-through remaining dir nodes */
  for (const auto& p : dirNodes) {
    if (!RecursiveCalculateTotalSize(totalSz, p.second, {}))
      return false;
  }

  /* Write-through remaining file nodes */
  for (const auto& p : fileNodes) {
    const Node& ch = *p.second;
    totalSz += ROUND_UP_32(ch.size());
  }

  return true;
}

bool DiscBuilderBase::PartitionBuilderBase::buildFromDirectory(IPartWriteStream& ws, SystemStringView dirIn) {
  if (dirIn.empty()) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("all arguments must be supplied to buildFromDirectory()")));
    return false;
  }

  SystemString dirStr(dirIn);
  SystemString basePath = m_isWii ? dirStr + _SYS_STR("/") + getKindString(m_kind) : dirStr;
  SystemString dolIn = basePath + _SYS_STR("/sys/main.dol");
  SystemString filesIn = basePath + _SYS_STR("/files");

  /* 1st pass - Tally up total progress steps */
  m_parent.m_progressTotal += 2; /* Prep and DOL */
  recursiveBuildNodesPre(filesIn.c_str());

  /* Clear file */
  m_parent.m_progressCB(m_parent.getProgressFactor(), _SYS_STR("Preparing output image"), -1);
  ++m_parent.m_progressIdx;

  /* Add root node */
  m_buildNodes.emplace_back(true, m_buildNameOff, 0, 1);
  addBuildName(_SYS_STR("<root>"));

  /* Write Boot DOL first (first thing seeked to after Apploader) */
  {
    Sstat dolStat;
    if (Stat(dolIn.c_str(), &dolStat)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), dolIn);
      return false;
    }
    size_t fileSz = ROUND_UP_32(dolStat.st_size);
    uint64_t fileOff = userAllocate(fileSz, ws);
    if (fileOff == UINT64_MAX)
      return false;
    m_dolOffset = fileOff;
    m_dolSize = fileSz;
    std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(dolIn.c_str())->beginReadStream();
    if (!rs)
      return false;
    bool patched;
    size_t xferSz = PatchDOL(*rs, ws, dolStat.st_size, patched);
    m_parent.m_progressCB(m_parent.getProgressFactor(), dolIn + (patched ? _SYS_STR(" [PATCHED]") : _SYS_STR("")),
                          xferSz);
    ++m_parent.m_progressIdx;
    for (size_t i = 0; i < fileSz - xferSz; ++i)
      ws.write("\xff", 1);
  }

  /* Gather files in root directory */
  if (!recursiveBuildNodes(ws, true, filesIn.c_str()))
    return false;
  if (!recursiveBuildNodes(ws, false, filesIn.c_str()))
    return false;
  if (!recursiveBuildFST(filesIn.c_str(), [&]() { m_buildNodes[0].incrementLength(); }, 0))
    return false;

  return true;
}

std::optional<uint64_t> DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeBuild(SystemStringView dirIn,
                                                                                       PartitionKind kind, bool isWii) {
  SystemString dirStr(dirIn);
  SystemString basePath = isWii ? dirStr + _SYS_STR("/") + getKindString(kind) : dirStr;
  SystemString dolIn = basePath + _SYS_STR("/sys/main.dol");
  SystemString filesIn = basePath + _SYS_STR("/files");

  Sstat dolStat;
  if (Stat(dolIn.c_str(), &dolStat)) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), dolIn);
    return std::nullopt;
  }
  uint64_t totalSz = ROUND_UP_32(dolStat.st_size);
  if (!RecursiveCalculateTotalSize(totalSz, nullptr, filesIn.c_str()))
    return std::nullopt;
  return totalSz;
}

bool DiscBuilderBase::PartitionBuilderBase::mergeFromDirectory(IPartWriteStream& ws, const IPartition* partIn,
                                                               SystemStringView dirIn) {
  if (dirIn.empty()) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("all arguments must be supplied to mergeFromDirectory()")));
    return false;
  }

  SystemString dirStr(dirIn);
  SystemString basePath = m_isWii ? dirStr + _SYS_STR("/") + getKindString(m_kind) : dirStr;
  SystemString filesIn = basePath + _SYS_STR("/files");

  /* 1st pass - Tally up total progress steps */
  m_parent.m_progressTotal += 2; /* Prep and DOL */
  recursiveMergeNodesPre(&partIn->getFSTRoot(), filesIn.c_str());

  /* Clear file */
  m_parent.m_progressCB(m_parent.getProgressFactor(), _SYS_STR("Preparing output image"), -1);
  ++m_parent.m_progressIdx;

  /* Add root node */
  m_buildNodes.emplace_back(true, m_buildNameOff, 0, 1);
  addBuildName(_SYS_STR("<root>"));

  /* Write Boot DOL first (first thing seeked to after Apploader) */
  {
    size_t xferSz = partIn->getDOLSize();
    size_t fileSz = ROUND_UP_32(xferSz);
    uint64_t fileOff = userAllocate(fileSz, ws);
    if (fileOff == UINT64_MAX)
      return false;
    m_dolOffset = fileOff;
    m_dolSize = fileSz;
    std::unique_ptr<uint8_t[]> dolBuf = partIn->getDOLBuf();
    bool patched;
    PatchDOL(dolBuf, xferSz, patched);
    ws.write(dolBuf.get(), xferSz);
    m_parent.m_progressCB(m_parent.getProgressFactor(),
                          SystemString(_SYS_STR("<boot-dol>")) + (patched ? _SYS_STR(" [PATCHED]") : _SYS_STR("")),
                          xferSz);
    ++m_parent.m_progressIdx;
    for (size_t i = 0; i < fileSz - xferSz; ++i)
      ws.write("\xff", 1);
  }

  /* Gather files in root directory */
  SystemString keyPath;
  if (!recursiveMergeNodes(ws, true, &partIn->getFSTRoot(), filesIn.c_str(), keyPath))
    return false;
  if (!recursiveMergeNodes(ws, false, &partIn->getFSTRoot(), filesIn.c_str(), keyPath))
    return false;
  if (!recursiveMergeFST(&partIn->getFSTRoot(), filesIn.c_str(), [&]() { m_buildNodes[0].incrementLength(); }, keyPath))
    return false;

  return true;
}

std::optional<uint64_t> DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeMerge(const IPartition* partIn,
                                                                                       SystemStringView dirIn) {
  SystemString dirStr(dirIn);
  SystemString basePath = partIn->isWii() ? dirStr + _SYS_STR("/") + getKindString(partIn->getKind()) : dirStr;
  SystemString filesIn = basePath + _SYS_STR("/files");

  uint64_t totalSz = ROUND_UP_32(partIn->getDOLSize());
  if (!RecursiveCalculateTotalSize(totalSz, &partIn->getFSTRoot(), filesIn.c_str()))
    return std::nullopt;
  return totalSz;
}

} // namespace nod
