#include "DiscBase.hpp"

namespace NOD
{

DiscBase::DiscBase(std::unique_ptr<IDiscIO>&& dio)
: m_discIO(std::move(dio)), m_header(*m_discIO.get())
{
}

}
