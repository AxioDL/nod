#include "nod/IDiscIO.hpp"
#include "nod/IFileIO.hpp"
#include "nod/Util.hpp"
#include "nod/aes.hpp"

#include <logvisor/logvisor.hpp>

namespace nod {

/*
 * NFS is the image format used to distribute Wii VC games for the Wii U.
 * It is an LBA format similar to WBFS but adds its own encryption layer.
 * It logically stores a standard Wii disc image with partitions.
 */

class DiscIONFS : public IDiscIO {
  std::vector<std::unique_ptr<IFileIO>> files;

  struct NFSHead {
    uint32_t magic; // EGGS
    uint32_t version;
    uint32_t unknown[2]; // Signature, UUID?
    uint32_t lbaRangeCount;
    struct {
      uint32_t startBlock;
      uint32_t numBlocks;
    } lbaRanges[61];
    uint32_t endMagic; // SGGE
  } nfsHead;

  uint8_t key[16];

  uint32_t calculateNumFiles() const {
    uint32_t totalBlockCount = 0;
    for (uint32_t i = 0; i < nfsHead.lbaRangeCount; ++i)
      totalBlockCount += nfsHead.lbaRanges[i].numBlocks;
    return (uint64_t(totalBlockCount) * uint64_t(0x8000) +
            (uint64_t(0x200) + uint64_t(0xF9FFFFF))) / uint64_t(0xFA00000);
  }

  struct FBO {
    uint32_t file, block, lblock, offset;
  };

  FBO logicalToFBO(uint64_t offset) const {
    auto blockAndRemBytes = nod::div(offset, uint64_t(0x8000)); /* 32768 bytes per block */
    uint32_t block = UINT32_MAX;
    for (uint32_t i = 0, physicalBlock = 0; i < nfsHead.lbaRangeCount; ++i) {
      const auto& range = nfsHead.lbaRanges[i];
      if (blockAndRemBytes.quot >= range.startBlock && blockAndRemBytes.quot - range.startBlock < range.numBlocks) {
        block = physicalBlock + (blockAndRemBytes.quot - range.startBlock);
        break;
      }
      physicalBlock += range.numBlocks;
    }
    /* This offset has no physical mapping, read zeroes */
    if (block == UINT32_MAX)
      return {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    auto fileAndRemBlocks = nod::div(block, uint32_t(8000)); /* 8000 blocks per file */
    return {uint32_t(fileAndRemBlocks.quot), uint32_t(fileAndRemBlocks.rem),
            uint32_t(blockAndRemBytes.quot), uint32_t(blockAndRemBytes.rem)};
  }

public:
  DiscIONFS(SystemStringView fpin, bool& err) {
    /* Validate file path format */
    using SignedSize = std::make_signed<SystemString::size_type>::type;
    const auto dotPos = SignedSize(fpin.rfind(_SYS_STR('.')));
    const auto slashPos = SignedSize(fpin.find_last_of(_SYS_STR("/\\")));
    if (fpin.size() <= 4 || dotPos == -1 || dotPos <= slashPos ||
        fpin.compare(slashPos + 1, 4, _SYS_STR("hif_")) ||
        fpin.compare(dotPos, fpin.size() - dotPos, _SYS_STR(".nfs"))) {
      LogModule.report(logvisor::Error,
        FMT_STRING(_SYS_STR("'{}' must begin with 'hif_' and end with '.nfs' to be accepted as an NFS image")), fpin);
      err = true;
      return;
    }

    /* Load key file */
    const SystemString dir(fpin.begin(), fpin.begin() + slashPos + 1);
    auto keyFile = NewFileIO(dir + _SYS_STR("../code/htk.bin"))->beginReadStream();
    if (!keyFile)
      keyFile = NewFileIO(dir + _SYS_STR("htk.bin"))->beginReadStream();
    if (!keyFile) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("Unable to open '{}../code/htk.bin' or '{}htk.bin'")), dir, dir);
      err = true;
      return;
    }
    if (keyFile->read(key, 16) != 16) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("Unable to read from '{}../code/htk.bin' or '{}htk.bin'")), dir, dir);
      err = true;
      return;
    }

    /* Load header from first file */
    const SystemString firstPath = fmt::format(FMT_STRING(_SYS_STR("{}hif_{:06}.nfs")), dir, 0);
    files.push_back(NewFileIO(firstPath));
    auto rs = files.back()->beginReadStream();
    if (!rs) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("'{}' does not exist")), firstPath);
      err = true;
      return;
    }
    if (rs->read(&nfsHead, 0x200) != 0x200) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("Unable to read header from '{}'")), firstPath);
      err = true;
      return;
    }
    if (std::memcmp(&nfsHead.magic, "EGGS", 4)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("Invalid magic in '{}'")), firstPath);
      err = true;
      return;
    }
    nfsHead.lbaRangeCount = SBig(nfsHead.lbaRangeCount);
    for (uint32_t i = 0; i < nfsHead.lbaRangeCount; ++i) {
      auto& range = nfsHead.lbaRanges[i];
      range.startBlock = SBig(range.startBlock);
      range.numBlocks = SBig(range.numBlocks);
    }

    /* Ensure remaining files exist */
    const uint32_t numFiles = calculateNumFiles();
    files.reserve(numFiles);
    for (uint32_t i = 1; i < numFiles; ++i) {
      SystemString path = fmt::format(FMT_STRING(_SYS_STR("{}hif_{:06}.nfs")), dir, i);
      files.push_back(NewFileIO(path));
      if (!files.back()->exists()) {
        LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("'{}' does not exist")), path);
        err = true;
        return;
      }
    }
  }

  class ReadStream : public IReadStream {
    friend class DiscIONFS;
    const DiscIONFS& m_parent;
    std::unique_ptr<IReadStream> m_rs;
    std::unique_ptr<IAES> m_aes;

    /* Physical address - all UINT32_MAX indicates logical zero block */
    DiscIONFS::FBO m_physAddr;

    /* Logical address */
    uint64_t m_offset;

    /* Active file stream and its offset as set in the system.
     * Block is typically one ahead of the presently decrypted block. */
    uint32_t m_curFile = UINT32_MAX;
    uint32_t m_curBlock = UINT32_MAX;

    ReadStream(const DiscIONFS& parent, uint64_t offset, bool& err)
    : m_parent(parent), m_aes(NewAES()),
      m_physAddr({UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}), m_offset(offset) {
      m_aes->setKey(m_parent.key);
      setLogicalAddr(offset);
    }

    uint8_t m_encBuf[0x8000] = {};
    uint8_t m_decBuf[0x8000] = {};

    void setCurFile(uint32_t curFile) {
      if (curFile >= m_parent.files.size()) {
        LogModule.report(logvisor::Error, FMT_STRING("Out of bounds NFS file access"));
        return;
      }
      m_curFile = curFile;
      m_curBlock = UINT32_MAX;
      m_rs = m_parent.files[m_curFile]->beginReadStream();
    }

    void setCurBlock(uint32_t curBlock) {
      m_curBlock = curBlock;
      m_rs->seek(m_curBlock * 0x8000 + 0x200);
    }

    void setPhysicalAddr(DiscIONFS::FBO physAddr) {
      /* If we're just changing the offset, nothing else needs to be done */
      if (m_physAddr.file == physAddr.file && m_physAddr.block == physAddr.block) {
        m_physAddr.offset = physAddr.offset;
        return;
      }
      m_physAddr = physAddr;

      /* Set logical zero block */
      if (m_physAddr.file == UINT32_MAX) {
        memset(m_decBuf, 0, 0x8000);
        return;
      }

      /* Make necessary file and block current with system */
      if (m_physAddr.file != m_curFile)
        setCurFile(m_physAddr.file);
      if (m_physAddr.block != m_curBlock)
        setCurBlock(m_physAddr.block);

      /* Read block, handling 0x200 overlap case */
      if (m_physAddr.block == 7999) {
        m_rs->read(m_encBuf, 0x7E00);
        setCurFile(m_curFile + 1);
        m_rs->read(m_encBuf + 0x7E00, 0x200);
        m_curBlock = 0;
      } else {
        m_rs->read(m_encBuf, 0x8000);
        ++m_curBlock;
      }

      /* Decrypt */
      const uint32_t ivBuf[] = {0, 0, 0, SBig(m_physAddr.lblock)};
      m_aes->decrypt((const uint8_t*)ivBuf, m_encBuf, m_decBuf, 0x8000);
    }

    void setLogicalAddr(uint64_t addr) { setPhysicalAddr(m_parent.logicalToFBO(m_offset)); }

  public:
    uint64_t read(void* buf, uint64_t length) override {
      uint64_t rem = length;
      uint8_t* dst = (uint8_t*)buf;

      /* Perform reads on block boundaries */
      while (rem) {
        uint64_t readSize = rem;
        uint32_t blockOffset = (m_physAddr.offset == UINT32_MAX) ? 0 : m_physAddr.offset;
        if (readSize + blockOffset > 0x8000)
          readSize = 0x8000 - blockOffset;

        memmove(dst, m_decBuf + blockOffset, readSize);
        dst += readSize;
        rem -= readSize;
        m_offset += readSize;
        setLogicalAddr(m_offset);
      }

      return dst - (uint8_t*)buf;
    }
    uint64_t position() const override { return m_offset; }
    void seek(int64_t offset, int whence) override {
      if (whence == SEEK_SET)
        m_offset = offset;
      else if (whence == SEEK_CUR)
        m_offset += offset;
      else
        return;
      setLogicalAddr(m_offset);
    }
  };

  std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const override {
    bool err = false;
    auto ret = std::unique_ptr<IReadStream>(new ReadStream(*this, offset, err));

    if (err)
      return {};

    return ret;
  }

  std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const override { return {}; }

  bool hasWiiCrypto() const override { return false; }
};

std::unique_ptr<IDiscIO> NewDiscIONFS(SystemStringView path) {
  bool err = false;
  auto ret = std::make_unique<DiscIONFS>(path, err);
  if (err)
    return {};
  return ret;
}

}