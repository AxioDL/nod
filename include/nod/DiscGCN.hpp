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
    bool extractDiscHeaderFiles(const SystemString& path, const ExtractionContext& ctx) const;
};

class DiscBuilderGCN : public DiscBuilderBase
{
    friend class DiscMergerGCN;
public:
    DiscBuilderGCN(const SystemChar* outPath, FProgress progressCB);
    EBuildResult buildFromDirectory(const SystemChar* dirIn);
    static uint64_t CalculateTotalSizeRequired(const SystemChar* dirIn);
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
