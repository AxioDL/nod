#include <sys/stat.h>
#include <stdexcept>
#include "IFileIO.hpp"

namespace NOD
{

class FileIOFILE : public IFileIO
{
    const char* filepath;
public:
    FileIOFILE(const char* path)
    : filepath(path)
    {
        struct stat theStat;
        if (stat(path, &theStat))
            throw std::runtime_error("unable to ");
    }
};

}
