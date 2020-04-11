#include "nod/DiscGCN.hpp"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "nod/nod.hpp"
#include "nod/Util.hpp"

#include <logvisor/logvisor.hpp>

#define BUFFER_SZ 0x8000

namespace nod {

class PartitionGCN : public IPartition {
public:
  PartitionGCN(const DiscGCN& parent, uint64_t offset, bool& err)
  : IPartition(parent, PartitionKind::Data, false, offset) {
    /* GCN-specific header reads */
    std::unique_ptr<IPartReadStream> s = beginReadStream(0x0);
    if (!s) {
      err = true;
      return;
    }
    m_header.read(*s);
    m_bi2Header.read(*s);
    m_dolOff = m_header.m_dolOff;
    m_fstOff = m_header.m_fstOff;
    m_fstSz = m_header.m_fstSz;
    uint32_t vals[2];
    s->seek(0x2440 + 0x14);
    s->read(vals, 8);
    m_apploaderSz = 32 + SBig(vals[0]) + SBig(vals[1]);

    /* Yay files!! */
    parseFST(*s);

    /* Also make DOL header and size handy */
    s->seek(m_dolOff);
    parseDOL(*s);
  }

  class PartReadStream : public IPartReadStream {
    const PartitionGCN& m_parent;
    std::unique_ptr<IReadStream> m_dio;

    uint64_t m_offset;
    size_t m_curBlock = SIZE_MAX;
    uint8_t m_buf[BUFFER_SZ];

  public:
    PartReadStream(const PartitionGCN& parent, uint64_t offset, bool& err) : m_parent(parent), m_offset(offset) {
      size_t block = m_offset / BUFFER_SZ;
      m_dio = m_parent.m_parent.getDiscIO().beginReadStream(block * BUFFER_SZ);
      if (!m_dio) {
        err = true;
        return;
      }
      m_dio->read(m_buf, BUFFER_SZ);
      m_curBlock = block;
    }
    void seek(int64_t offset, int whence) override {
      if (whence == SEEK_SET)
        m_offset = offset;
      else if (whence == SEEK_CUR)
        m_offset += offset;
      else
        return;
      size_t block = m_offset / BUFFER_SZ;
      if (block != m_curBlock) {
        m_dio->seek(block * BUFFER_SZ);
        m_dio->read(m_buf, BUFFER_SZ);
        m_curBlock = block;
      }
    }
    uint64_t position() const override { return m_offset; }
    uint64_t read(void* buf, uint64_t length) override {
      size_t block = m_offset / BUFFER_SZ;
      size_t cacheOffset = m_offset % BUFFER_SZ;
      uint64_t cacheSize;
      uint64_t rem = length;
      uint8_t* dst = (uint8_t*)buf;

      while (rem) {
        if (block != m_curBlock) {
          m_dio->read(m_buf, BUFFER_SZ);
          m_curBlock = block;
        }

        cacheSize = rem;
        if (cacheSize + cacheOffset > BUFFER_SZ)
          cacheSize = BUFFER_SZ - cacheOffset;

        memmove(dst, m_buf + cacheOffset, cacheSize);
        dst += cacheSize;
        rem -= cacheSize;
        cacheOffset = 0;
        ++block;
      }

      m_offset += length;
      return dst - (uint8_t*)buf;
    }
  };

  std::unique_ptr<IPartReadStream> beginReadStream(uint64_t offset) const override {
    bool err = false;
    auto ret = std::make_unique<PartReadStream>(*this, offset, err);

    if (err) {
      return nullptr;
    }

    return ret;
  }
};

DiscGCN::DiscGCN(std::unique_ptr<IDiscIO>&& dio, bool& err) : DiscBase(std::move(dio), err) {
  if (err)
    return;

  /* One lone partition for GCN */
  m_partitions.emplace_back(std::make_unique<PartitionGCN>(*this, 0, err));
}

DiscBuilderGCN DiscGCN::makeMergeBuilder(SystemStringView outPath, FProgress progressCB) {
  return DiscBuilderGCN(outPath, progressCB);
}

bool DiscGCN::extractDiscHeaderFiles(SystemStringView path, const ExtractionContext& ctx) const { return true; }

class PartitionBuilderGCN : public DiscBuilderBase::PartitionBuilderBase {
  uint64_t m_curUser = 0x57058000;

public:
  class PartWriteStream : public IPartWriteStream {
    const PartitionBuilderGCN& m_parent;
    uint64_t m_offset;
    std::unique_ptr<IFileIO::IWriteStream> m_fio;

  public:
    PartWriteStream(const PartitionBuilderGCN& parent, uint64_t offset, bool& err)
    : m_parent(parent), m_offset(offset) {
      m_fio = m_parent.m_parent.getFileIO().beginWriteStream(offset);
      if (!m_fio)
        err = true;
    }
    void close() override { m_fio.reset(); }
    uint64_t position() const override { return m_offset; }
    uint64_t write(const void* buf, uint64_t length) override {
      uint64_t len = m_fio->write(buf, length);
      m_offset += len;
      return len;
    }
    void seek(size_t off) {
      m_offset = off;
      m_fio = m_parent.m_parent.getFileIO().beginWriteStream(off);
    }
  };

  PartitionBuilderGCN(DiscBuilderBase& parent)
  : DiscBuilderBase::PartitionBuilderBase(parent, PartitionKind::Data, false) {}

  uint64_t userAllocate(uint64_t reqSz, IPartWriteStream& ws) override {
    m_curUser -= reqSz;
    m_curUser &= 0xfffffffffffffff0;
    if (m_curUser < 0x30000) {
      LogModule.report(logvisor::Error, FMT_STRING("user area low mark reached"));
      return -1;
    }
    static_cast<PartWriteStream&>(ws).seek(m_curUser);
    return m_curUser;
  }

  uint32_t packOffset(uint64_t offset) const override { return offset; }

  std::unique_ptr<IPartWriteStream> beginWriteStream(uint64_t offset) override {
    bool err = false;
    auto ret = std::make_unique<PartWriteStream>(*this, offset, err);

    if (err) {
      return nullptr;
    }

    return ret;
  }

  bool
  _build(const std::function<bool(IPartWriteStream&, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>& headerFunc,
         const std::function<bool(IPartWriteStream&)>& bi2Func,
         const std::function<bool(IPartWriteStream&, size_t&)>& apploaderFunc) {
    std::unique_ptr<IPartWriteStream> ws = beginWriteStream(0x2440);
    if (!ws)
      return false;
    size_t xferSz = 0;
    if (!apploaderFunc(*ws, xferSz))
      return false;

    size_t fstOff = ROUND_UP_32(xferSz);
    size_t fstSz = sizeof(FSTNode) * m_buildNodes.size();
    for (size_t i = 0; i < fstOff - xferSz; ++i)
      ws->write("\xff", 1);
    fstOff += 0x2440;
    ws->write(m_buildNodes.data(), fstSz);
    for (const std::string& str : m_buildNames)
      ws->write(str.data(), str.size() + 1);
    fstSz += m_buildNameOff;
    fstSz = ROUND_UP_32(fstSz);

    if (fstOff + fstSz >= m_curUser) {
      LogModule.report(logvisor::Error, FMT_STRING("FST flows into user area (one or the other is too big)"));
      return false;
    }

    ws = beginWriteStream(0);
    if (!ws)
      return false;
    if (!headerFunc(*ws, m_dolOffset, fstOff, fstSz, m_curUser, 0x57058000 - m_curUser))
      return false;
    if (!bi2Func(*ws))
      return false;

    return true;
  }

  bool buildFromDirectory(SystemStringView dirIn) {
    std::unique_ptr<IPartWriteStream> ws = beginWriteStream(0);
    if (!ws)
      return false;
    bool result = DiscBuilderBase::PartitionBuilderBase::buildFromDirectory(*ws, dirIn);
    if (!result)
      return false;

    SystemString dirStr(dirIn);

    /* Check Apploader */
    SystemString apploaderIn = dirStr + _SYS_STR("/sys/apploader.img");
    Sstat apploaderStat;
    if (Stat(apploaderIn.c_str(), &apploaderStat)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), apploaderIn);
      return false;
    }

    /* Check Boot */
    SystemString bootIn = dirStr + _SYS_STR("/sys/boot.bin");
    Sstat bootStat;
    if (Stat(bootIn.c_str(), &bootStat)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), bootIn);
      return false;
    }

    /* Check BI2 */
    SystemString bi2In = dirStr + _SYS_STR("/sys/bi2.bin");
    Sstat bi2Stat;
    if (Stat(bi2In.c_str(), &bi2Stat)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), bi2In);
      return false;
    }

    return _build(
        [&bootIn](IPartWriteStream& ws, uint32_t dolOff, uint32_t fstOff, uint32_t fstSz, uint32_t userOff,
                  uint32_t userSz) -> bool {
          std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(bootIn.c_str())->beginReadStream();
          if (!rs)
            return false;
          Header header;
          header.read(*rs);
          header.m_dolOff = dolOff;
          header.m_fstOff = fstOff;
          header.m_fstSz = fstSz;
          header.m_fstMaxSz = fstSz;
          header.m_userPosition = userOff;
          header.m_userSz = userSz;
          header.write(ws);
          return true;
        },
        [&bi2In](IPartWriteStream& ws) -> bool {
          std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(bi2In.c_str())->beginReadStream();
          if (!rs)
            return false;
          BI2Header bi2;
          bi2.read(*rs);
          bi2.write(ws);
          return true;
        },
        [this, &apploaderIn](IPartWriteStream& ws, size_t& xferSz) -> bool {
          std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(apploaderIn.c_str())->beginReadStream();
          if (!rs)
            return false;
          char buf[8192];
          while (true) {
            size_t rdSz = rs->read(buf, 8192);
            if (!rdSz)
              break;
            ws.write(buf, rdSz);
            xferSz += rdSz;
            if (0x2440 + xferSz >= m_curUser) {
              LogModule.report(logvisor::Error, FMT_STRING("apploader flows into user area (one or the other is too big)"));
              return false;
            }
            m_parent.m_progressCB(m_parent.getProgressFactor(), apploaderIn, xferSz);
          }
          ++m_parent.m_progressIdx;
          return true;
        });
  }

  bool mergeFromDirectory(const PartitionGCN* partIn, SystemStringView dirIn) {
    std::unique_ptr<IPartWriteStream> ws = beginWriteStream(0);
    if (!ws)
      return false;
    bool result = DiscBuilderBase::PartitionBuilderBase::mergeFromDirectory(*ws, partIn, dirIn);
    if (!result)
      return false;

    return _build(
        [partIn](IPartWriteStream& ws, uint32_t dolOff, uint32_t fstOff, uint32_t fstSz, uint32_t userOff,
                 uint32_t userSz) -> bool {
          Header header = partIn->getHeader();
          header.m_dolOff = dolOff;
          header.m_fstOff = fstOff;
          header.m_fstSz = fstSz;
          header.m_fstMaxSz = fstSz;
          header.m_userPosition = userOff;
          header.m_userSz = userSz;
          header.write(ws);
          return true;
        },
        [partIn](IPartWriteStream& ws) -> bool {
          partIn->getBI2().write(ws);
          return true;
        },
        [this, partIn](IPartWriteStream& ws, size_t& xferSz) -> bool {
          std::unique_ptr<uint8_t[]> apploaderBuf = partIn->getApploaderBuf();
          size_t apploaderSz = partIn->getApploaderSize();
          SystemString apploaderName(_SYS_STR("<apploader>"));
          ws.write(apploaderBuf.get(), apploaderSz);
          xferSz += apploaderSz;
          if (0x2440 + xferSz >= m_curUser) {
            LogModule.report(logvisor::Error, FMT_STRING("apploader flows into user area (one or the other is too big)"));
            return false;
          }
          m_parent.m_progressCB(m_parent.getProgressFactor(), apploaderName, xferSz);
          ++m_parent.m_progressIdx;
          return true;
        });
  }
};

EBuildResult DiscBuilderGCN::buildFromDirectory(SystemStringView dirIn) {
  if (!m_fileIO->beginWriteStream())
    return EBuildResult::Failed;
  if (!CheckFreeSpace(m_outPath.c_str(), 0x57058000)) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("not enough free disk space for {}")), m_outPath);
    return EBuildResult::DiskFull;
  }
  m_progressCB(getProgressFactor(), _SYS_STR("Preallocating image"), -1);
  ++m_progressIdx;
  {
    auto ws = m_fileIO->beginWriteStream(0);
    if (!ws)
      return EBuildResult::Failed;
    char zeroBytes[1024] = {};
    for (uint64_t i = 0; i < 0x57058000; i += 1024)
      ws->write(zeroBytes, 1024);
  }

  PartitionBuilderGCN& pb = static_cast<PartitionBuilderGCN&>(*m_partitions[0]);
  return pb.buildFromDirectory(dirIn) ? EBuildResult::Success : EBuildResult::Failed;
}

std::optional<uint64_t> DiscBuilderGCN::CalculateTotalSizeRequired(SystemStringView dirIn) {
  std::optional<uint64_t> sz = DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeBuild(dirIn, PartitionKind::Data, false);
  if (!sz)
    return sz;
  *sz += 0x30000;
  if (sz > 0x57058000) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("disc capacity exceeded [{} / {}]")), *sz, 0x57058000);
    return std::nullopt;
  }
  return sz;
}

DiscBuilderGCN::DiscBuilderGCN(SystemStringView outPath, FProgress progressCB)
: DiscBuilderBase(outPath, 0x57058000, progressCB) {
  m_partitions.emplace_back(std::make_unique<PartitionBuilderGCN>(*this));
}

DiscMergerGCN::DiscMergerGCN(SystemStringView outPath, DiscGCN& sourceDisc, FProgress progressCB)
: m_sourceDisc(sourceDisc), m_builder(sourceDisc.makeMergeBuilder(outPath, progressCB)) {}

EBuildResult DiscMergerGCN::mergeFromDirectory(SystemStringView dirIn) {
  if (!m_builder.getFileIO().beginWriteStream())
    return EBuildResult::Failed;
  if (!CheckFreeSpace(m_builder.m_outPath.c_str(), 0x57058000)) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("not enough free disk space for {}")), m_builder.m_outPath);
    return EBuildResult::DiskFull;
  }
  m_builder.m_progressCB(m_builder.getProgressFactor(), _SYS_STR("Preallocating image"), -1);
  ++m_builder.m_progressIdx;
  {
    auto ws = m_builder.m_fileIO->beginWriteStream(0);
    if (!ws)
      return EBuildResult::Failed;
    char zeroBytes[1024] = {};
    for (uint64_t i = 0; i < 0x57058000; i += 1024)
      ws->write(zeroBytes, 1024);
  }

  PartitionBuilderGCN& pb = static_cast<PartitionBuilderGCN&>(*m_builder.m_partitions[0]);
  return pb.mergeFromDirectory(static_cast<PartitionGCN*>(m_sourceDisc.getDataPartition()), dirIn)
             ? EBuildResult::Success
             : EBuildResult::Failed;
}

std::optional<uint64_t> DiscMergerGCN::CalculateTotalSizeRequired(DiscGCN& sourceDisc, SystemStringView dirIn) {
  std::optional<uint64_t> sz = DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeMerge(sourceDisc.getDataPartition(), dirIn);
  if (!sz)
    return std::nullopt;
  *sz += 0x30000;
  if (sz > 0x57058000) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("disc capacity exceeded [{} / {}]")), *sz, 0x57058000);
    return std::nullopt;
  }
  return sz;
}

} // namespace nod
