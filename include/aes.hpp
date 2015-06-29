#ifndef __AES_HPP_
#define __AES_HPP_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <cpuid.h>

namespace NOD
{

class IAES
{
public:
    virtual void encrypt(uint8_t* iv, const uint8_t* inbuf, uint8_t* outbuf, size_t len)=0;
    virtual void decrypt(uint8_t* iv, const uint8_t* inbuf, uint8_t* outbuf, size_t len)=0;
    virtual void setKey(const uint8_t* key)=0;
};

class SoftwareAES : public IAES
{
protected:
    uint8_t fbsub[256];
    uint8_t rbsub[256];
    uint8_t ptab[256], ltab[256];
    uint32_t ftable[256];
    uint32_t rtable[256];
    uint32_t rco[30];

    /* Parameter-dependent data */

    int Nk, Nb, Nr;
    uint8_t fi[24], ri[24];
    uint32_t fkey[120];
    uint32_t rkey[120];


    uint8_t bmul(uint8_t x, uint8_t y);
    uint32_t SubByte(uint32_t a);
    uint8_t product(uint32_t x, uint32_t y);
    uint32_t InvMixCol(uint32_t x);
    uint8_t ByteSub(uint8_t x);
    void gentables(void);
    void gkey(int nb, int nk, const uint8_t* key);
    void _encrypt(uint8_t* buff);
    void _decrypt(uint8_t* buff);

public:
    void encrypt(uint8_t* iv, const uint8_t* inbuf, uint8_t* outbuf, size_t len);
    void decrypt(uint8_t* iv, const uint8_t* inbuf, uint8_t* outbuf, size_t len);
    void setKey(const uint8_t* key);
};

#if __AES__

#include <wmmintrin.h>

class NiAES : public IAES
{
    __m128i m_ekey[11];
    __m128i m_dkey[11];
public:
    void encrypt(uint8_t* iv, const uint8_t* inbuf, uint8_t* outbuf, size_t len)
    {
        __m128i feedback,data;
        size_t i,j;
        if (len%16)
            len = len/16+1;
        else len /= 16;
            feedback = _mm_loadu_si128((__m128i*)iv);
        for (i=0 ; i<len ; i++)
        {
            data = _mm_loadu_si128(&((__m128i*)inbuf)[i]);
            feedback = _mm_xor_si128(data, feedback);
            feedback = _mm_xor_si128(feedback, m_ekey[0]);
            for(j=1 ; j<10 ; j++)
                feedback = _mm_aesenc_si128(feedback, m_ekey[j]);
            feedback = _mm_aesenclast_si128(feedback, m_ekey[j]);
            _mm_storeu_si128(&((__m128i*)outbuf)[i], feedback);
        }
    }
    void decrypt(uint8_t* iv, const uint8_t* inbuf, uint8_t* outbuf, size_t len)
    {
        __m128i data,feedback,last_in;
        size_t i,j;
        if (len%16)
            len = len/16+1;
        else
            len /= 16;
        feedback = _mm_loadu_si128((__m128i*)iv);
        for (i=0 ; i<len ; i++)
        {
            last_in=_mm_loadu_si128(&((__m128i*)inbuf)[i]);
            data = _mm_xor_si128(last_in, m_dkey[0]);
            for(j=1 ; j<10 ; j++)
                data = _mm_aesdec_si128(data, m_dkey[j]);
            data = _mm_aesdeclast_si128(data, m_dkey[j]);
            data = _mm_xor_si128(data, feedback);
            _mm_storeu_si128(&((__m128i*)outbuf)[i], data);
            feedback = last_in;
        }
    }
    void setKey(const uint8_t* key);
};

#endif

std::unique_ptr<IAES> NewAES();

}

#endif //__AES_HPP_
