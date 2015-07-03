#ifndef __NOD_LIB__
#define __NOD_LIB__

#include <memory>
#include <LogVisor/LogVisor.hpp>
#include "Util.hpp"

namespace NOD
{

class DiscBase;

std::unique_ptr<DiscBase> OpenDiscFromImage(const SystemChar* path);
std::unique_ptr<DiscBase> OpenDiscFromImage(const SystemChar* path, bool& isWii);

}

#include "DiscGCN.hpp"
#include "DiscWii.hpp"
#include "IDiscIO.hpp"

#endif // __NOD_LIB__
