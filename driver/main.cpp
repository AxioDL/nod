#include <stdio.h>
#include <string.h>
#include "NODLib.hpp"

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: nodlib <image-in>\n");
        return -1;
    }

    std::unique_ptr<NOD::DiscBase> disc = NOD::OpenDiscFromImage(argv[1]);
    if (!disc)
        return -1;

    disc->extractToDirectory("/home/jacko/Desktop/TrilogyDump2");

    const NOD::DiscBase::IPartition* dataPart = disc->getDataPartition();
    if (dataPart)
    {
        for (const NOD::DiscBase::IPartition::Node& node : dataPart->getFSTRoot())
        {
            if (node.getKind() == NOD::DiscBase::IPartition::Node::NODE_FILE)
                printf("FILE: %s\n", node.getName().c_str());
            else if (node.getKind() == NOD::DiscBase::IPartition::Node::NODE_DIRECTORY)
            {
                printf("DIR: %s\n", node.getName().c_str());
                for (const NOD::DiscBase::IPartition::Node& subnode : node)
                    printf("SUBFILE: %s\n", subnode.getName().c_str());
            }
        }
    }

    return 0;
}

