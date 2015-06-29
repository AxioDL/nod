#include "DiscBase.hpp"

namespace NOD
{

DiscBase::DiscBase(std::unique_ptr<IDiscIO>&& dio)
: m_discIO(std::move(dio)), m_header(*m_discIO.get())
{
}

void DiscBase::IPartition::parseFST()
{
    char buf[1024];
    std::unique_ptr<IPartReadStream> s = beginReadStream();
    s->read(buf, 1024);
    printf("");
}

}
