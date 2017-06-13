#ifndef HASHBLOCK_H
#define HASHBLOCK_H

#include "uint256.h"
#include "sph_jh.h"
#include "sph_keccak.h"
#include "sph_echo.h"

#ifndef QT_NO_DEBUG
#include <string>
#endif

#ifdef GLOBALDEFINED
#define GLOBAL
#else
#define GLOBAL extern
#endif

GLOBAL sph_jh512_context        z_jh;
GLOBAL sph_keccak512_context    z_keccak;
GLOBAL sph_echo512_context      z_echo;

#define fillz() do { \
    sph_jh512_init(&z_jh); \
    sph_keccak512_init(&z_keccak); \
    sph_echo512_init(&z_echo); \
} while (0)
	

#define ZJH (memcpy(&ctx_jh, &z_jh, sizeof(z_jh)))
#define ZKECCAK (memcpy(&ctx_keccak, &z_keccak, sizeof(z_keccak)))


template<typename T1>
inline uint256 Tribus(const T1 pbegin, const T1 pend)

{
    sph_jh512_context        ctx_jh;
    sph_keccak512_context    ctx_keccak;
    sph_echo512_context      ctx_echo;
    static unsigned char pblank[1];

#ifndef QT_NO_DEBUG
    //std::string strhash;
    //strhash = "";
#endif
    
    uint512 hash[17];

    sph_jh512_init(&ctx_jh);
    sph_jh512 (&ctx_jh, (pbegin == pend ? pblank : static_cast<const void*>(&pbegin[0])), (pend - pbegin) * sizeof(pbegin[0]));
    sph_jh512_close(&ctx_jh, static_cast<void*>(&hash[0]));

    sph_keccak512_init(&ctx_keccak);
    sph_keccak512 (&ctx_keccak, static_cast<const void*>(&hash[0]), 64);
    sph_keccak512_close(&ctx_keccak, static_cast<void*>(&hash[1]));

    sph_echo512_init(&ctx_echo);
    sph_echo512 (&ctx_echo, static_cast<const void*>(&hash[1]), 64);
    sph_echo512_close(&ctx_echo, static_cast<void*>(&hash[2]));
	
    return hash[2].trim256();
}






#endif // HASHBLOCK_H
