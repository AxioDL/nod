#ifndef __NOD_DISC_BASE__
#define __NOD_DISC_BASE__

#include <vector>
#include <memory>
#include <string>
#include "Util.hpp"
#include "IDiscIO.hpp"
#include "IFileIO.hpp"

namespace NOD
{

class DiscBase
{
public:
    struct Header
    {
        char gameID[6];
        char discNum;
        char discVersion;
        char audioStreaming;
        char streamBufSz;
        char unk[14];
        uint32_t wiiMagic;
        uint32_t gcnMagic;
        char gameTitle[64];

        Header(IDiscIO& dio)
        {
            std::unique_ptr<IDiscIO::IReadStream> s = dio.beginReadStream(0);
            s->read(this, sizeof(*this));
            wiiMagic = SBig(wiiMagic);
            gcnMagic = SBig(gcnMagic);
        }
    };

    struct FSTNode
    {
        uint32_t typeAndNameOff;
        uint32_t offset;
        uint32_t length;
        FSTNode(IDiscIO::IReadStream& s)
        {
            s.read(this, 12);
            typeAndNameOff = SBig(typeAndNameOff);
            offset = SBig(offset);
            length = SBig(length);
        }
    };

    struct IPartReadStream
    {
        virtual size_t read(void* buf, size_t length)=0;
    };

    struct IPartWriteStream
    {
        virtual size_t write(void* buf, size_t length)=0;
    };

    class IPartition
    {
    public:
        enum Kind
        {
            PART_DATA,
            PART_UPDATE,
            PART_CHANNEL
        };
        class Node
        {
        public:
            enum Kind
            {
                NODE_FILE,
                NODE_DIRECTORY
            };
        private:
            const IPartition& m_parent;
            Kind m_kind;
            std::string m_name;

            std::unique_ptr<IFileIO> m_hddFile;
            size_t m_discOffset;
            size_t m_discLength;

        public:
            Node(const IPartition& parent, bool isDir, const char* name)
            : m_parent(parent), m_kind(isDir ? NODE_DIRECTORY : NODE_FILE), m_name(name) {}
            inline Kind getKind() const {return m_kind;}
            std::unique_ptr<IPartReadStream> beginReadStream() const
            {
                if (m_kind != NODE_FILE)
                {
                    throw std::runtime_error("unable to stream a non-file");
                    return std::unique_ptr<IPartReadStream>();
                }
                return m_parent.beginReadStream(m_discOffset);
            }
        };
    protected:
        std::vector<Node> m_files;
        void parseFST();

        const DiscBase& m_parent;
        Kind m_kind;
        size_t m_offset;
    public:
        IPartition(const DiscBase& parent, Kind kind, size_t offset)
        : m_parent(parent), m_kind(kind), m_offset(offset) {}
        inline Kind getKind() const {return m_kind;}
        virtual std::unique_ptr<IPartReadStream> beginReadStream(size_t offset=0) const=0;
    };

protected:
    std::unique_ptr<IDiscIO> m_discIO;
    Header m_header;
    std::vector<std::unique_ptr<IPartition>> m_partitions;
public:
    DiscBase(std::unique_ptr<IDiscIO>&& dio);
    virtual bool commit()=0;
    inline Header getHeader() const {return m_header;}
    inline const IDiscIO& getDiscIO() const {return *m_discIO.get();}
};

}

#endif // __NOD_DISC_BASE__
