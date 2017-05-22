#ifndef __NOD_DISC_GCN__
#define __NOD_DISC_GCN__

#include "DiscBase.hpp"

namespace nod
{
class DiscBuilderGCN;

class DiscGCN : public DiscBase
{
    friend class DiscMergerGCN;
    DiscBuilderGCN makeMergeBuilder(const SystemChar* outPath, FProgress progressCB);
public:
    DiscGCN(std::unique_ptr<IDiscIO>&& dio, bool& err);
};

class DiscBuilderGCN : public DiscBuilderBase
{
    friend class DiscMergerGCN;
public:
    DiscBuilderGCN(const SystemChar* outPath, const char gameID[6], const char* gameTitle,
                   uint32_t fstMemoryAddr, FProgress progressCB);
    EBuildResult buildFromDirectory(const SystemChar* dirIn, const SystemChar* dolIn,
                                    const SystemChar* apploaderIn);
    static uint64_t CalculateTotalSizeRequired(const SystemChar* dirIn, const SystemChar* dolIn);
};

class DiscMergerGCN
{
    DiscGCN& m_sourceDisc;
    DiscBuilderGCN m_builder;
public:
    DiscMergerGCN(const SystemChar* outPath, DiscGCN& sourceDisc, FProgress progressCB);
    EBuildResult mergeFromDirectory(const SystemChar* dirIn);
    static uint64_t CalculateTotalSizeRequired(DiscGCN& sourceDisc, const SystemChar* dirIn);
};

}


#endif // __NOD_DISC_GCN__
