#ifndef __NOD_DISC_GCN__
#define __NOD_DISC_GCN__

#include "DiscBase.hpp"

namespace NOD
{

class DiscGCN : public DiscBase
{
public:
    DiscGCN(std::unique_ptr<IDiscIO>&& dio);
    bool commit();
};

}


#endif // __NOD_DISC_GCN__
