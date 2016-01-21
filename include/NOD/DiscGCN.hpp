#ifndef __NOD_DISC_GCN__
#define __NOD_DISC_GCN__

#include "DiscBase.hpp"

namespace NOD
{

class DiscGCN : public DiscBase
{
public:
    DiscGCN(std::unique_ptr<IDiscIO>&& dio);
};

class DiscBuilderGCN : public DiscBuilderBase
{
public:
    DiscBuilderGCN(const SystemChar* outPath, const char gameID[6], const char* gameTitle,
                   uint32_t fstMemoryAddr, std::function<void(size_t, const SystemString&, size_t)> progressCB);
    bool buildFromDirectory(const SystemChar* dirIn, const SystemChar* dolIn,
                            const SystemChar* apploaderIn);
};

}


#endif // __NOD_DISC_GCN__
