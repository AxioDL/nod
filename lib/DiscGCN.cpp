#include "DiscGCN.hpp"

namespace NOD
{

DiscGCN::DiscGCN(std::unique_ptr<IDiscIO>&& dio)
: DiscBase(std::move(dio))
{

}

bool DiscGCN::commit()
{
    return false;
}

}
