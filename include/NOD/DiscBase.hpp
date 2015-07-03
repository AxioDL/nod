#ifndef __NOD_DISC_BASE__
#define __NOD_DISC_BASE__

#include <vector>
#include <memory>
#include <string>
#include <stdio.h>
#include <stdint.h>
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

    class FSTNode
    {
        uint32_t typeAndNameOffset;
        uint32_t offset;
        uint32_t length;
    public:
        inline bool isDir() const {return SBig(typeAndNameOffset) >> 24;}
        inline uint32_t getNameOffset() const {return SBig(typeAndNameOffset) & 0xffffff;}
        inline uint32_t getOffset() const {return SBig(offset);}
        inline uint32_t getLength() const {return SBig(length);}
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
        struct DOLHeader
        {
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

        class Node
        {
        public:
            enum Kind
            {
                NODE_FILE,
                NODE_DIRECTORY
            };
        private:
            friend class IPartition;
            const IPartition& m_parent;
            Kind m_kind;

            std::unique_ptr<IFileIO> m_hddFile;
            uint64_t m_discOffset;
            uint64_t m_discLength;
            std::string m_name;

            std::vector<Node>::iterator m_childrenBegin;
            std::vector<Node>::iterator m_childrenEnd;

        public:
            Node(const IPartition& parent, const FSTNode& node, const char* name)
            : m_parent(parent),
              m_kind(node.isDir() ? NODE_DIRECTORY : NODE_FILE),
              m_discOffset(parent.normalizeOffset(node.getOffset())),
              m_discLength(node.getLength()),
              m_name(name) {}
            inline Kind getKind() const {return m_kind;}
            inline const std::string& getName() const {return m_name;}
            std::unique_ptr<IPartReadStream> beginReadStream() const
            {
                if (m_kind != NODE_FILE)
                {
                    throw std::runtime_error("unable to stream a non-file");
                    return std::unique_ptr<IPartReadStream>();
                }
                return m_parent.beginReadStream(m_discOffset);
            }
            inline std::vector<Node>::iterator rawBegin() const {return m_childrenBegin;}
            inline std::vector<Node>::iterator rawEnd() const {return m_childrenEnd;}

            class DirectoryIterator : std::iterator<std::forward_iterator_tag, Node>
            {
                friend class Node;
                std::vector<Node>::iterator m_it;
                DirectoryIterator(const std::vector<Node>::iterator& it)
                : m_it(it) {}
            public:
                inline bool operator!=(const DirectoryIterator& other) {return m_it != other.m_it;}
                inline DirectoryIterator& operator++()
                {
                    if (m_it->m_kind == NODE_DIRECTORY)
                        m_it = m_it->rawEnd();
                    else
                        ++m_it;
                    return *this;
                }
                inline Node& operator*() {return *m_it;}
            };
            inline DirectoryIterator begin() const {return DirectoryIterator(m_childrenBegin);}
            inline DirectoryIterator end() const {return DirectoryIterator(m_childrenEnd);}

            void extractToDirectory(const SystemString& basePath, bool force=false);
        };
    protected:
        uint64_t m_dolOff;
        uint64_t m_fstOff;
        uint64_t m_fstSz;
        uint64_t m_apploaderSz;
        std::vector<Node> m_nodes;
        void parseFST(IPartReadStream& s);

        const DiscBase& m_parent;
        Kind m_kind;
        uint64_t m_offset;
    public:
        IPartition(const DiscBase& parent, Kind kind, uint64_t offset)
        : m_parent(parent), m_kind(kind), m_offset(offset) {}
        virtual uint64_t normalizeOffset(uint64_t anOffset) const {return anOffset;}
        inline Kind getKind() const {return m_kind;}
        virtual std::unique_ptr<IPartReadStream> beginReadStream(uint64_t offset=0) const=0;
        inline const Node& getFSTRoot() const {return m_nodes[0];}
        inline Node& getFSTRoot() {return m_nodes[0];}
        void extractToDirectory(const SystemString& path, bool force=false);
    };

protected:
    std::unique_ptr<IDiscIO> m_discIO;
    Header m_header;
    std::vector<std::unique_ptr<IPartition>> m_partitions;
public:
    DiscBase(std::unique_ptr<IDiscIO>&& dio);
    virtual bool commit()=0;
    inline const Header& getHeader() const {return m_header;}
    inline const IDiscIO& getDiscIO() const {return *m_discIO.get();}
    inline IPartition* getDataPartition()
    {
        for (const std::unique_ptr<IPartition>& part : m_partitions)
            if (part->getKind() == IPartition::PART_DATA)
                return part.get();
        return nullptr;
    }
    inline IPartition* getUpdatePartition()
    {
        for (const std::unique_ptr<IPartition>& part : m_partitions)
            if (part->getKind() == IPartition::PART_UPDATE)
                return part.get();
        return nullptr;
    }
    inline void extractToDirectory(const SystemString& path, bool force=false)
    {
        for (std::unique_ptr<IPartition>& part : m_partitions)
            part->extractToDirectory(path, force);
    }
};

}

#endif // __NOD_DISC_BASE__
