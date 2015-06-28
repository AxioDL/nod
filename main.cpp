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

    bool isWii;
    std::unique_ptr<NOD::DiscBase> disc = NOD::OpenDiscFromImage(argv[1], isWii);
    if (!disc)
        return -1;

    return 0;
}

