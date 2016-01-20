#ifndef __NOD_DISC_GCN__
#define __NOD_DISC_GCN__

#include "DiscBase.hpp"

namespace NOD
{

class DiscGCN : public DiscBase
{
public:
    DiscGCN(std::unique_ptr<IDiscIO>&& dio);
    bool packFromDirectory(const SystemChar* dataPath, const SystemChar* updatePath,
                           const SystemChar* outPath, const char gameID[6], const char* gameTitle,
                           bool korean=false);
};

}


#endif // __NOD_DISC_GCN__
