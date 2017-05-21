#ifndef __NOD_DISC_BASE__
#define __NOD_DISC_BASE__

#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <stdio.h>
#include <stdint.h>
#include <functional>
#include "Util.hpp"
#include "IDiscIO.hpp"
#include "IFileIO.hpp"

namespace nod
{

using FProgress = std::function<void(size_t, const SystemString&, size_t)>;

class FSTNode
{
    uint32_t typeAndNameOffset;
    uint32_t offset;
    uint32_t length;
public:
    FSTNode(bool isDir, uint32_t nameOff, uint32_t off, uint32_t len)
    {
        typeAndNameOffset = nameOff & 0xffffff;
        typeAndNameOffset |= isDir << 24;
        typeAndNameOffset = SBig(typeAndNameOffset);
        offset = SBig(off);
        length = SBig(len);
    }
    inline bool isDir() const {return ((SBig(typeAndNameOffset) >> 24) != 0);}
    inline uint32_t getNameOffset() const {return SBig(typeAndNameOffset) & 0xffffff;}
    inline uint32_t getOffset() const {return SBig(offset);}
    inline uint32_t getLength() const {return SBig(length);}
    void incrementLength()
    {
        uint32_t orig = SBig(length);
        ++orig;
        length = SBig(orig);
    }
};

struct Header
{
    char m_gameID[6];
    char m_discNum;
    char m_discVersion;
    char m_audioStreaming;
    char m_streamBufSz;
    char m_unk[14];
    uint32_t m_wiiMagic;
    uint32_t m_gcnMagic;
    char m_gameTitle[64];

    Header(IDiscIO& dio, bool& err)
    {
        std::unique_ptr<IDiscIO::IReadStream> s = dio.beginReadStream(0);
        if (!s)
        {
            err = true;
            return;
        }
        s->read(this, sizeof(*this));
        m_wiiMagic = SBig(m_wiiMagic);
        m_gcnMagic = SBig(m_gcnMagic);
    }

    Header(const char gameID[6], const char* gameTitle, bool wii, char discNum=0, char discVersion=0,
           char audioStreaming=1, char streamBufSz=0)
    {
        memset(this, 0, sizeof(*this));
        memcpy(m_gameID, gameID, 6);
        strncpy(m_gameTitle, gameTitle, 64);
        m_discNum = discNum;
        m_discVersion = discVersion;
        m_audioStreaming = audioStreaming;
        m_streamBufSz = streamBufSz;
        if (wii)
            m_wiiMagic = 0x5D1C9EA3;
        else
            m_gcnMagic = 0xC2339F3D;
    }

    template <class WriteStream>
    void write(WriteStream& ws) const
    {
        Header hs(*this);
        hs.m_wiiMagic = SBig(hs.m_wiiMagic);
        hs.m_gcnMagic = SBig(hs.m_gcnMagic);
        ws.write(&hs, sizeof(hs));
    }
};

struct ExtractionContext;
class DiscBase
{
public:
    virtual ~DiscBase() {}

    class IPartition
    {
    public:
        virtual ~IPartition() {}
        enum class Kind : uint32_t
        {
            Data,
            Update,
            Channel
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
            enum class Kind
            {
                File,
                Directory
            };
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
            Node(const IPartition& parent, const FSTNode& node, const char* name)
            : m_parent(parent),
              m_kind(node.isDir() ? Kind::Directory : Kind::File),
              m_discOffset(parent.normalizeOffset(node.getOffset())),
              m_discLength(node.getLength()),
              m_name(name) {}
            inline Kind getKind() const {return m_kind;}
            inline const std::string& getName() const {return m_name;}
            inline uint64_t size() const {return m_discLength;}
            std::unique_ptr<IPartReadStream> beginReadStream(uint64_t offset=0) const
            {
                if (m_kind != Kind::File)
                {
                    LogModule.report(logvisor::Error, "unable to stream a non-file %s", m_name.c_str());
                    return std::unique_ptr<IPartReadStream>();
                }
                return m_parent.beginReadStream(m_discOffset + offset);
            }
            std::unique_ptr<uint8_t[]> getBuf() const
            {
                if (m_kind != Kind::File)
                {
                    LogModule.report(logvisor::Error, "unable to buffer a non-file %s", m_name.c_str());
                    return std::unique_ptr<uint8_t[]>();
                }
                uint8_t* buf = new uint8_t[m_discLength];
                beginReadStream()->read(buf, m_discLength);
                return std::unique_ptr<uint8_t[]>(buf);
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
                inline bool operator==(const DirectoryIterator& other) {return m_it == other.m_it;}
                inline DirectoryIterator& operator++()
                {
                    if (m_it->m_kind == Kind::Directory)
                        m_it = m_it->rawEnd();
                    else
                        ++m_it;
                    return *this;
                }
                inline Node& operator*() {return *m_it;}
                inline Node* operator->() {return &*m_it;}
            };
            inline DirectoryIterator begin() const {return DirectoryIterator(m_childrenBegin);}
            inline DirectoryIterator end() const {return DirectoryIterator(m_childrenEnd);}
            inline DirectoryIterator find(const std::string& name) const
            {
                if (m_kind == Kind::Directory)
                {
                    DirectoryIterator it=begin();
                    for (; it != end() ; ++it)
                    {
                        if (!it->getName().compare(name))
                            return it;
                    }
                    return it;
                }
                return end();
            }

            bool extractToDirectory(const SystemString& basePath, const ExtractionContext& ctx) const;
        };
    protected:
        uint64_t m_dolOff;
        uint64_t m_fstOff;
        uint64_t m_fstSz;
        uint64_t m_fstMemoryAddr;
        uint64_t m_apploaderSz;
        std::vector<Node> m_nodes;
        void parseFST(IPartReadStream& s);

        std::vector<FSTNode> m_buildNodes;
        std::vector<std::string> m_buildNames;
        size_t m_buildNameOff = 0;
        void recursiveBuildNodes(const SystemChar* dirIn, std::function<void(void)> incParents);

        uint64_t m_dolSz;
        void parseDOL(IPartReadStream& s);

        const DiscBase& m_parent;
        Kind m_kind;
        uint64_t m_offset;
    public:
        IPartition(const DiscBase& parent, Kind kind, uint64_t offset)
        : m_parent(parent), m_kind(kind), m_offset(offset) {}
        virtual uint64_t normalizeOffset(uint64_t anOffset) const {return anOffset;}
        inline Kind getKind() const {return m_kind;}
        inline uint64_t getDiscOffset() const {return m_offset;}
        virtual std::unique_ptr<IPartReadStream> beginReadStream(uint64_t offset=0) const=0;
        inline std::unique_ptr<IPartReadStream> beginDOLReadStream(uint64_t offset=0) const
        {return beginReadStream(m_dolOff + offset);}
        inline std::unique_ptr<IPartReadStream> beginFSTReadStream(uint64_t offset=0) const
        {return beginReadStream(m_fstOff + offset);}
        inline std::unique_ptr<IPartReadStream> beginApploaderReadStream(uint64_t offset=0) const
        {return beginReadStream(0x2440 + offset);}
        inline const Node& getFSTRoot() const {return m_nodes[0];}
        inline Node& getFSTRoot() {return m_nodes[0];}
        bool extractToDirectory(const SystemString& path, const ExtractionContext& ctx);

        inline uint64_t getDOLSize() const {return m_dolSz;}
        inline std::unique_ptr<uint8_t[]> getDOLBuf() const
        {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[m_dolSz]);
            beginDOLReadStream()->read(buf.get(), m_dolSz);
            return buf;
        }

        inline uint64_t getFSTSize() const {return m_fstSz;}
        inline std::unique_ptr<uint8_t[]> getFSTBuf() const
        {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[m_fstSz]);
            beginFSTReadStream()->read(buf.get(), m_fstSz);
            return buf;
        }

        inline uint64_t getFSTMemoryAddr() const {return m_fstMemoryAddr;}

        inline uint64_t getApploaderSize() const {return m_apploaderSz;}
        inline std::unique_ptr<uint8_t[]> getApploaderBuf() const
        {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[m_apploaderSz]);
            beginApploaderReadStream()->read(buf.get(), m_apploaderSz);
            return buf;
        }

        inline size_t getNodeCount() const { return m_nodes.size(); }
    };

protected:
    std::unique_ptr<IDiscIO> m_discIO;
    Header m_header;
    std::vector<std::unique_ptr<IPartition>> m_partitions;
public:
    DiscBase(std::unique_ptr<IDiscIO>&& dio, bool& err)
    : m_discIO(std::move(dio)), m_header(*m_discIO, err) {}

    inline const Header& getHeader() const {return m_header;}
    inline const IDiscIO& getDiscIO() const {return *m_discIO;}
    inline size_t getPartitonNodeCount(size_t partition = 0) const
    {
        if (partition > m_partitions.size())
            return -1;
        return m_partitions[partition]->getNodeCount();
    }

    inline IPartition* getDataPartition()
    {
        for (const std::unique_ptr<IPartition>& part : m_partitions)
            if (part->getKind() == IPartition::Kind::Data)
                return part.get();
        return nullptr;
    }
    inline IPartition* getUpdatePartition()
    {
        for (const std::unique_ptr<IPartition>& part : m_partitions)
            if (part->getKind() == IPartition::Kind::Update)
                return part.get();
        return nullptr;
    }
    inline void extractToDirectory(const SystemString& path, const ExtractionContext& ctx)
    {
        for (std::unique_ptr<IPartition>& part : m_partitions)
            part->extractToDirectory(path, ctx);
    }

};

class DiscBuilderBase
{
    friend class DiscMergerWii;
public:
    class PartitionBuilderBase
    {
    public:
        virtual ~PartitionBuilderBase() {}
        enum class Kind : uint32_t
        {
            Data,
            Update,
            Channel
        };
    protected:
        std::unordered_map<SystemString, std::pair<uint64_t,uint64_t>> m_fileOffsetsSizes;
        std::vector<FSTNode> m_buildNodes;
        std::vector<std::string> m_buildNames;
        size_t m_buildNameOff = 0;
        virtual uint64_t userAllocate(uint64_t reqSz, IPartWriteStream& ws)=0;
        virtual uint32_t packOffset(uint64_t offset) const=0;
        bool recursiveBuildNodes(IPartWriteStream& ws, bool system, const SystemChar* dirIn, uint64_t dolInode);
        bool recursiveBuildFST(const SystemChar* dirIn, uint64_t dolInode,
                               std::function<void(void)> incParents);
        bool recursiveMergeNodes(IPartWriteStream& ws, bool system,
                                 const DiscBase::IPartition::Node* nodeIn, const SystemChar* dirIn,
                                 const SystemString& keyPath);
        bool recursiveMergeFST(const DiscBase::IPartition::Node* nodeIn,
                               const SystemChar* dirIn, std::function<void(void)> incParents,
                               const SystemString& keyPath);
        static bool RecursiveCalculateTotalSize(uint64_t& totalSz,
                                                const DiscBase::IPartition::Node* nodeIn,
                                                const SystemChar* dirIn);
        void addBuildName(const SystemString& str)
        {
            SystemUTF8View utf8View(str);
            m_buildNames.push_back(utf8View.utf8_str());
            m_buildNameOff += str.size() + 1;
        }

        DiscBuilderBase& m_parent;
        Kind m_kind;

        char m_gameID[6];
        std::string m_gameTitle;
        uint64_t m_dolOffset = 0;
        uint64_t m_dolSize = 0;
    public:
        PartitionBuilderBase(DiscBuilderBase& parent, Kind kind,
                             const char gameID[6], const char* gameTitle)
        : m_parent(parent), m_kind(kind), m_gameTitle(gameTitle)
        {
            memcpy(m_gameID, gameID, 6);
        }
        virtual std::unique_ptr<IPartWriteStream> beginWriteStream(uint64_t offset)=0;
        bool buildFromDirectory(IPartWriteStream& ws,
                                const SystemChar* dirIn, const SystemChar* dolIn,
                                const SystemChar* apploaderIn);
        static uint64_t CalculateTotalSizeBuild(const SystemChar* dolIn,
                                                const SystemChar* dirIn);
        bool mergeFromDirectory(IPartWriteStream& ws,
                                const DiscBase::IPartition* partIn,
                                const SystemChar* dirIn);
        static uint64_t CalculateTotalSizeMerge(const DiscBase::IPartition* partIn,
                                                const SystemChar* dirIn);

        const char* getGameID() const {return m_gameID;}
        const std::string& getGameTitle() const {return m_gameTitle;}
    };
protected:
    SystemString m_outPath;
    std::unique_ptr<IFileIO> m_fileIO;
    std::vector<std::unique_ptr<PartitionBuilderBase>> m_partitions;
    int64_t m_discCapacity;
public:
    std::function<void(size_t idx, const SystemString&, size_t)> m_progressCB;
    size_t m_progressIdx = 0;
    virtual ~DiscBuilderBase() = default;
    DiscBuilderBase(const SystemChar* outPath, int64_t discCapacity,
                    std::function<void(size_t idx, const SystemString&, size_t)> progressCB)
    : m_outPath(outPath), m_fileIO(NewFileIO(outPath, discCapacity)),
      m_discCapacity(discCapacity), m_progressCB(progressCB) {}
    DiscBuilderBase(DiscBuilderBase&&) = default;
    DiscBuilderBase& operator=(DiscBuilderBase&&) = default;

    IFileIO& getFileIO() {return *m_fileIO;}
};

using Partition = DiscBase::IPartition;
using Node = Partition::Node;

}

#endif // __NOD_DISC_BASE__
