#ifndef __NOD_LIB__
#define __NOD_LIB__

#include <memory>

namespace NOD
{

class DiscBase;

std::unique_ptr<DiscBase> OpenDiscFromImage(const char* path);
std::unique_ptr<DiscBase> OpenDiscFromImage(const char* path, bool& isWii);

}

#include "DiscGCN.hpp"
#include "DiscWii.hpp"
#include "IDiscIO.hpp"
#include "Util.hpp"

#endif // __NOD_LIB__
