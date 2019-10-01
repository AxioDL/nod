#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nod/IDiscIO.hpp"
#include "nod/IFileIO.hpp"
#include "nod/Util.hpp"

namespace nod {

using FProgress = std::function<void(float totalProg, SystemStringView fileName, size_t fileBytesXfered)>;

enum class EBuildResult { Success, Failed, DiskFull };

enum class PartitionKind : uint32_t { Data, Update, Channel };
const SystemChar* getKindString(PartitionKind kind);

class FSTNode {
  uint32_t typeAndNameOffset;
  uint32_t offset;
  uint32_t length;

public:
  FSTNode(bool isDir, uint32_t nameOff, uint32_t off, uint32_t len) {
    typeAndNameOffset = nameOff & 0xffffff;
    typeAndNameOffset |= isDir << 24;
    typeAndNameOffset = SBig(typeAndNameOffset);
    offset = SBig(off);
    length = SBig(len);
  }
  bool isDir() const { return ((SBig(typeAndNameOffset) >> 24) != 0); }
  uint32_t getNameOffset() const { return SBig(typeAndNameOffset) & 0xffffff; }
  uint32_t getOffset() const { return SBig(offset); }
  uint32_t getLength() const { return SBig(length); }
  void incrementLength() {
    uint32_t orig = SBig(length);
    ++orig;
    length = SBig(orig);
  }
};

struct Header {
  char m_gameID[6];
  char m_discNum;
  char m_discVersion;
  char m_audioStreaming;
  char m_streamBufSz;
  char m_unk1[14];
  uint32_t m_wiiMagic;
  uint32_t m_gcnMagic;
  char m_gameTitle[64];
  char m_disableHashVerification;
  char m_disableDiscEnc;
  char m_unk2[0x39e];
  uint32_t m_debugMonOff;
  uint32_t m_debugLoadAddr;
  char m_unk3[0x18];
  uint32_t m_dolOff;
  uint32_t m_fstOff;
  uint32_t m_fstSz;
  uint32_t m_fstMaxSz;
  uint32_t m_fstMemoryAddress;
  uint32_t m_userPosition;
  uint32_t m_userSz;
  uint8_t padding1[4];

  Header() = default;
  Header(IDiscIO& dio, bool& err) {
    auto rs = dio.beginReadStream();
    if (!rs) {
      err = true;
      return;
    }
    read(*rs);
  }

  void read(IReadStream& s) {
    memset(this, 0, sizeof(*this));
    s.read(this, sizeof(*this));
    m_wiiMagic = SBig(m_wiiMagic);
    m_gcnMagic = SBig(m_gcnMagic);
    m_debugMonOff = SBig(m_debugMonOff);
    m_debugLoadAddr = SBig(m_debugLoadAddr);
    m_dolOff = SBig(m_dolOff);
    m_fstOff = SBig(m_fstOff);
    m_fstSz = SBig(m_fstSz);
    m_fstMaxSz = SBig(m_fstMaxSz);
    m_fstMemoryAddress = SBig(m_fstMemoryAddress);
    m_userPosition = SBig(m_userPosition);
    m_userSz = SBig(m_userSz);
  }

  void write(IWriteStream& ws) const {
    Header hs(*this);
    hs.m_wiiMagic = SBig(hs.m_wiiMagic);
    hs.m_gcnMagic = SBig(hs.m_gcnMagic);
    hs.m_debugMonOff = SBig(hs.m_debugMonOff);
    hs.m_debugLoadAddr = SBig(hs.m_debugLoadAddr);
    hs.m_dolOff = SBig(hs.m_dolOff);
    hs.m_fstOff = SBig(hs.m_fstOff);
    hs.m_fstSz = SBig(hs.m_fstSz);
    hs.m_fstMaxSz = SBig(hs.m_fstMaxSz);
    hs.m_fstMemoryAddress = SBig(hs.m_fstMemoryAddress);
    hs.m_userPosition = SBig(hs.m_userPosition);
    hs.m_userSz = SBig(hs.m_userSz);
    ws.write(&hs, sizeof(hs));
  }
};

/* Currently only kept for dolphin compatibility */
struct BI2Header {
  int32_t m_debugMonitorSize;
  int32_t m_simMemSize;
  uint32_t m_argOffset;
  uint32_t m_debugFlag;
  uint32_t m_trkAddress;
  uint32_t m_trkSz;
  uint32_t m_countryCode;
  uint32_t m_unk1;
  uint32_t m_unk2;
  uint32_t m_unk3;
  uint32_t m_dolLimit;
  uint32_t m_unk4;
  uint8_t padding2[0x1FD0];

  void read(IReadStream& rs) {
    memset(this, 0, sizeof(*this));
    rs.read(this, sizeof(*this));
    m_debugMonitorSize = SBig(m_debugMonitorSize);
    m_simMemSize = SBig(m_simMemSize);
    m_argOffset = SBig(m_argOffset);
    m_debugFlag = SBig(m_debugFlag);
    m_trkAddress = SBig(m_trkAddress);
    m_trkSz = SBig(m_trkSz);
    m_countryCode = SBig(m_countryCode);
    m_unk1 = SBig(m_unk1);
    m_unk2 = SBig(m_unk2);
    m_unk3 = SBig(m_unk3);
    m_dolLimit = SBig(m_dolLimit);
    m_unk4 = SBig(m_unk4);
  }

  void write(IWriteStream& ws) const {
    BI2Header h = *this;
    h.m_debugMonitorSize = SBig(h.m_debugMonitorSize);
    h.m_simMemSize = SBig(h.m_simMemSize);
    h.m_argOffset = SBig(h.m_argOffset);
    h.m_debugFlag = SBig(h.m_debugFlag);
    h.m_trkAddress = SBig(h.m_trkAddress);
    h.m_trkSz = SBig(h.m_trkSz);
    h.m_countryCode = SBig(h.m_countryCode);
    h.m_unk1 = SBig(h.m_unk1);
    h.m_unk2 = SBig(h.m_unk2);
    h.m_unk3 = SBig(h.m_unk3);
    h.m_dolLimit = SBig(h.m_dolLimit);
    h.m_unk4 = SBig(h.m_unk4);
    ws.write(&h, sizeof(h));
  }
};

struct ExtractionContext;
class IPartition;
class DiscBase;

class Node {
public:
  enum class Kind { File, Directory };

private:
  friend class IPartition;
  const IPartition& m_parent;
  Kind m_kind;

  uint64_t m_discOffset;
  uint64_t m_discLength;
  std::string m_name;

  std::vector<Node>::iterator m_childrenBegin;
  std::vector<Node>::iterator m_childrenEnd;

public:
  Node(const IPartition& parent, const FSTNode& node, std::string_view name);
  Kind getKind() const { return m_kind; }
  std::string_view getName() const { return m_name; }
  uint64_t size() const { return m_discLength; }
  std::unique_ptr<IPartReadStream> beginReadStream(uint64_t offset = 0) const;
  std::unique_ptr<uint8_t[]> getBuf() const;
  std::vector<Node>::iterator rawBegin() const { return m_childrenBegin; }
  std::vector<Node>::iterator rawEnd() const { return m_childrenEnd; }

  class DirectoryIterator {
    friend class Node;
    std::vector<Node>::iterator m_it;
    DirectoryIterator(const std::vector<Node>::iterator& it) : m_it(it) {}

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Node;
    using difference_type = std::ptrdiff_t;
    using pointer = Node*;
    using reference = Node&;

    bool operator==(const DirectoryIterator& other) const { return m_it == other.m_it; }
    bool operator!=(const DirectoryIterator& other) const { return !operator==(other); }
    DirectoryIterator& operator++() {
      if (m_it->m_kind == Kind::Directory)
        m_it = m_it->rawEnd();
      else
        ++m_it;
      return *this;
    }
    Node& operator*() { return *m_it; }
    const Node& operator*() const { return *m_it; }
    Node* operator->() { return &*m_it; }
    const Node* operator->() const { return &*m_it; }
  };
  DirectoryIterator begin() const { return DirectoryIterator(m_childrenBegin); }
  DirectoryIterator end() const { return DirectoryIterator(m_childrenEnd); }
  DirectoryIterator find(std::string_view name) const {
    if (m_kind == Kind::Directory) {
      DirectoryIterator it = begin();
      for (; it != end(); ++it) {
        if (it->getName() == name)
          return it;
      }
      return it;
    }
    return end();
  }

  bool extractToDirectory(SystemStringView basePath, const ExtractionContext& ctx) const;
};

class IPartition {
public:
  virtual ~IPartition() = default;
  struct DOLHeader {
    uint32_t textOff[7];
    uint32_t dataOff[11];
    uint32_t textStarts[7];
    uint32_t dataStarts[11];
    uint32_t textSizes[7];
    uint32_t dataSizes[11];
    uint32_t bssStart;
    uint32_t bssSize;
    uint32_t entryPoint;
  };

protected:
  Header m_header;
  BI2Header m_bi2Header;
  uint64_t m_dolOff;
  uint64_t m_fstOff;
  uint64_t m_fstSz;
  uint64_t m_apploaderSz;
  std::vector<Node> m_nodes;
  void parseFST(IPartReadStream& s);

  std::vector<FSTNode> m_buildNodes;
  std::vector<std::string> m_buildNames;
  size_t m_buildNameOff = 0;

  uint64_t m_dolSz;
  void parseDOL(IPartReadStream& s);

  const DiscBase& m_parent;
  PartitionKind m_kind;
  uint64_t m_offset;
  bool m_isWii;

public:
  mutable size_t m_curNodeIdx = 0;
  float getProgressFactor() const { return getNodeCount() ? m_curNodeIdx / float(getNodeCount()) : 0.f; }
  float getProgressFactorMidFile(size_t curByte, size_t totalBytes) const {
    if (!getNodeCount())
      return 0.f;

    if (totalBytes)
      return (m_curNodeIdx + (curByte / float(totalBytes))) / float(getNodeCount());
    else
      return m_curNodeIdx / float(getNodeCount());
  }

  IPartition(const DiscBase& parent, PartitionKind kind, bool isWii, uint64_t offset)
  : m_parent(parent), m_kind(kind), m_offset(offset), m_isWii(isWii) {}
  virtual uint64_t normalizeOffset(uint64_t anOffset) const { return anOffset; }
  PartitionKind getKind() const { return m_kind; }
  bool isWii() const { return m_isWii; }
  uint64_t getDiscOffset() const { return m_offset; }
  virtual std::unique_ptr<IPartReadStream> beginReadStream(uint64_t offset = 0) const = 0;
  std::unique_ptr<IPartReadStream> beginDOLReadStream(uint64_t offset = 0) const {
    return beginReadStream(m_dolOff + offset);
  }
  std::unique_ptr<IPartReadStream> beginFSTReadStream(uint64_t offset = 0) const {
    return beginReadStream(m_fstOff + offset);
  }
  std::unique_ptr<IPartReadStream> beginApploaderReadStream(uint64_t offset = 0) const {
    return beginReadStream(0x2440 + offset);
  }
  const Node& getFSTRoot() const { return m_nodes[0]; }
  Node& getFSTRoot() { return m_nodes[0]; }
  bool extractToDirectory(SystemStringView path, const ExtractionContext& ctx);

  uint64_t getDOLSize() const { return m_dolSz; }
  std::unique_ptr<uint8_t[]> getDOLBuf() const {
    std::unique_ptr<uint8_t[]> buf(new uint8_t[m_dolSz]);
    beginDOLReadStream()->read(buf.get(), m_dolSz);
    return buf;
  }

  uint64_t getFSTSize() const { return m_fstSz; }
  std::unique_ptr<uint8_t[]> getFSTBuf() const {
    std::unique_ptr<uint8_t[]> buf(new uint8_t[m_fstSz]);
    beginFSTReadStream()->read(buf.get(), m_fstSz);
    return buf;
  }

  uint64_t getApploaderSize() const { return m_apploaderSz; }
  std::unique_ptr<uint8_t[]> getApploaderBuf() const {
    std::unique_ptr<uint8_t[]> buf(new uint8_t[m_apploaderSz]);
    beginApploaderReadStream()->read(buf.get(), m_apploaderSz);
    return buf;
  }

  size_t getNodeCount() const { return m_nodes.size(); }
  const Header& getHeader() const { return m_header; }
  const BI2Header& getBI2() const { return m_bi2Header; }
  virtual bool extractCryptoFiles(SystemStringView path, const ExtractionContext& ctx) const { return true; }
  bool extractSysFiles(SystemStringView path, const ExtractionContext& ctx) const;
};

class DiscBase {
public:
  virtual ~DiscBase() = default;

protected:
  std::unique_ptr<IDiscIO> m_discIO;
  Header m_header;
  std::vector<std::unique_ptr<IPartition>> m_partitions;

public:
  DiscBase(std::unique_ptr<IDiscIO>&& dio, bool& err) : m_discIO(std::move(dio)), m_header(*m_discIO, err) {}

  const Header& getHeader() const { return m_header; }
  const IDiscIO& getDiscIO() const { return *m_discIO; }
  size_t getPartitionNodeCount(size_t partition = 0) const {
    if (partition >= m_partitions.size()) {
      return -1;
    }
    return m_partitions[partition]->getNodeCount();
  }

  IPartition* getDataPartition() {
    for (const std::unique_ptr<IPartition>& part : m_partitions)
      if (part->getKind() == PartitionKind::Data)
        return part.get();
    return nullptr;
  }

  IPartition* getUpdatePartition() {
    for (const std::unique_ptr<IPartition>& part : m_partitions)
      if (part->getKind() == PartitionKind::Update)
        return part.get();
    return nullptr;
  }

  void extractToDirectory(SystemStringView path, const ExtractionContext& ctx) {
    for (std::unique_ptr<IPartition>& part : m_partitions)
      part->extractToDirectory(path, ctx);
  }

  virtual bool extractDiscHeaderFiles(SystemStringView path, const ExtractionContext& ctx) const = 0;
};

class DiscBuilderBase {
  friend class DiscMergerWii;

public:
  class PartitionBuilderBase {
  public:
    virtual ~PartitionBuilderBase() = default;

  protected:
    std::unordered_map<SystemString, std::pair<uint64_t, uint64_t>> m_fileOffsetsSizes;
    std::vector<FSTNode> m_buildNodes;
    std::vector<std::string> m_buildNames;
    size_t m_buildNameOff = 0;
    virtual uint64_t userAllocate(uint64_t reqSz, IPartWriteStream& ws) = 0;
    virtual uint32_t packOffset(uint64_t offset) const = 0;

    void recursiveBuildNodesPre(SystemStringView dirIn);
    bool recursiveBuildNodes(IPartWriteStream& ws, bool system, SystemStringView dirIn);

    bool recursiveBuildFST(SystemStringView dirIn, std::function<void(void)> incParents, size_t parentDirIdx);

    void recursiveMergeNodesPre(const Node* nodeIn, SystemStringView dirIn);
    bool recursiveMergeNodes(IPartWriteStream& ws, bool system, const Node* nodeIn, SystemStringView dirIn,
                             SystemStringView keyPath);
    bool recursiveMergeFST(const Node* nodeIn, SystemStringView dirIn, std::function<void(void)> incParents,
                           SystemStringView keyPath);

    static bool RecursiveCalculateTotalSize(uint64_t& totalSz, const Node* nodeIn, SystemStringView dirIn);

    void addBuildName(SystemStringView str) {
      SystemUTF8Conv utf8View(str);
      m_buildNames.emplace_back(utf8View.utf8_str());
      m_buildNameOff += str.size() + 1;
    }

    DiscBuilderBase& m_parent;
    PartitionKind m_kind;
    uint64_t m_dolOffset = 0;
    uint64_t m_dolSize = 0;
    bool m_isWii;

  public:
    PartitionBuilderBase(DiscBuilderBase& parent, PartitionKind kind, bool isWii)
    : m_parent(parent), m_kind(kind), m_isWii(isWii) {}
    virtual std::unique_ptr<IPartWriteStream> beginWriteStream(uint64_t offset) = 0;
    bool buildFromDirectory(IPartWriteStream& ws, SystemStringView dirIn);
    static std::optional<uint64_t> CalculateTotalSizeBuild(SystemStringView dirIn, PartitionKind kind, bool isWii);
    bool mergeFromDirectory(IPartWriteStream& ws, const IPartition* partIn, SystemStringView dirIn);
    static std::optional<uint64_t> CalculateTotalSizeMerge(const IPartition* partIn, SystemStringView dirIn);
  };

protected:
  SystemString m_outPath;
  std::unique_ptr<IFileIO> m_fileIO;
  std::vector<std::unique_ptr<PartitionBuilderBase>> m_partitions;
  int64_t m_discCapacity;

public:
  FProgress m_progressCB;
  size_t m_progressIdx = 0;
  size_t m_progressTotal = 0;
  float getProgressFactor() const {
    return m_progressTotal ? std::min(1.f, m_progressIdx / float(m_progressTotal)) : 0.f;
  }
  float getProgressFactorMidFile(size_t curByte, size_t totalBytes) const {
    if (!m_progressTotal)
      return 0.f;

    if (totalBytes)
      return (m_progressIdx + (curByte / float(totalBytes))) / float(m_progressTotal);
    else
      return m_progressIdx / float(m_progressTotal);
  }

  virtual ~DiscBuilderBase() = default;
  DiscBuilderBase(SystemStringView outPath, int64_t discCapacity, FProgress progressCB)
  : m_outPath(outPath)
  , m_fileIO(NewFileIO(outPath, discCapacity))
  , m_discCapacity(discCapacity)
  , m_progressCB(std::move(progressCB)) {}
  DiscBuilderBase(DiscBuilderBase&&) = default;
  DiscBuilderBase& operator=(DiscBuilderBase&&) = default;

  IFileIO& getFileIO() { return *m_fileIO; }
};

} // namespace nod
