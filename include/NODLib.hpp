#ifndef __NOD_LIB__
#define __NOD_LIB__

#include <memory>
#include "DiscGCN.hpp"
#include "DiscWii.hpp"
#include "IDiscIO.hpp"

namespace NOD
{

std::unique_ptr<IDiscIO> NewDiscIOFILE(const char* path);
std::unique_ptr<IDiscIO> NewDiscIOWBFS(const char* path);

}

#endif // __NOD_LIB__
