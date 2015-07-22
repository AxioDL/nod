#include <stdio.h>
#include <stdexcept>
#include "NOD/Util.hpp"
#include "NOD/IDiscIO.hpp"

#if _WIN32
#define ftello _ftelli64
#define fseeko _fseeki64
#endif

namespace NOD
{

#define ALIGN_LBA(x) (((x)+p->hd_sec_sz-1)&(~(p->hd_sec_sz-1)))

static uint8_t size_to_shift(uint32_t size)
{
    uint8_t ret = 0;
    while (size)
    {
        ret++;
        size>>=1;
    }
    return ret-1;
}

class DiscIOWBFS : public IDiscIO
{
    SystemString filepath;

    struct WBFSHead
    {
        uint32_t magic;
        // parameters copied in the partition for easy dumping, and bug reports
        uint32_t n_hd_sec;	       // total number of hd_sec in this partition
        uint8_t  hd_sec_sz_s;       // sector size in this partition
        uint8_t  wbfs_sec_sz_s;     // size of a wbfs sec
        uint8_t  padding3[2];
        uint8_t  disc_table[0];	// size depends on hd sector size
    };
    std::unique_ptr<uint8_t[]> wbfsHead;

    struct WBFSDiscInfo
    {
        uint8_t disc_header_copy[0x100];
        uint16_t wlba_table[0];
    };
    std::unique_ptr<uint8_t[]> wbfsDiscInfo;

    struct WBFS
    {
        /* hdsectors, the size of the sector provided by the hosting hard drive */
        uint32_t hd_sec_sz;
        uint8_t  hd_sec_sz_s; // the power of two of the last number
        uint32_t n_hd_sec;	 // the number of hd sector in the wbfs partition

        /* standard wii sector (0x8000 bytes) */
        uint32_t wii_sec_sz;
        uint8_t  wii_sec_sz_s;
        uint32_t n_wii_sec;
        uint32_t n_wii_sec_per_disc;

        /* The size of a wbfs sector */
        uint32_t wbfs_sec_sz;
        uint32_t wbfs_sec_sz_s;
        uint16_t n_wbfs_sec;   // this must fit in 16 bit!
        uint16_t n_wbfs_sec_per_disc;   // size of the lookup table

        uint32_t part_lba;

        uint16_t max_disc;
        uint32_t freeblks_lba;
        uint32_t *freeblks;
        uint16_t disc_info_sz;

        uint32_t n_disc_open;

    } wbfs;

    static int _wbfsReadSector(FILE* fp, uint32_t lba, uint32_t count, void* buf)
    {
        uint64_t off = lba;
        off*=512ULL;
        if (fseeko(fp, off, SEEK_SET))
        {
            LogModule.report(LogVisor::FatalError, "error seeking in disc partition: %lld %d", off, count);
            return 1;
        }
        if (fread(buf, count*512ULL, 1, fp) != 1){
            LogModule.report(LogVisor::FatalError, "error reading disc");
            return 1;
        }
        return 0;
    }

public:
    DiscIOWBFS(const SystemString& fpin)
    : filepath(fpin)
    {
        /* Temporary file handle to read LBA table */
#if NOD_UCS2
        FILE* fp = _wfopen(filepath.c_str(), L"rb");
#else
        FILE* fp = fopen(filepath.c_str(), "rb");
#endif

        WBFS* p = &wbfs;
        WBFSHead tmpHead;
        if (fread(&tmpHead, 1, sizeof(tmpHead), fp) != sizeof(tmpHead))
            LogModule.report(LogVisor::FatalError, "unable to read WBFS head");
        fseek(fp, 0, SEEK_SET);
        unsigned hd_sector_size = 1 << tmpHead.hd_sec_sz_s;
        unsigned num_hd_sector = SBig(tmpHead.n_hd_sec);

        wbfsHead.reset(new uint8_t[hd_sector_size]);
        WBFSHead* head = (WBFSHead*)wbfsHead.get();
        if (fread(head, 1, hd_sector_size, fp) != hd_sector_size)
            LogModule.report(LogVisor::FatalError, "unable to read WBFS head");

        //constants, but put here for consistancy
        p->wii_sec_sz = 0x8000;
        p->wii_sec_sz_s = size_to_shift(0x8000);
        p->n_wii_sec = (num_hd_sector/0x8000)*hd_sector_size;
        p->n_wii_sec_per_disc = 143432*2;//support for double layers discs..
        p->part_lba = 0;
        _wbfsReadSector(fp, p->part_lba, 1, head);
        if (hd_sector_size && head->hd_sec_sz_s !=  size_to_shift(hd_sector_size)) {
            LogModule.report(LogVisor::FatalError, "hd sector size doesn't match");
        }
        if (num_hd_sector && head->n_hd_sec != SBig(num_hd_sector)) {
            LogModule.report(LogVisor::FatalError, "hd num sector doesn't match");
        }
        p->hd_sec_sz = 1<<head->hd_sec_sz_s;
        p->hd_sec_sz_s = head->hd_sec_sz_s;
        p->n_hd_sec = SBig(head->n_hd_sec);

        p->n_wii_sec = (p->n_hd_sec/p->wii_sec_sz)*(p->hd_sec_sz);

        p->wbfs_sec_sz_s = head->wbfs_sec_sz_s;
        p->wbfs_sec_sz = 1<<p->wbfs_sec_sz_s;
        p->n_wbfs_sec = p->n_wii_sec >> (p->wbfs_sec_sz_s - p->wii_sec_sz_s);
        p->n_wbfs_sec_per_disc = p->n_wii_sec_per_disc >> (p->wbfs_sec_sz_s - p->wii_sec_sz_s);
        p->disc_info_sz = ALIGN_LBA(sizeof(WBFSDiscInfo) + p->n_wbfs_sec_per_disc*2);

        p->freeblks_lba = (p->wbfs_sec_sz - p->n_wbfs_sec/8)>>p->hd_sec_sz_s;

        p->freeblks = 0; // will alloc and read only if needed
        p->max_disc = (p->freeblks_lba-1)/(p->disc_info_sz>>p->hd_sec_sz_s);
        if(p->max_disc > p->hd_sec_sz - sizeof(WBFSHead))
            p->max_disc = p->hd_sec_sz - sizeof(WBFSHead);

        p->n_disc_open = 0;

        int disc_info_sz_lba = p->disc_info_sz>>p->hd_sec_sz_s;
        if (head->disc_table[0])
        {
            wbfsDiscInfo.reset(new uint8_t[p->disc_info_sz]);
            if (!wbfsDiscInfo)
                LogModule.report(LogVisor::FatalError, "allocating memory");
            _wbfsReadSector(fp, p->part_lba+1, disc_info_sz_lba, wbfsDiscInfo.get());
            p->n_disc_open++;
            //for(i=0;i<p->n_wbfs_sec_per_disc;i++)
            //    printf("%d,",wbfs_ntohs(d->header->wlba_table[i]));
        }
    }

    class ReadStream : public IReadStream
    {
        friend class DiscIOWBFS;
        const DiscIOWBFS& m_parent;
        FILE* fp;
        uint64_t m_offset;
        std::unique_ptr<uint8_t[]> m_tmpBuffer;

        ReadStream(const DiscIOWBFS& parent, FILE* fpin, uint64_t offset)
        : m_parent(parent),
          fp(fpin),
          m_offset(offset),
          m_tmpBuffer(new uint8_t[parent.wbfs.hd_sec_sz]) {}
        ~ReadStream() {fclose(fp);}

        int wbfsReadSector(uint32_t lba, uint32_t count, void* buf)
        {return DiscIOWBFS::_wbfsReadSector(fp, lba, count, buf);}

        int wbfsDiscRead(uint32_t offset, uint8_t *data, uint64_t len)
        {
            const WBFS* p = &m_parent.wbfs;
            const WBFSDiscInfo* d = (WBFSDiscInfo*)m_parent.wbfsDiscInfo.get();
            uint16_t wlba = offset>>(p->wbfs_sec_sz_s-2);
            uint32_t iwlba_shift = p->wbfs_sec_sz_s - p->hd_sec_sz_s;
            uint32_t lba_mask = (p->wbfs_sec_sz-1)>>(p->hd_sec_sz_s);
            uint64_t lba = (offset>>(p->hd_sec_sz_s-2))&lba_mask;
            uint64_t off = offset&((p->hd_sec_sz>>2)-1);
            uint16_t iwlba = SBig(d->wlba_table[wlba]);
            uint64_t len_copied;
            int err = 0;
            uint8_t  *ptr = data;
            if (!iwlba)
                return 1;
            if (off)
            {
                off*=4;
                err = wbfsReadSector(p->part_lba + (iwlba<<iwlba_shift) + lba, 1, m_tmpBuffer.get());
                if (err)
                    return err;
                len_copied = p->hd_sec_sz - off;
                if (len < len_copied)
                    len_copied = len;
                memcpy(ptr, m_tmpBuffer.get() + off, len_copied);
                len -= len_copied;
                ptr += len_copied;
                lba++;
                if (lba>lba_mask && len)
                {
                    lba=0;
                    iwlba = SBig(d->wlba_table[++wlba]);
                    if (!iwlba)
                        return 1;
                }
            }
            while (len>=p->hd_sec_sz)
            {
                uint32_t nlb = len>>(p->hd_sec_sz_s);

                if (lba + nlb > p->wbfs_sec_sz) // dont cross wbfs sectors..
                    nlb = p->wbfs_sec_sz-lba;
                err = wbfsReadSector(p->part_lba + (iwlba<<iwlba_shift) + lba, nlb, ptr);
                if (err)
                    return err;
                len -= nlb<<p->hd_sec_sz_s;
                ptr += nlb<<p->hd_sec_sz_s;
                lba += nlb;
                if (lba>lba_mask && len)
                {
                    lba = 0;
                    iwlba = SBig(d->wlba_table[++wlba]);
                    if (!iwlba)
                        return 1;
                }
            }
            if (len)
            {
                err = wbfsReadSector(p->part_lba + (iwlba<<iwlba_shift) + lba, 1, m_tmpBuffer.get());
                if (err)
                    return err;
                memcpy(ptr, m_tmpBuffer.get(), len);
            }
            return 0;
        }

    public:
        uint64_t read(void* buf, uint64_t length)
        {
            uint8_t extra[4];
            uint64_t rem_offset = m_offset % 4;
            if (rem_offset)
            {
                uint64_t rem_rem = 4 - rem_offset;
                if (wbfsDiscRead((uint32_t)(m_offset / 4), extra, 4))
                    return 0;
                memcpy(buf, extra + rem_offset, rem_rem);
                if (wbfsDiscRead((uint32_t)(m_offset / 4 + 1), (uint8_t*)buf + rem_rem, length - rem_rem))
                    return 0;
            }
            else
            {
                if (wbfsDiscRead((uint32_t)(m_offset / 4), (uint8_t*)buf, length))
                    return 0;
            }
            m_offset += length;
            return length;
        }
        uint64_t position() const {return m_offset;}
        void seek(int64_t offset, int whence)
        {
            if (whence == SEEK_SET)
                m_offset = offset;
            else if (whence == SEEK_CUR)
                m_offset += offset;
        }
    };
    std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const
    {
#if NOD_UCS2
        FILE* fp = _wfopen(filepath.c_str(), L"rb");
#else
        FILE* fp = fopen(filepath.c_str(), "rb");
#endif
        if (!fp)
        {
            LogModule.report(LogVisor::Error, _S("Unable to open '%s' for reading"), filepath.c_str());
            return std::unique_ptr<IReadStream>();
        }
        return std::unique_ptr<IReadStream>(new ReadStream(*this, fp, offset));
    }

    class WriteStream : public IWriteStream
    {
        friend class DiscIOWBFS;
        FILE* fp;
        WriteStream(FILE* fpin)
        : fp(fpin) {}
        ~WriteStream() {fclose(fp);}
    public:
        uint64_t write(void* buf, uint64_t length)
        {return fwrite(buf, 1, length, fp);}
    };
    std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const
    {
#if NOD_UCS2
        FILE* fp = _wfopen(filepath.c_str(), L"wb");
#else
        FILE* fp = fopen(filepath.c_str(), "wb");
#endif
        if (!fp)
        {
            LogModule.report(LogVisor::Error, _S("Unable to open '%s' for writing"), filepath.c_str());
            return std::unique_ptr<IWriteStream>();
        }
        fseeko(fp, offset, SEEK_SET);
        return std::unique_ptr<IWriteStream>(new WriteStream(fp));
    }
};

std::unique_ptr<IDiscIO> NewDiscIOWBFS(const SystemChar* path)
{
    return std::unique_ptr<IDiscIO>(new DiscIOWBFS(path));
}

}

