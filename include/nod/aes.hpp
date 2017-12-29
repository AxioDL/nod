#ifndef __AES_HPP__
#define __AES_HPP__

#include <cstdint>
#include <cstdlib>
#include <memory>

namespace nod
{

class IAES
{
public:
    virtual ~IAES() = default;
    virtual void encrypt(const uint8_t* iv, const uint8_t* inbuf, uint8_t* outbuf, size_t len)=0;
    virtual void decrypt(const uint8_t* iv, const uint8_t* inbuf, uint8_t* outbuf, size_t len)=0;
    virtual void setKey(const uint8_t* key)=0;
};

std::unique_ptr<IAES> NewAES();

}

#endif //__AES_HPP__
