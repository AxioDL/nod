#include "nod/DiscWii.hpp"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "nod/aes.hpp"
#include "nod/nod.hpp"
#include "nod/sha1.h"
#include "nod/Util.hpp"

namespace nod {

static const uint8_t COMMON_KEYS[2][16] = {
    /* Normal */
    {0xeb, 0xe4, 0x2a, 0x22, 0x5e, 0x85, 0x93, 0xe4, 0x48, 0xd9, 0xc5, 0x45, 0x73, 0x81, 0xaa, 0xf7},
    /* Korean */
    {0x63, 0xb8, 0x2b, 0xb4, 0xf4, 0x61, 0x4e, 0x2e, 0x13, 0xf2, 0xfe, 0xfb, 0xba, 0x4c, 0x9b, 0x7e}};

class PartitionWii : public IPartition {
  enum class SigType : uint32_t { RSA_4096 = 0x00010000, RSA_2048 = 0x00010001, ELIPTICAL_CURVE = 0x00010002 };

  enum class KeyType : uint32_t { RSA_4096 = 0x00000000, RSA_2048 = 0x00000001 };

  struct Ticket {
    uint32_t sigType;
    char sig[256];
    char padding[60];
    char sigIssuer[64];
    char ecdh[60];
    char padding1[3];
    unsigned char encKey[16];
    char padding2;
    char ticketId[8];
    char consoleId[4];
    char titleId[8];
    char padding3[2];
    uint16_t ticketVersion;
    uint32_t permittedTitlesMask;
    uint32_t permitMask;
    char titleExportAllowed;
    char commonKeyIdx;
    char padding4[48];
    char contentAccessPermissions[64];
    char padding5[2];
    struct TimeLimit {
      uint32_t enableTimeLimit;
      uint32_t timeLimit;
    } timeLimits[8];

    void read(IReadStream& s) {
      s.read(this, 676);
      sigType = SBig(sigType);
      ticketVersion = SBig(ticketVersion);
      permittedTitlesMask = SBig(permittedTitlesMask);
      permitMask = SBig(permitMask);
      for (size_t t = 0; t < 8; ++t) {
        timeLimits[t].enableTimeLimit = SBig(timeLimits[t].enableTimeLimit);
        timeLimits[t].timeLimit = SBig(timeLimits[t].timeLimit);
      }
    }

    void write(IWriteStream& s) const {
      Ticket tik = *this;
      tik.sigType = SBig(tik.sigType);
      tik.ticketVersion = SBig(tik.ticketVersion);
      tik.permittedTitlesMask = SBig(tik.permittedTitlesMask);
      tik.permitMask = SBig(tik.permitMask);
      for (size_t t = 0; t < 8; ++t) {
        tik.timeLimits[t].enableTimeLimit = SBig(tik.timeLimits[t].enableTimeLimit);
        tik.timeLimits[t].timeLimit = SBig(tik.timeLimits[t].timeLimit);
      }
      s.write(&tik, 676);
    }
  } m_ticket;

  struct TMD {
    SigType sigType;
    char sig[256];
    char padding[60];
    char sigIssuer[64];
    char version;
    char caCrlVersion;
    char signerCrlVersion;
    char padding1;
    uint32_t iosIdMajor;
    uint32_t iosIdMinor;
    uint32_t titleIdMajor;
    char titleIdMinor[4];
    uint32_t titleType;
    uint16_t groupId;
    char padding2[62];
    uint32_t accessFlags;
    uint16_t titleVersion;
    uint16_t numContents;
    uint16_t bootIdx;
    uint16_t padding3;

    struct Content {
      uint32_t id;
      uint16_t index;
      uint16_t type;
      uint64_t size;
      char hash[20];

      void read(IReadStream& s) {
        s.read(this, 36);
        id = SBig(id);
        index = SBig(index);
        type = SBig(type);
        size = SBig(size);
      }

      void write(IWriteStream& s) const {
        Content c = *this;
        c.id = SBig(c.id);
        c.index = SBig(c.index);
        c.type = SBig(c.type);
        c.size = SBig(c.size);
        s.write(&c, 36);
      }
    };
    std::vector<Content> contents;

    void read(IReadStream& s) {
      s.read(this, 484);
      sigType = SigType(SBig(uint32_t(sigType)));
      iosIdMajor = SBig(iosIdMajor);
      iosIdMinor = SBig(iosIdMinor);
      titleIdMajor = SBig(titleIdMajor);
      titleType = SBig(titleType);
      groupId = SBig(groupId);
      accessFlags = SBig(accessFlags);
      titleVersion = SBig(titleVersion);
      numContents = SBig(numContents);
      bootIdx = SBig(bootIdx);
      contents.clear();
      contents.reserve(numContents);
      for (uint16_t c = 0; c < numContents; ++c) {
        contents.emplace_back();
        contents.back().read(s);
      }
    }

    void write(IWriteStream& s) const {
      TMD tmd = *this;
      tmd.sigType = SigType(SBig(uint32_t(tmd.sigType)));
      tmd.iosIdMajor = SBig(tmd.iosIdMajor);
      tmd.iosIdMinor = SBig(tmd.iosIdMinor);
      tmd.titleIdMajor = SBig(tmd.titleIdMajor);
      tmd.titleType = SBig(tmd.titleType);
      tmd.groupId = SBig(tmd.groupId);
      tmd.accessFlags = SBig(tmd.accessFlags);
      tmd.titleVersion = SBig(tmd.titleVersion);
      tmd.numContents = SBig(tmd.numContents);
      tmd.bootIdx = SBig(tmd.bootIdx);
      s.write(&tmd, 484);
      for (uint16_t c = 0; c < numContents; ++c)
        tmd.contents.back().write(s);
    }
  } m_tmd;

  struct Certificate {
    SigType sigType;
    char sig[512];
    char issuer[64];
    KeyType keyType;
    char subject[64];
    char key[512];
    uint32_t modulus;
    uint32_t pubExp;

    void read(IReadStream& s) {
      s.read(&sigType, 4);
      sigType = SigType(SBig(uint32_t(sigType)));
      if (sigType == SigType::RSA_4096)
        s.read(sig, 512);
      else if (sigType == SigType::RSA_2048)
        s.read(sig, 256);
      else if (sigType == SigType::ELIPTICAL_CURVE)
        s.read(sig, 64);
      s.seek(60, SEEK_CUR);

      s.read(issuer, 64);
      s.read(&keyType, 4);
      s.read(subject, 64);
      keyType = KeyType(SBig(uint32_t(keyType)));
      if (keyType == KeyType::RSA_4096)
        s.read(key, 512);
      else if (keyType == KeyType::RSA_2048)
        s.read(key, 256);

      s.read(&modulus, 8);
      modulus = SBig(modulus);
      pubExp = SBig(pubExp);

      s.seek(52, SEEK_CUR);
    }

    void write(IWriteStream& s) const {
      Certificate c = *this;
      c.sigType = SigType(SBig(uint32_t(c.sigType)));
      s.write(&c.sigType, 4);
      if (sigType == SigType::RSA_4096)
        s.write(sig, 512);
      else if (sigType == SigType::RSA_2048)
        s.write(sig, 256);
      else if (sigType == SigType::ELIPTICAL_CURVE)
        s.write(sig, 64);

      uint32_t zero = 0;
      for (int i = 0; i < 15; ++i)
        s.write(&zero, 4);

      s.write(issuer, 64);
      c.keyType = KeyType(SBig(uint32_t(c.keyType)));
      s.write(&c.keyType, 4);
      s.write(subject, 64);
      if (keyType == KeyType::RSA_4096)
        s.write(key, 512);
      else if (keyType == KeyType::RSA_2048)
        s.write(key, 256);

      c.modulus = SBig(c.modulus);
      c.pubExp = SBig(c.pubExp);
      s.write(&c.modulus, 8);

      for (int i = 0; i < 13; ++i)
        s.write(&zero, 4);
    }
  };
  Certificate m_caCert;
  Certificate m_tmdCert;
  Certificate m_ticketCert;

  std::unique_ptr<uint8_t[]> m_h3Data;

  uint64_t m_dataOff;
  uint8_t m_decKey[16];

public:
  PartitionWii(const DiscWii& parent, PartitionKind kind, uint64_t offset, bool& err)
  : IPartition(parent, kind, true, offset) {
    std::unique_ptr<IReadStream> s = parent.getDiscIO().beginReadStream(offset);
    if (!s) {
      err = true;
      return;
    }

    m_ticket.read(*s);

    uint32_t tmdSize;
    s->read(&tmdSize, 4);
    tmdSize = SBig(tmdSize);
    uint32_t tmdOff;
    s->read(&tmdOff, 4);
    tmdOff = SBig(tmdOff) << 2;

    uint32_t certChainSize;
    s->read(&certChainSize, 4);
    certChainSize = SBig(certChainSize);
    uint32_t certChainOff;
    s->read(&certChainOff, 4);
    certChainOff = SBig(certChainOff) << 2;

    uint32_t globalHashTableOff;
    s->read(&globalHashTableOff, 4);
    globalHashTableOff = SBig(globalHashTableOff) << 2;

    uint32_t dataOff;
    s->read(&dataOff, 4);
    dataOff = SBig(dataOff) << 2;
    m_dataOff = offset + dataOff;
    uint32_t dataSize;
    s->read(&dataSize, 4);
    dataSize = SBig(dataSize) << 2;

    s->seek(offset + tmdOff);
    m_tmd.read(*s);

    s->seek(offset + certChainOff);
    m_caCert.read(*s);
    m_tmdCert.read(*s);
    m_ticketCert.read(*s);

    s->seek(globalHashTableOff);
    m_h3Data.reset(new uint8_t[0x18000]);
    s->read(m_h3Data.get(), 0x18000);

    /* Decrypt title key */
    std::unique_ptr<IAES> aes = NewAES();
    uint8_t iv[16] = {};
    memmove(iv, m_ticket.titleId, 8);
    aes->setKey(COMMON_KEYS[(int)m_ticket.commonKeyIdx]);
    aes->decrypt(iv, m_ticket.encKey, m_decKey, 16);

    /* Wii-specific header reads (now using title key to decrypt) */
    std::unique_ptr<IPartReadStream> ds = beginReadStream(0x0);
    if (!ds) {
      err = true;
      return;
    }
    m_header.read(*ds);
    m_bi2Header.read(*ds);
    m_dolOff = m_header.m_dolOff << 2;
    m_fstOff = m_header.m_fstOff << 2;
    m_fstSz = m_header.m_fstSz << 2;
    ds->seek(0x2440 + 0x14);
    uint32_t vals[2];
    ds->read(vals, 8);
    m_apploaderSz = 32 + SBig(vals[0]) + SBig(vals[1]);

    /* Yay files!! */
    parseFST(*ds);

    /* Also make DOL header and size handy */
    ds->seek(m_dolOff);
    parseDOL(*ds);
  }

  class PartReadStream : public IPartReadStream {
    std::unique_ptr<IAES> m_aes;
    const PartitionWii& m_parent;
    uint64_t m_baseOffset;
    uint64_t m_offset;
    std::unique_ptr<IReadStream> m_dio;

    size_t m_curBlock = SIZE_MAX;
    uint8_t m_encBuf[0x8000];
    uint8_t m_decBuf[0x7c00];

    void decryptBlock() {
      if (m_aes) {
        m_dio->read(m_encBuf, 0x8000);
        m_aes->decrypt(&m_encBuf[0x3d0], &m_encBuf[0x400], m_decBuf, 0x7c00);
      } else {
        m_dio->seek(0x400, SEEK_CUR);
        m_dio->read(m_decBuf, 0x7c00);
      }
    }

  public:
    PartReadStream(const PartitionWii& parent, uint64_t baseOffset, uint64_t offset, bool& err)
    : m_parent(parent), m_baseOffset(baseOffset), m_offset(offset) {
      if (m_parent.m_parent.getDiscIO().hasWiiCrypto()) {
        m_aes = NewAES();
        m_aes->setKey(parent.m_decKey);
      }
      size_t block = m_offset / 0x7c00;
      m_dio = m_parent.m_parent.getDiscIO().beginReadStream(m_baseOffset + block * 0x8000);
      if (!m_dio) {
        err = true;
        return;
      }
      decryptBlock();
      m_curBlock = block;
    }
    void seek(int64_t offset, int whence) override {
      if (whence == SEEK_SET)
        m_offset = offset;
      else if (whence == SEEK_CUR)
        m_offset += offset;
      else
        return;
      size_t block = m_offset / 0x7c00;
      if (block != m_curBlock) {
        m_dio->seek(m_baseOffset + block * 0x8000);
        decryptBlock();
        m_curBlock = block;
      }
    }
    uint64_t position() const override { return m_offset; }
    uint64_t read(void* buf, uint64_t length) override {
      auto blockAndRemOff = nod::div(m_offset, uint64_t(0x7c00));
      uint64_t rem = length;
      uint8_t* dst = (uint8_t*)buf;

      while (rem) {
        if (blockAndRemOff.quot != m_curBlock) {
          decryptBlock();
          m_curBlock = blockAndRemOff.quot;
        }

        uint64_t cacheSize = rem;
        if (cacheSize + blockAndRemOff.rem > 0x7c00)
          cacheSize = 0x7c00 - blockAndRemOff.rem;

        memmove(dst, m_decBuf + blockAndRemOff.rem, cacheSize);
        dst += cacheSize;
        rem -= cacheSize;
        blockAndRemOff.rem = 0;
        ++blockAndRemOff.quot;
      }

      m_offset += length;
      return dst - (uint8_t*)buf;
    }
  };

  std::unique_ptr<IPartReadStream> beginReadStream(uint64_t offset) const override {
    bool err = false;
    auto ret = std::make_unique<PartReadStream>(*this, m_dataOff, offset, err);

    if (err) {
      return nullptr;
    }

    return ret;
  }

  uint64_t normalizeOffset(uint64_t anOffset) const override { return anOffset << 2; }

  std::unique_ptr<uint8_t[]> readPartitionHeaderBuf(size_t& szOut) const {
    {
      std::unique_ptr<IReadStream> rs = m_parent.getDiscIO().beginReadStream(m_offset + 0x2B4);
      if (!rs) {
        return nullptr;
      }

      uint32_t h3;
      if (rs->read(&h3, 4) != 4) {
        LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to read H3 offset apploader")));
        return nullptr;
      }
      h3 = SBig(h3);
      szOut = uint64_t(h3) << 2;
    }

    std::unique_ptr<IReadStream> rs = m_parent.getDiscIO().beginReadStream(m_offset);
    if (!rs) {
      return nullptr;
    }

    std::unique_ptr<uint8_t[]> buf(new uint8_t[szOut]);
    rs->read(buf.get(), szOut);

    return buf;
  }

  bool extractCryptoFiles(SystemStringView basePath, const ExtractionContext& ctx) const override {
    Sstat theStat;
    SystemString basePathStr(basePath);

    /* Extract Ticket */
    SystemString ticketPath = basePathStr + _SYS_STR("/ticket.bin");
    if (ctx.force || Stat(ticketPath.c_str(), &theStat)) {
      if (ctx.progressCB)
        ctx.progressCB("ticket.bin", 0.f);
      auto ws = NewFileIO(ticketPath)->beginWriteStream();
      if (!ws)
        return false;
      m_ticket.write(*ws);
    }

    /* Extract TMD */
    SystemString tmdPath = basePathStr + _SYS_STR("/tmd.bin");
    if (ctx.force || Stat(tmdPath.c_str(), &theStat)) {
      if (ctx.progressCB)
        ctx.progressCB("tmd.bin", 0.f);
      auto ws = NewFileIO(tmdPath)->beginWriteStream();
      if (!ws)
        return false;
      m_tmd.write(*ws);
    }

    /* Extract Certs */
    SystemString certPath = basePathStr + _SYS_STR("/cert.bin");
    if (ctx.force || Stat(certPath.c_str(), &theStat)) {
      if (ctx.progressCB)
        ctx.progressCB("cert.bin", 0.f);
      auto ws = NewFileIO(certPath)->beginWriteStream();
      if (!ws)
        return false;
      m_caCert.write(*ws);
      m_tmdCert.write(*ws);
      m_ticketCert.write(*ws);
    }

    /* Extract H3 */
    SystemString h3Path = basePathStr + _SYS_STR("/h3.bin");
    if (ctx.force || Stat(h3Path.c_str(), &theStat)) {
      if (ctx.progressCB)
        ctx.progressCB("h3.bin", 0.f);
      auto ws = NewFileIO(h3Path)->beginWriteStream();
      if (!ws)
        return false;
      ws->write(m_h3Data.get(), 0x18000);
    }

    return true;
  }
};

DiscWii::DiscWii(std::unique_ptr<IDiscIO>&& dio, bool& err) : DiscBase(std::move(dio), err) {
  if (err)
    return;

  /* Read partition info */
  struct PartInfo {
    uint32_t partCount;
    uint32_t partInfoOff;
    struct Part {
      uint32_t partDataOff;
      PartitionKind partType;
    } parts[4];
    PartInfo(IDiscIO& dio, bool& err) {
      std::unique_ptr<IReadStream> s = dio.beginReadStream(0x40000);
      if (!s) {
        err = true;
        return;
      }

      s->read(this, 32);
      partCount = SBig(partCount);
      partInfoOff = SBig(partInfoOff);

      s->seek(partInfoOff << 2);
      for (uint32_t p = 0; p < partCount && p < 4; ++p) {
        s->read(&parts[p], 8);
        parts[p].partDataOff = SBig(parts[p].partDataOff);
        parts[p].partType = PartitionKind(SBig(uint32_t(parts[p].partType)));
      }
    }
  } partInfo(*m_discIO, err);
  if (err)
    return;

  /* Iterate for data partition */
  m_partitions.reserve(partInfo.partCount);
  for (uint32_t p = 0; p < partInfo.partCount && p < 4; ++p) {
    PartInfo::Part& part = partInfo.parts[p];
    PartitionKind kind;
    switch (part.partType) {
    case PartitionKind::Data:
    case PartitionKind::Update:
    case PartitionKind::Channel:
      kind = part.partType;
      break;
    default:
      LogModule.report(logvisor::Error, FMT_STRING("invalid partition type {}"), part.partType);
      err = true;
      return;
    }
    m_partitions.emplace_back(std::make_unique<PartitionWii>(*this, kind, part.partDataOff << 2, err));
    if (err)
      return;
  }
}

DiscBuilderWii DiscWii::makeMergeBuilder(SystemStringView outPath, bool dualLayer, FProgress progressCB) {
  return DiscBuilderWii(outPath, dualLayer, progressCB);
}

bool DiscWii::extractDiscHeaderFiles(SystemStringView basePath, const ExtractionContext& ctx) const {
  SystemString basePathStr(basePath);

  if (Mkdir((basePathStr + _SYS_STR("/disc")).c_str(), 0755) && errno != EEXIST) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to mkdir '{}/disc'")), basePathStr);
    return false;
  }

  Sstat theStat;

  /* Extract Header */
  SystemString headerPath = basePathStr + _SYS_STR("/disc/header.bin");
  if (ctx.force || Stat(headerPath.c_str(), &theStat)) {
    if (ctx.progressCB)
      ctx.progressCB("header.bin", 0.f);
    std::unique_ptr<IReadStream> rs = getDiscIO().beginReadStream(0x0);
    if (!rs)
      return false;
    Header header;
    header.read(*rs);
    auto ws = NewFileIO(headerPath)->beginWriteStream();
    if (!ws)
      return false;
    header.write(*ws);
  }

  /* Extract Region info */
  SystemString regionPath = basePathStr + _SYS_STR("/disc/region.bin");
  if (ctx.force || Stat(regionPath.c_str(), &theStat)) {
    if (ctx.progressCB)
      ctx.progressCB("header.bin", 0.f);
    std::unique_ptr<IReadStream> rs = getDiscIO().beginReadStream(0x4E000);
    if (!rs)
      return false;
    std::unique_ptr<uint8_t[]> buf(new uint8_t[0x20]);
    rs->read(buf.get(), 0x20);
    auto ws = NewFileIO(regionPath)->beginWriteStream();
    if (!ws)
      return false;
    ws->write(buf.get(), 0x20);
  }

  return true;
}

static const uint8_t ZEROIV[16] = {0};

class PartitionBuilderWii : public DiscBuilderBase::PartitionBuilderBase {
  friend class DiscBuilderWii;
  friend class DiscMergerWii;

  uint64_t m_baseOffset;
  uint64_t m_userOffset = 0;
  uint64_t m_curUser = 0x1F0000;
  std::unique_ptr<IAES> m_aes;
  uint8_t m_h3[4916][20] = {};

public:
  class PartWriteStream : public IPartWriteStream {
    friend class PartitionBuilderWii;
    PartitionBuilderWii& m_parent;
    uint64_t m_baseOffset;
    uint64_t m_offset;
    std::unique_ptr<IFileIO::IWriteStream> m_fio;
    bool m_closed = false;

    size_t m_curGroup = SIZE_MAX;
    char m_buf[0x200000];

    void encryptGroup(uint8_t h3Out[20]) {
      sha1nfo sha;
      uint8_t h2[8][20];

      for (int s = 0; s < 8; ++s) {
        char* ptr1 = m_buf + s * 0x40000;
        uint8_t h1[8][20];

        for (int c = 0; c < 8; ++c) {
          char* ptr0 = ptr1 + c * 0x8000;
          uint8_t h0[31][20];

          for (int j = 0; j < 31; ++j) {
            sha1_init(&sha);
            sha1_write(&sha, ptr0 + (j + 1) * 0x400, 0x400);
            memmove(h0[j], sha1_result(&sha), 20);
          }

          sha1_init(&sha);
          sha1_write(&sha, (char*)h0, 0x26C);
          memmove(h1[c], sha1_result(&sha), 20);

          memmove(ptr0, h0, 0x26C);
          memset(ptr0 + 0x26C, 0, 0x014);
        }

        sha1_init(&sha);
        sha1_write(&sha, (char*)h1, 0x0A0);
        memmove(h2[s], sha1_result(&sha), 20);

        for (int c = 0; c < 8; ++c) {
          char* ptr0 = ptr1 + c * 0x8000;
          memmove(ptr0 + 0x280, h1, 0x0A0);
          memset(ptr0 + 0x320, 0, 0x020);
        }
      }

      sha1_init(&sha);
      sha1_write(&sha, (char*)h2, 0x0A0);
      memmove(h3Out, sha1_result(&sha), 20);

      for (int s = 0; s < 8; ++s) {
        char* ptr1 = m_buf + s * 0x40000;
        for (int c = 0; c < 8; ++c) {
          char* ptr0 = ptr1 + c * 0x8000;
          memmove(ptr0 + 0x340, h2, 0x0A0);
          memset(ptr0 + 0x3E0, 0, 0x020);
          m_parent.m_aes->encrypt(ZEROIV, (uint8_t*)ptr0, (uint8_t*)ptr0, 0x400);
          m_parent.m_aes->encrypt((uint8_t*)(ptr0 + 0x3D0), (uint8_t*)(ptr0 + 0x400), (uint8_t*)(ptr0 + 0x400), 0x7c00);
        }
      }

      if (m_fio->write(m_buf, 0x200000) != 0x200000) {
        LogModule.report(logvisor::Error, FMT_STRING("unable to write full disc group"));
        return;
      }
    }

  public:
    PartWriteStream(PartitionBuilderWii& parent, uint64_t baseOffset, uint64_t offset, bool& err)
    : m_parent(parent), m_baseOffset(baseOffset), m_offset(offset) {
      if (offset % 0x1F0000) {
        LogModule.report(logvisor::Error, FMT_STRING("partition write stream MUST begin on 0x1F0000-aligned boundary"));
        err = true;
        return;
      }
      size_t group = m_offset / 0x1F0000;
      m_fio = m_parent.m_parent.getFileIO().beginWriteStream(m_baseOffset + group * 0x200000);
      if (!m_fio)
        err = true;
      m_curGroup = group;
    }
    ~PartWriteStream() override { PartWriteStream::close(); }
    void close() override {
      if (m_closed)
        return;
      m_closed = true;
      size_t rem = m_offset % 0x1F0000;
      if (rem) {
        rem = 0x1F0000 - rem;
        write(nullptr, rem);
      }
      encryptGroup(m_parent.m_h3[m_curGroup]);
      m_fio.reset();
    }
    uint64_t position() const override { return m_offset; }
    uint64_t write(const void* buf, uint64_t length) override {
      size_t group = m_offset / 0x1F0000;
      size_t block = (m_offset - group * 0x1F0000) / 0x7c00;
      size_t cacheOffset = m_offset % 0x7c00;
      uint64_t cacheSize;
      uint64_t rem = length;
      const uint8_t* src = (uint8_t*)buf;

      while (rem) {
        if (group != m_curGroup) {
          encryptGroup(m_parent.m_h3[m_curGroup]);
          m_curGroup = group;
        }

        cacheSize = rem;
        if (cacheSize + cacheOffset > 0x7c00)
          cacheSize = 0x7c00 - cacheOffset;

        if (src) {
          memmove(m_buf + block * 0x8000 + 0x400 + cacheOffset, src, cacheSize);
          src += cacheSize;
        } else
          memset(m_buf + block * 0x8000 + 0x400 + cacheOffset, 0, cacheSize);

        rem -= cacheSize;
        cacheOffset = 0;
        ++block;
        if (block == 64) {
          block = 0;
          ++group;
        }
      }

      m_offset += length;
      return length;
    }
  };

  PartitionBuilderWii(DiscBuilderBase& parent, PartitionKind kind, uint64_t baseOffset)
  : DiscBuilderBase::PartitionBuilderBase(parent, kind, true), m_baseOffset(baseOffset), m_aes(NewAES()) {}

  uint64_t getCurUserEnd() const { return m_curUser; }

  uint64_t userAllocate(uint64_t reqSz, IPartWriteStream& ws) override {
    reqSz = ROUND_UP_32(reqSz);
    if (m_curUser + reqSz >= 0x1FB450000) {
      LogModule.report(logvisor::Error, FMT_STRING("partition exceeds maximum single-partition capacity"));
      return -1;
    }
    uint64_t ret = m_curUser;
    PartWriteStream& cws = static_cast<PartWriteStream&>(ws);
    if (cws.m_offset > ret) {
      LogModule.report(logvisor::Error, FMT_STRING("partition overwrite error"));
      return -1;
    }
    while (cws.m_offset < ret)
      cws.write("\xff", 1);
    m_curUser += reqSz;
    return ret;
  }

  uint32_t packOffset(uint64_t offset) const override { return uint32_t(offset >> uint64_t(2)); }

  std::unique_ptr<IPartWriteStream> beginWriteStream(uint64_t offset) override {
    bool err = false;
    auto ret = std::make_unique<PartWriteStream>(*this, m_baseOffset + m_userOffset, offset, err);

    if (err) {
      return nullptr;
    }

    return ret;
  }

  uint64_t _build(const std::function<bool(IFileIO::IWriteStream&, uint32_t& h3Off, uint32_t& dataOff, uint8_t& ccIdx,
                                           uint8_t tkey[16], uint8_t tkeyiv[16], std::unique_ptr<uint8_t[]>& tmdData,
                                           size_t& tmdSz)>& cryptoFunc,
                  const std::function<bool(IPartWriteStream&, uint32_t, uint32_t, uint32_t)>& headerFunc,
                  const std::function<bool(IPartWriteStream&)>& bi2Func,
                  const std::function<bool(IPartWriteStream&, size_t&)>& apploaderFunc,
                  const std::function<bool(IPartWriteStream&)>& contentFunc, size_t apploaderSz) {
    /* Write partition head up to H3 table */
    std::unique_ptr<IFileIO::IWriteStream> ws = m_parent.getFileIO().beginWriteStream(m_baseOffset);
    if (!ws)
      return -1;
    uint32_t h3Off, dataOff;
    uint8_t tkey[16], tkeyiv[16];
    uint8_t ccIdx;
    std::unique_ptr<uint8_t[]> tmdData;
    size_t tmdSz;
    if (!cryptoFunc(*ws, h3Off, dataOff, ccIdx, tkey, tkeyiv, tmdData, tmdSz))
      return -1;

    m_userOffset = dataOff;

    /* Prepare crypto pass */
    m_aes->setKey(COMMON_KEYS[ccIdx]);
    m_aes->decrypt(tkeyiv, tkey, tkey, 16);
    m_aes->setKey(tkey);

    {
      /* Assemble partition data */
      std::unique_ptr<IPartWriteStream> cws = beginWriteStream(0x1F0000);
      if (!cws)
        return -1;
      if (!contentFunc(*cws))
        return -1;

      /* Pad out user area to nearest cleartext sector */
      m_curUser = cws->position();
      uint64_t curUserRem = m_curUser % 0x1F0000;
      if (curUserRem) {
        curUserRem = 0x1F0000 - curUserRem;
        for (size_t i = 0; i < curUserRem; ++i)
          cws->write("\xff", 1);
        m_curUser += curUserRem;
      }

      /* Begin crypto write and add content header */
      cws = beginWriteStream(0);
      if (!cws)
        return -1;

      /* Compute boot table members and write */
      size_t fstOff = 0x2440 + ROUND_UP_32(apploaderSz);
      size_t fstSz = sizeof(FSTNode) * m_buildNodes.size();
      fstSz += m_buildNameOff;
      fstSz = ROUND_UP_32(fstSz);

      if (fstOff + fstSz >= 0x1F0000) {
        LogModule.report(logvisor::Error, FMT_STRING("FST flows into user area (one or the other is too big)"));
        return -1;
      }

      if (!headerFunc(*cws, m_dolOffset, fstOff, fstSz))
        return -1;

      if (!bi2Func(*cws))
        return -1;

      size_t xferSz = 0;
      if (!apploaderFunc(*cws, xferSz))
        return -1;

      size_t fstOffRel = fstOff - 0x2440;
      if (xferSz > fstOffRel) {
        LogModule.report(logvisor::Error, FMT_STRING("apploader unexpectedly flows into FST"));
        return -1;
      }
      for (size_t i = 0; i < fstOffRel - xferSz; ++i)
        cws->write("\xff", 1);

      /* Write FST */
      cws->write(m_buildNodes.data(), m_buildNodes.size() * sizeof(FSTNode));
      for (const std::string& str : m_buildNames)
        cws->write(str.data(), str.size() + 1);
    }

    /* Write new crypto content size */
    uint64_t groupCount = m_curUser / 0x1F0000;
    uint64_t cryptContentSize = (groupCount * 0x200000) >> uint64_t(2);
    uint32_t cryptContentSizeBig = SBig(uint32_t(cryptContentSize));
    ws = m_parent.getFileIO().beginWriteStream(m_baseOffset + 0x2BC);
    if (!ws)
      return -1;
    ws->write(&cryptContentSizeBig, 0x4);

    /* Write new H3 */
    ws = m_parent.getFileIO().beginWriteStream(m_baseOffset + h3Off);
    if (!ws)
      return -1;
    ws->write(m_h3, 0x18000);

    /* Compute content hash and replace in TMD */
    sha1nfo sha;
    sha1_init(&sha);
    sha1_write(&sha, (char*)m_h3, 0x18000);
    memmove(tmdData.get() + 0x1F4, sha1_result(&sha), 20);

    /* Same for content size */
    uint64_t contentSize = groupCount * 0x1F0000;
    uint64_t contentSizeBig = SBig(contentSize);
    memmove(tmdData.get() + 0x1EC, &contentSizeBig, 8);

    /* Zero-out TMD signature to simplify brute-force */
    memset(tmdData.get() + 0x4, 0, 0x100);

    /* Brute-force zero-starting hash */
    size_t tmdCheckSz = tmdSz - 0x140;
    struct BFWindow {
      uint64_t word[7];
    }* bfWindow = (BFWindow*)(tmdData.get() + 0x19A);
    bool good = false;
    uint64_t attempts = 0;
    SystemString bfName(_SYS_STR("Brute force attempts"));
    for (int w = 0; w < 7; ++w) {
      for (uint64_t i = 0; i < UINT64_MAX; ++i) {
        bfWindow->word[w] = i;
        sha1_init(&sha);
        sha1_write(&sha, (char*)(tmdData.get() + 0x140), tmdCheckSz);
        uint8_t* hash = sha1_result(&sha);
        ++attempts;
        if (hash[0] == 0) {
          good = true;
          break;
        }
        m_parent.m_progressCB(m_parent.getProgressFactor(), bfName, attempts);
      }
      if (good)
        break;
    }
    m_parent.m_progressCB(m_parent.getProgressFactor(), bfName, attempts);
    ++m_parent.m_progressIdx;

    ws = m_parent.getFileIO().beginWriteStream(m_baseOffset + 0x2C0);
    if (!ws)
      return -1;
    ws->write(tmdData.get(), tmdSz);

    return m_baseOffset + dataOff + groupCount * 0x200000;
  }

  uint64_t buildFromDirectory(SystemStringView dirIn) {
    SystemString dirStr(dirIn);
    SystemString basePath = dirStr + _SYS_STR("/") + getKindString(m_kind);

    /* Check Ticket */
    SystemString ticketIn = basePath + _SYS_STR("/ticket.bin");
    Sstat theStat;
    if (Stat(ticketIn.c_str(), &theStat)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), ticketIn);
      return -1;
    }

    /* Check TMD */
    SystemString tmdIn = basePath + _SYS_STR("/tmd.bin");
    Sstat tmdStat;
    if (Stat(tmdIn.c_str(), &tmdStat)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), tmdIn);
      return -1;
    }

    /* Check Cert */
    SystemString certIn = basePath + _SYS_STR("/cert.bin");
    Sstat certStat;
    if (Stat(certIn.c_str(), &certStat)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), certIn);
      return -1;
    }

    /* Check Apploader */
    SystemString apploaderIn = basePath + _SYS_STR("/sys/apploader.img");
    Sstat apploaderStat;
    if (Stat(apploaderIn.c_str(), &apploaderStat)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), apploaderIn);
      return -1;
    }

    /* Check Boot */
    SystemString bootIn = basePath + _SYS_STR("/sys/boot.bin");
    Sstat bootStat;
    if (Stat(bootIn.c_str(), &bootStat)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), bootIn);
      return -1;
    }

    /* Check BI2 */
    SystemString bi2In = basePath + _SYS_STR("/sys/bi2.bin");
    Sstat bi2Stat;
    if (Stat(bi2In.c_str(), &bi2Stat)) {
      LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {}")), bi2In);
      return -1;
    }

    return _build(
        [&](IFileIO::IWriteStream& ws, uint32_t& h3OffOut, uint32_t& dataOffOut, uint8_t& ccIdx, uint8_t tkey[16],
            uint8_t tkeyiv[16], std::unique_ptr<uint8_t[]>& tmdData, size_t& tmdSzOut) -> bool {
          h3OffOut = 0x8000;
          dataOffOut = 0x20000;

          std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(ticketIn.c_str())->beginReadStream();
          if (!rs)
            return false;
          uint8_t buf[0x2A4];
          memset(buf, 0, 0x2A4);
          rs->read(buf, 0x2A4);
          ws.write(buf, 0x2A4);

          ccIdx = buf[0x1F1];
          memmove(tkey, buf + 0x1BF, 16);
          memmove(tkeyiv, buf + 0x1DC, 8);
          memset(tkeyiv + 8, 0, 8);

          uint32_t curOff = 0x2C0;
          uint32_t tmdSz = SBig(uint32_t(tmdStat.st_size));
          ws.write(&tmdSz, 4);
          uint32_t tmdOff = SBig(curOff >> 2);
          ws.write(&tmdOff, 4);
          curOff += ROUND_UP_32(tmdStat.st_size);

          uint32_t certSz = SBig(uint32_t(certStat.st_size));
          ws.write(&certSz, 4);
          uint32_t certOff = SBig(curOff >> 2);
          ws.write(&certOff, 4);
          curOff += ROUND_UP_32(certStat.st_size);

          uint32_t h3Off = SBig(0x8000 >> 2);
          ws.write(&h3Off, 4);
          uint32_t dataOff = SBig(0x20000 >> 2);
          ws.write(&dataOff, 4);
          uint32_t dataSz = 0;
          ws.write(&dataSz, 4);

          rs = NewFileIO(tmdIn.c_str())->beginReadStream();
          tmdData.reset(new uint8_t[tmdStat.st_size]);
          tmdSzOut = tmdStat.st_size;
          rs->read(tmdData.get(), tmdStat.st_size);
          ws.write(tmdData.get(), tmdStat.st_size);
          uint32_t tmdPadding = ROUND_UP_32(tmdStat.st_size) - tmdStat.st_size;
          for (uint32_t i = 0; i < tmdPadding; ++i)
            ws.write("", 1);

          rs = NewFileIO(certIn.c_str())->beginReadStream();
          std::unique_ptr<uint8_t[]> certBuf(new uint8_t[certStat.st_size]);
          rs->read(certBuf.get(), certStat.st_size);
          ws.write(certBuf.get(), certStat.st_size);

          return true;
        },
        [&bootIn](IPartWriteStream& cws, uint32_t dolOff, uint32_t fstOff, uint32_t fstSz) -> bool {
          std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(bootIn.c_str())->beginReadStream();
          if (!rs)
            return false;
          Header header;
          header.read(*rs);
          header.m_dolOff = uint32_t(dolOff >> 2);
          header.m_fstOff = uint32_t(fstOff >> 2);
          header.m_fstSz = fstSz;
          header.m_fstMaxSz = fstSz;
          header.write(cws);
          return true;
        },
        [&bi2In](IPartWriteStream& cws) -> bool {
          std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(bi2In.c_str())->beginReadStream();
          if (!rs)
            return false;
          BI2Header bi2;
          bi2.read(*rs);
          bi2.write(cws);
          return true;
        },
        [this, &apploaderIn](IPartWriteStream& cws, size_t& xferSz) -> bool {
          std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(apploaderIn.c_str())->beginReadStream();
          if (!rs)
            return false;
          char buf[8192];
          while (true) {
            size_t rdSz = rs->read(buf, 8192);
            if (!rdSz)
              break;
            cws.write(buf, rdSz);
            xferSz += rdSz;
            if (0x2440 + xferSz >= 0x1F0000) {
              LogModule.report(logvisor::Error, FMT_STRING("apploader flows into user area (one or the other is too big)"));
              return false;
            }
            m_parent.m_progressCB(m_parent.getProgressFactor(), apploaderIn, xferSz);
          }
          ++m_parent.m_progressIdx;
          return true;
        },
        [this, dirIn](IPartWriteStream& cws) -> bool {
          return DiscBuilderBase::PartitionBuilderBase::buildFromDirectory(cws, dirIn);
        },
        apploaderStat.st_size);
  }

  uint64_t mergeFromDirectory(const PartitionWii* partIn, SystemStringView dirIn) {
    size_t phSz;
    std::unique_ptr<uint8_t[]> phBuf = partIn->readPartitionHeaderBuf(phSz);

    return _build(
        [&](IFileIO::IWriteStream& ws, uint32_t& h3OffOut, uint32_t& dataOffOut, uint8_t& ccIdx, uint8_t tkey[16],
            uint8_t tkeyiv[16], std::unique_ptr<uint8_t[]>& tmdData, size_t& tmdSz) -> bool {
          h3OffOut = SBig(*reinterpret_cast<uint32_t*>(&phBuf[0x2B4])) << 2;
          dataOffOut = SBig(*reinterpret_cast<uint32_t*>(&phBuf[0x2B8])) << 2;

          ccIdx = phBuf[0x1F1];
          memmove(tkey, phBuf.get() + 0x1BF, 16);
          memmove(tkeyiv, phBuf.get() + 0x1DC, 8);
          memset(tkeyiv + 8, 0, 8);

          tmdSz = SBig(*reinterpret_cast<uint32_t*>(&phBuf[0x2A4]));
          tmdData.reset(new uint8_t[tmdSz]);
          memmove(tmdData.get(), phBuf.get() + 0x2C0, tmdSz);

          size_t copySz = std::min(phSz, size_t(h3OffOut));
          ws.write(phBuf.get(), copySz);

          return true;
        },
        [partIn](IPartWriteStream& cws, uint32_t dolOff, uint32_t fstOff, uint32_t fstSz) -> bool {
          Header header = partIn->getHeader();
          header.m_dolOff = uint32_t(dolOff >> uint64_t(2));
          header.m_fstOff = uint32_t(fstOff >> uint64_t(2));
          header.m_fstSz = fstSz;
          header.m_fstMaxSz = fstSz;
          header.write(cws);
          return true;
        },
        [partIn](IPartWriteStream& cws) -> bool {
          partIn->getBI2().write(cws);
          return true;
        },
        [this, partIn](IPartWriteStream& cws, size_t& xferSz) -> bool {
          std::unique_ptr<uint8_t[]> apploaderBuf = partIn->getApploaderBuf();
          size_t apploaderSz = partIn->getApploaderSize();
          SystemString apploaderName(_SYS_STR("<apploader>"));
          cws.write(apploaderBuf.get(), apploaderSz);
          xferSz += apploaderSz;
          if (0x2440 + xferSz >= 0x1F0000) {
            LogModule.report(logvisor::Error, FMT_STRING("apploader flows into user area (one or the other is too big)"));
            return false;
          }
          m_parent.m_progressCB(m_parent.getProgressFactor(), apploaderName, xferSz);
          ++m_parent.m_progressIdx;
          return true;
        },
        [this, partIn, dirIn](IPartWriteStream& cws) -> bool {
          return DiscBuilderBase::PartitionBuilderBase::mergeFromDirectory(cws, partIn, dirIn);
        },
        partIn->getApploaderSize());
  }
};

EBuildResult DiscBuilderWii::buildFromDirectory(SystemStringView dirIn) {
  SystemString dirStr(dirIn);
  SystemString basePath = SystemString(dirStr) + _SYS_STR("/") + getKindString(PartitionKind::Data);

  PartitionBuilderWii& pb = static_cast<PartitionBuilderWii&>(*m_partitions[0]);
  uint64_t filledSz = pb.m_baseOffset;
  if (!m_fileIO->beginWriteStream())
    return EBuildResult::Failed;

  if (!CheckFreeSpace(m_outPath.c_str(), m_discCapacity)) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("not enough free disk space for {}")), m_outPath);
    return EBuildResult::DiskFull;
  }
  m_progressCB(getProgressFactor(), _SYS_STR("Preallocating image"), -1);
  ++m_progressIdx;
  {
    std::unique_ptr<IFileIO::IWriteStream> ws = m_fileIO->beginWriteStream(0);
    if (!ws)
      return EBuildResult::Failed;
    char zeroBytes[1024] = {};
    for (int64_t i = 0; i < m_discCapacity; i += 1024)
      ws->write(zeroBytes, 1024);
  }

  /* Assemble image */
  filledSz = pb.buildFromDirectory(dirIn);
  if (filledSz == UINT64_MAX)
    return EBuildResult::Failed;
  else if (filledSz >= uint64_t(m_discCapacity)) {
    LogModule.report(logvisor::Error, FMT_STRING("data partition exceeds disc capacity"));
    return EBuildResult::Failed;
  }

  m_progressCB(getProgressFactor(), _SYS_STR("Finishing Disc"), -1);
  ++m_progressIdx;

  /* Populate disc header */
  std::unique_ptr<IFileIO::IWriteStream> ws = m_fileIO->beginWriteStream(0);
  if (!ws)
    return EBuildResult::Failed;
  SystemString headerPath = basePath + _SYS_STR("/disc/header.bin");
  std::unique_ptr<IFileIO::IReadStream> rs = NewFileIO(headerPath.c_str())->beginReadStream();
  if (!rs)
    return EBuildResult::Failed;
  Header header;
  header.read(*rs);
  header.write(*ws);

  /* Populate partition info */
  ws = m_fileIO->beginWriteStream(0x40000);
  if (!ws)
    return EBuildResult::Failed;
  uint32_t vals[2] = {SBig(uint32_t(1)), SBig(uint32_t(0x40020 >> uint64_t(2)))};
  ws->write(vals, 8);

  ws = m_fileIO->beginWriteStream(0x40020);
  if (!ws)
    return EBuildResult::Failed;
  vals[0] = SBig(uint32_t(pb.m_baseOffset >> uint64_t(2)));
  ws->write(vals, 4);

  /* Populate region info */
  SystemString regionPath = basePath + _SYS_STR("/disc/region.bin");
  rs = NewFileIO(regionPath.c_str())->beginReadStream();
  if (!rs)
    return EBuildResult::Failed;
  uint8_t regionBuf[0x20];
  rs->read(regionBuf, 0x20);
  ws = m_fileIO->beginWriteStream(0x4E000);
  if (!ws)
    return EBuildResult::Failed;
  ws->write(regionBuf, 0x20);

  /* Fill image to end */
  ws = m_fileIO->beginWriteStream(filledSz);
  if (!ws)
    return EBuildResult::Failed;
  uint8_t fillBuf[512];
  memset(fillBuf, 0xff, 512);
  for (size_t i = m_discCapacity - filledSz; i > 0;) {
    if (i >= 512) {
      ws->write(fillBuf, 512);
      i -= 512;
      continue;
    }
    ws->write(fillBuf, i);
    break;
  }

  return EBuildResult::Success;
}

std::optional<uint64_t> DiscBuilderWii::CalculateTotalSizeRequired(SystemStringView dirIn, bool& dualLayer) {
  std::optional<uint64_t> sz = DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeBuild(dirIn, PartitionKind::Data, true);
  if (!sz)
    return sz;  
  auto szDiv = nod::div(*sz, uint64_t(0x1F0000));
  if (szDiv.rem)
    ++szDiv.quot;
  sz = szDiv.quot * 0x200000;
  *sz += 0x200000;
  dualLayer = (sz > 0x118240000);
  if (sz > 0x1FB4E0000) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("disc capacity exceeded [{} / {}]")), *sz, 0x1FB4E0000);
    return std::nullopt;
  }
  return sz;
}

DiscBuilderWii::DiscBuilderWii(SystemStringView outPath, bool dualLayer, FProgress progressCB)
: DiscBuilderBase(outPath, dualLayer ? 0x1FB4E0000 : 0x118240000, progressCB) {
  m_partitions.emplace_back(std::make_unique<PartitionBuilderWii>(*this, PartitionKind::Data, 0x200000));
}

DiscMergerWii::DiscMergerWii(SystemStringView outPath, DiscWii& sourceDisc, bool dualLayer, FProgress progressCB)
: m_sourceDisc(sourceDisc), m_builder(sourceDisc.makeMergeBuilder(outPath, dualLayer, progressCB)) {}

EBuildResult DiscMergerWii::mergeFromDirectory(SystemStringView dirIn) {
  PartitionBuilderWii& pb = static_cast<PartitionBuilderWii&>(*m_builder.m_partitions[0]);
  uint64_t filledSz = pb.m_baseOffset;
  if (!m_builder.m_fileIO->beginWriteStream())
    return EBuildResult::Failed;

  if (!CheckFreeSpace(m_builder.m_outPath.c_str(), m_builder.m_discCapacity)) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("not enough free disk space for {}")), m_builder.m_outPath);
    return EBuildResult::DiskFull;
  }
  m_builder.m_progressCB(m_builder.getProgressFactor(), _SYS_STR("Preallocating image"), -1);
  ++m_builder.m_progressIdx;
  {
    std::unique_ptr<IFileIO::IWriteStream> ws = m_builder.m_fileIO->beginWriteStream(0);
    if (!ws)
      return EBuildResult::Failed;
    char zeroBytes[1024] = {};
    for (int64_t i = 0; i < m_builder.m_discCapacity; i += 1024)
      ws->write(zeroBytes, 1024);
  }

  /* Assemble image */
  filledSz = pb.mergeFromDirectory(static_cast<PartitionWii*>(m_sourceDisc.getDataPartition()), dirIn);
  if (filledSz == UINT64_MAX)
    return EBuildResult::Failed;
  else if (filledSz >= uint64_t(m_builder.m_discCapacity)) {
    LogModule.report(logvisor::Error, FMT_STRING("data partition exceeds disc capacity"));
    return EBuildResult::Failed;
  }

  m_builder.m_progressCB(m_builder.getProgressFactor(), _SYS_STR("Finishing Disc"), -1);
  ++m_builder.m_progressIdx;

  /* Populate disc header */
  std::unique_ptr<IFileIO::IWriteStream> ws = m_builder.m_fileIO->beginWriteStream(0);
  if (!ws)
    return EBuildResult::Failed;
  m_sourceDisc.getHeader().write(*ws);

  /* Populate partition info */
  ws = m_builder.m_fileIO->beginWriteStream(0x40000);
  if (!ws)
    return EBuildResult::Failed;
  uint32_t vals[2] = {SBig(uint32_t(1)), SBig(uint32_t(0x40020 >> uint64_t(2)))};
  ws->write(vals, 8);

  ws = m_builder.m_fileIO->beginWriteStream(0x40020);
  if (!ws)
    return EBuildResult::Failed;
  vals[0] = SBig(uint32_t(pb.m_baseOffset >> uint64_t(2)));
  ws->write(vals, 4);

  /* Populate region info */
  std::unique_ptr<IReadStream> rs = m_sourceDisc.getDiscIO().beginReadStream(0x4E000);
  if (!rs)
    return EBuildResult::Failed;
  uint8_t regionBuf[0x20];
  rs->read(regionBuf, 0x20);
  ws = m_builder.m_fileIO->beginWriteStream(0x4E000);
  if (!ws)
    return EBuildResult::Failed;
  ws->write(regionBuf, 0x20);

  /* Fill image to end */
  ws = m_builder.m_fileIO->beginWriteStream(filledSz);
  if (!ws)
    return EBuildResult::Failed;
  uint8_t fillBuf[512];
  memset(fillBuf, 0xff, 512);
  for (size_t i = m_builder.m_discCapacity - filledSz; i > 0;) {
    if (i >= 512) {
      ws->write(fillBuf, 512);
      i -= 512;
      continue;
    }
    ws->write(fillBuf, i);
    break;
  }

  return EBuildResult::Success;
}

std::optional<uint64_t> DiscMergerWii::CalculateTotalSizeRequired(DiscWii& sourceDisc, SystemStringView dirIn, bool& dualLayer) {
  std::optional<uint64_t> sz = DiscBuilderBase::PartitionBuilderBase::CalculateTotalSizeMerge(sourceDisc.getDataPartition(), dirIn);
  if (!sz)
    return std::nullopt;
  auto szDiv = nod::div(*sz, uint64_t(0x1F0000));
  if (szDiv.rem)
    ++szDiv.quot;
  sz = szDiv.quot * 0x200000;
  *sz += 0x200000;
  dualLayer = (sz > 0x118240000);
  if (sz > 0x1FB4E0000) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("disc capacity exceeded [{} / {}]")), *sz, 0x1FB4E0000);
    return std::nullopt;
  }
  return sz;
}

} // namespace nod
