#ifndef __NOD_DISC_WII__
#define __NOD_DISC_WII__

#include "DiscBase.hpp"

namespace NOD
{

class DiscWii : public DiscBase
{
public:
    DiscWii(std::unique_ptr<IDiscIO>&& dio);
    void writeOutDataPartitionHeader(const SystemChar* pathOut) const;
};

class DiscBuilderWii : public DiscBuilderBase
{
    const SystemChar* m_outPath;
    bool m_dualLayer;
public:
    DiscBuilderWii(const SystemChar* outPath, const char gameID[6], const char* gameTitle, bool dualLayer,
                   std::function<void(size_t, const SystemString&, size_t)> progressCB);
    bool buildFromDirectory(const SystemChar* dirIn, const SystemChar* dolIn,
                            const SystemChar* apploaderIn, const SystemChar* partHeadIn);
};

}

#endif // __NOD_DISC_WII__
