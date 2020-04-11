#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <memory>

#include "nod/IDiscIO.hpp"
#include "nod/IFileIO.hpp"
#include "nod/Util.hpp"

#include <logvisor/logvisor.hpp>

namespace nod {

#define ALIGN_LBA(x) (((x) + p->hd_sec_sz - 1) & (~(p->hd_sec_sz - 1)))

static uint8_t size_to_shift(uint32_t size) {
  uint8_t ret = 0;
  while (size) {
    ret++;
    size >>= 1;
  }
  return ret - 1;
}

class DiscIOWBFS : public IDiscIO {
  std::unique_ptr<IFileIO> m_fio;

  struct WBFSHead {
    uint32_t magic;
    // parameters copied in the partition for easy dumping, and bug reports
    uint32_t n_hd_sec;     // total number of hd_sec in this partition
    uint8_t hd_sec_sz_s;   // sector size in this partition
    uint8_t wbfs_sec_sz_s; // size of a wbfs sec
    uint8_t padding3[2];
    uint8_t disc_table[0]; // size depends on hd sector size
  };
  std::unique_ptr<uint8_t[]> wbfsHead;

  struct WBFSDiscInfo {
    uint8_t disc_header_copy[0x100];
    uint16_t wlba_table[0];
  };
  std::unique_ptr<uint8_t[]> wbfsDiscInfo;

  struct WBFS {
    /* hdsectors, the size of the sector provided by the hosting hard drive */
    uint32_t hd_sec_sz;
    uint8_t hd_sec_sz_s; // the power of two of the last number
    uint32_t n_hd_sec;   // the number of hd sector in the wbfs partition

    /* standard wii sector (0x8000 bytes) */
    uint32_t wii_sec_sz;
    uint8_t wii_sec_sz_s;
    uint32_t n_wii_sec;
    uint32_t n_wii_sec_per_disc;

    /* The size of a wbfs sector */
    uint32_t wbfs_sec_sz;
    uint32_t wbfs_sec_sz_s;
    uint16_t n_wbfs_sec;          // this must fit in 16 bit!
    uint16_t n_wbfs_sec_per_disc; // size of the lookup table

    uint32_t part_lba;

    uint16_t max_disc;
    uint32_t freeblks_lba;
    uint32_t* freeblks;
    uint16_t disc_info_sz;

    uint32_t n_disc_open;

  } wbfs;

  static int _wbfsReadSector(IFileIO::IReadStream& rs, uint32_t lba, uint32_t count, void* buf) {
    uint64_t off = lba;
    off *= 512ULL;
    rs.seek(off, SEEK_SET);
    if (rs.read(buf, count * 512ULL) != count * 512ULL) {
      LogModule.report(logvisor::Error, FMT_STRING("error reading disc"));
      return 1;
    }
    return 0;
  }

public:
  DiscIOWBFS(SystemStringView fpin) : m_fio(NewFileIO(fpin)) {
    /* Temporary file handle to read LBA table */
    std::unique_ptr<IFileIO::IReadStream> rs = m_fio->beginReadStream();
    if (!rs)
      return;

    WBFS* p = &wbfs;
    WBFSHead tmpHead;
    if (rs->read(&tmpHead, sizeof(tmpHead)) != sizeof(tmpHead)) {
      LogModule.report(logvisor::Error, FMT_STRING("unable to read WBFS head"));
      return;
    }
    unsigned hd_sector_size = 1 << tmpHead.hd_sec_sz_s;
    unsigned num_hd_sector = SBig(tmpHead.n_hd_sec);

    wbfsHead.reset(new uint8_t[hd_sector_size]);
    WBFSHead* head = (WBFSHead*)wbfsHead.get();
    rs->seek(0, SEEK_SET);
    if (rs->read(head, hd_sector_size) != hd_sector_size) {
      LogModule.report(logvisor::Error, FMT_STRING("unable to read WBFS head"));
      return;
    }

    // constants, but put here for consistancy
    p->wii_sec_sz = 0x8000;
    p->wii_sec_sz_s = size_to_shift(0x8000);
    p->n_wii_sec = (num_hd_sector / 0x8000) * hd_sector_size;
    p->n_wii_sec_per_disc = 143432 * 2; // support for double layers discs..
    p->part_lba = 0;
    if (_wbfsReadSector(*rs, p->part_lba, 1, head))
      return;
    if (hd_sector_size && head->hd_sec_sz_s != size_to_shift(hd_sector_size)) {
      LogModule.report(logvisor::Error, FMT_STRING("hd sector size doesn't match"));
      return;
    }
    if (num_hd_sector && head->n_hd_sec != SBig(num_hd_sector)) {
      LogModule.report(logvisor::Error, FMT_STRING("hd num sector doesn't match"));
      return;
    }
    p->hd_sec_sz = 1 << head->hd_sec_sz_s;
    p->hd_sec_sz_s = head->hd_sec_sz_s;
    p->n_hd_sec = SBig(head->n_hd_sec);

    p->n_wii_sec = (p->n_hd_sec / p->wii_sec_sz) * (p->hd_sec_sz);

    p->wbfs_sec_sz_s = head->wbfs_sec_sz_s;
    p->wbfs_sec_sz = 1 << p->wbfs_sec_sz_s;
    p->n_wbfs_sec = p->n_wii_sec >> (p->wbfs_sec_sz_s - p->wii_sec_sz_s);
    p->n_wbfs_sec_per_disc = p->n_wii_sec_per_disc >> (p->wbfs_sec_sz_s - p->wii_sec_sz_s);
    p->disc_info_sz = ALIGN_LBA(uint16_t(sizeof(WBFSDiscInfo)) + p->n_wbfs_sec_per_disc * 2);

    p->freeblks_lba = (p->wbfs_sec_sz - p->n_wbfs_sec / 8) >> p->hd_sec_sz_s;

    p->freeblks = 0; // will alloc and read only if needed
    p->max_disc = (p->freeblks_lba - 1) / (p->disc_info_sz >> p->hd_sec_sz_s);
    if (p->max_disc > p->hd_sec_sz - sizeof(WBFSHead))
      p->max_disc = p->hd_sec_sz - sizeof(WBFSHead);

    p->n_disc_open = 0;

    int disc_info_sz_lba = p->disc_info_sz >> p->hd_sec_sz_s;
    if (head->disc_table[0]) {
      wbfsDiscInfo.reset(new uint8_t[p->disc_info_sz]);
      if (!wbfsDiscInfo) {
        LogModule.report(logvisor::Error, FMT_STRING("allocating memory"));
        return;
      }
      if (_wbfsReadSector(*rs, p->part_lba + 1, disc_info_sz_lba, wbfsDiscInfo.get()))
        return;
      p->n_disc_open++;
      // for(i=0;i<p->n_wbfs_sec_per_disc;i++)
      //    printf("%d,",wbfs_ntohs(d->header->wlba_table[i]));
    }
  }

  class ReadStream : public IReadStream {
    friend class DiscIOWBFS;
    const DiscIOWBFS& m_parent;
    std::unique_ptr<IFileIO::IReadStream> fp;
    uint64_t m_offset;
    std::unique_ptr<uint8_t[]> m_tmpBuffer;

    ReadStream(const DiscIOWBFS& parent, std::unique_ptr<IFileIO::IReadStream>&& fpin, uint64_t offset, bool& err)
    : m_parent(parent), fp(std::move(fpin)), m_offset(offset), m_tmpBuffer(new uint8_t[parent.wbfs.hd_sec_sz]) {
      if (!fp)
        err = true;
    }

    int wbfsReadSector(uint32_t lba, uint32_t count, void* buf) {
      return DiscIOWBFS::_wbfsReadSector(*fp, lba, count, buf);
    }

    int wbfsDiscRead(uint32_t offset, uint8_t* data, uint64_t len) {
      const WBFS* p = &m_parent.wbfs;
      const WBFSDiscInfo* d = (WBFSDiscInfo*)m_parent.wbfsDiscInfo.get();
      uint16_t wlba = offset >> (p->wbfs_sec_sz_s - 2);
      uint32_t iwlba_shift = p->wbfs_sec_sz_s - p->hd_sec_sz_s;
      uint32_t lba_mask = (p->wbfs_sec_sz - 1) >> (p->hd_sec_sz_s);
      uint64_t lba = (offset >> (p->hd_sec_sz_s - 2)) & lba_mask;
      uint64_t off = offset & ((p->hd_sec_sz >> 2) - 1);
      uint16_t iwlba = SBig(d->wlba_table[wlba]);
      uint64_t len_copied;
      int err = 0;
      uint8_t* ptr = data;
      if (!iwlba)
        return 1;
      if (off) {
        off *= 4;
        err = wbfsReadSector(p->part_lba + (iwlba << iwlba_shift) + lba, 1, m_tmpBuffer.get());
        if (err)
          return err;
        len_copied = p->hd_sec_sz - off;
        if (len < len_copied)
          len_copied = len;
        memcpy(ptr, m_tmpBuffer.get() + off, len_copied);
        len -= len_copied;
        ptr += len_copied;
        lba++;
        if (lba > lba_mask && len) {
          lba = 0;
          iwlba = SBig(d->wlba_table[++wlba]);
          if (!iwlba)
            return 1;
        }
      }
      while (len >= p->hd_sec_sz) {
        uint32_t nlb = len >> (p->hd_sec_sz_s);

        if (lba + nlb > p->wbfs_sec_sz) // dont cross wbfs sectors..
          nlb = p->wbfs_sec_sz - lba;
        err = wbfsReadSector(p->part_lba + (iwlba << iwlba_shift) + lba, nlb, ptr);
        if (err)
          return err;
        len -= nlb << p->hd_sec_sz_s;
        ptr += nlb << p->hd_sec_sz_s;
        lba += nlb;
        if (lba > lba_mask && len) {
          lba = 0;
          iwlba = SBig(d->wlba_table[++wlba]);
          if (!iwlba)
            return 1;
        }
      }
      if (len) {
        err = wbfsReadSector(p->part_lba + (iwlba << iwlba_shift) + lba, 1, m_tmpBuffer.get());
        if (err)
          return err;
        memcpy(ptr, m_tmpBuffer.get(), len);
      }
      return 0;
    }

  public:
    uint64_t read(void* buf, uint64_t length) override {
      uint8_t extra[4];
      uint64_t rem_offset = m_offset % 4;
      if (rem_offset) {
        uint64_t rem_rem = 4 - rem_offset;
        if (wbfsDiscRead((uint32_t)(m_offset / 4), extra, 4))
          return 0;
        memcpy(buf, extra + rem_offset, rem_rem);
        if (wbfsDiscRead((uint32_t)(m_offset / 4 + 1), (uint8_t*)buf + rem_rem, length - rem_rem))
          return 0;
      } else {
        if (wbfsDiscRead((uint32_t)(m_offset / 4), (uint8_t*)buf, length))
          return 0;
      }
      m_offset += length;
      return length;
    }
    uint64_t position() const override { return m_offset; }
    void seek(int64_t offset, int whence) override {
      if (whence == SEEK_SET)
        m_offset = offset;
      else if (whence == SEEK_CUR)
        m_offset += offset;
    }
  };

  std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const override {
    bool err = false;
    auto ret = std::unique_ptr<IReadStream>(new ReadStream(*this, m_fio->beginReadStream(), offset, err));

    if (err)
      return {};

    return ret;
  }

  std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const override { return {}; }
};

std::unique_ptr<IDiscIO> NewDiscIOWBFS(SystemStringView path) { return std::make_unique<DiscIOWBFS>(path); }

} // namespace nod
