#ifndef __NOD_DISC_WII__
#define __NOD_DISC_WII__

#include "DiscBase.hpp"

namespace NOD
{

class DiscWii : public DiscBase
{
public:
    DiscWii(std::unique_ptr<IDiscIO>&& dio);
    bool commit();
};

}

#endif // __NOD_DISC_WII__
