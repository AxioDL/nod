#ifndef __NOD_LIB__
#define __NOD_LIB__

#include <memory>
#include <functional>
#include "logvisor/logvisor.hpp"
#include "Util.hpp"

namespace nod
{

class DiscBase;

struct ExtractionContext final
{
    bool verbose : 1;
    bool force : 1;
    std::function<void(const std::string&)> progressCB;
};

std::unique_ptr<DiscBase> OpenDiscFromImage(const SystemChar* path);
std::unique_ptr<DiscBase> OpenDiscFromImage(const SystemChar* path, bool& isWii);

}

#include "DiscGCN.hpp"
#include "DiscWii.hpp"
#include "IDiscIO.hpp"

#endif // __NOD_LIB__
