#ifndef __AES_HPP__
#define __AES_HPP__

#include <stdint.h>
#include <stdlib.h>
#include <memory>

namespace NOD
{

class IAES
{
public:
    virtual void encrypt(uint8_t* iv, const uint8_t* inbuf, uint8_t* outbuf, uint64_t len)=0;
    virtual void decrypt(uint8_t* iv, const uint8_t* inbuf, uint8_t* outbuf, uint64_t len)=0;
    virtual void setKey(const uint8_t* key)=0;
};

std::unique_ptr<IAES> NewAES();

}

#endif //__AES_HPP__
