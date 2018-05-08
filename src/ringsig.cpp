// Copyright (c) 2018 Denarius developers
// Copyright (c) 2014 The ShadowCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "ringsig.h"
#include "base58.h"

#include <openssl/rand.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>


static EC_GROUP* ecGrp         = NULL;
static BN_CTX*   bnCtx         = NULL;
static BIGNUM*   bnP           = NULL;
static BIGNUM*   bnN           = NULL;

static int printPoint(EC_POINT *P, const char *pref)
{
    char* v;
    BIGNUM* bnX = BN_new();
    BIGNUM* bnY = BN_new();

    printf("printPoint %s\n", pref != NULL ? pref : "");
    if (EC_POINT_get_affine_coordinates_GFp(ecGrp, P, bnX, bnY, NULL))
    {
        v = BN_bn2hex(bnX);
        printf("bnX %s\n", v);
        OPENSSL_free(v);
        v = BN_bn2hex(bnY);
        printf("bnY %s\n", v);
        OPENSSL_free(v);
    };
    BN_free(bnX);
    BN_free(bnY);

    return 0;
};

static int printBN(BIGNUM* bn, const char *pref)
{
    char* v = BN_bn2hex(bn);
    if (!v)
    {
         printf("printBN error, prefix %s\n", pref != NULL ? pref : "");
         return 1;
    };

    printf("printBN %s %s\n", pref != NULL ? pref : "", v);

    OPENSSL_free(v);

    return 0;
};

int initialiseRingSigs()
{
    if (fDebugRingSig)
        printf("initialiseRingSigs()\n");

    if (!ecGrp && !(ecGrp = EC_GROUP_new_by_curve_name(NID_secp256k1)))
    {
        printf("initialiseRingSigs(): EC_GROUP_new_by_curve_name failed.\n");
        return 1;
    };

    if (!bnCtx && !(bnCtx = BN_CTX_new()))
    {
        printf("initialiseRingSigs(): BN_CTX_new failed.\n");
        return 1;
    };

    bnN = BN_new();
    EC_GROUP_get_order(ecGrp, bnN, bnCtx);

    bnP = BN_new();
    EC_GROUP_get_curve_GFp(ecGrp, bnP, NULL, NULL, bnCtx);

    return 0;
};

int finaliseRingSigs()
{
    if (fDebugRingSig)
        printf("finaliseRingSigs()\n");

    if (bnN)
        BN_free(bnN);
    if (bnP)
        BN_free(bnP);
    if (bnCtx)
        BN_CTX_free(bnCtx);
    if (ecGrp)
        EC_GROUP_free(ecGrp);

    return 0;
};

int splitAmount(int64_t nValue, std::vector<int64_t>& vOut)
{
    int64_t nTest = 1;
    int i;

    // split amounts into 1, 3, 4, 5

    while (nValue >= nTest)
    {
        i = (nValue / nTest) % 10;
        switch (i)
        {
            case 0:
                break;
            case 2:
                vOut.push_back(1*nTest);
                vOut.push_back(1*nTest);
                break;
            case 6:
                vOut.push_back(5*nTest);
                vOut.push_back(1*nTest);
                break;
            case 7:
                vOut.push_back(3*nTest);
                vOut.push_back(4*nTest);
                break;
            case 8:
                vOut.push_back(5*nTest);
                vOut.push_back(3*nTest);
                break;
            case 9:
                vOut.push_back(5*nTest);
                vOut.push_back(4*nTest);
                break;
            default:
                vOut.push_back(i*nTest);
        };
        nTest *= 10;
    };

    return 0;
};

static int hashToEC(const uint8_t* p, uint32_t len, BIGNUM* bnTmp, EC_POINT* ptRet)
{
    // bn(hash(data)) * G
    uint256 pkHash = Hash(p, p + len);

    if (!bnTmp || !(BN_bin2bn(pkHash.begin(), 32, bnTmp)))
    {
        printf("hashToEC(): BN_bin2bn failed.\n");
        return 1;
    };

    if (!ptRet
        || !EC_POINT_mul(ecGrp, ptRet, bnTmp, NULL, NULL, bnCtx))
    {
        printf("hashToEC() EC_POINT_mul failed.\n");
        return 1;
    };

    return 0;
};

int generateKeyImage(ec_point &publicKey, ec_secret secret, ec_point &keyImage)
{
    // -- keyImage = secret * hash(publicKey) * G

    if (publicKey.size() != ec_compressed_size)
    {
        printf("generateKeyImage(): invalid publicKey.\n");
        return 1;
    };

    int rv = 0;
    BN_CTX_start(bnCtx);
    BIGNUM*   bnTmp     = BN_CTX_get(bnCtx);
    BIGNUM*   bnSec     = BN_CTX_get(bnCtx);
    EC_POINT* hG        = NULL;

    if (!(hG = EC_POINT_new(ecGrp)))
    {
        printf("generateKeyImage() EC_POINT_new failed.\n");
        rv = 1; goto End;
    };

    if (hashToEC(&publicKey[0], publicKey.size(), bnTmp, hG) != 0)
    {
        printf("generateKeyImage(): hashToEC failed.\n");
        rv = 1; goto End;
    };

    if (!bnSec || !(BN_bin2bn(&secret.e[0], 32, bnSec)))
    {
        printf("generateKeyImage(): BN_bin2bn failed.\n");
        rv = 1; goto End;
    };

    if (!EC_POINT_mul(ecGrp, hG, NULL, hG, bnSec, bnCtx))
    {
        printf("kimg EC_POINT_mul failed.\n");
        rv = 1; goto End;
    };

    //printPoint(hG, "kimg");

    try { keyImage.resize(ec_compressed_size); } catch (std::exception& e)
    {
        printf("keyImage.resize threw: %s.\n", e.what());
        rv = 1; goto End;
    };

    if (!(EC_POINT_point2bn(ecGrp, hG, POINT_CONVERSION_COMPRESSED, bnTmp, bnCtx))
        || BN_num_bytes(bnTmp) != (int) ec_compressed_size
        || BN_bn2bin(bnTmp, &keyImage[0]) != (int) ec_compressed_size)
    {
        printf("point -> keyImage failed.\n");
        rv = 1; goto End;
    };

    //printBN(bnTmp, "[rem] bnTmp");

    if (fDebugRingSig)
        printf("keyImage %s\n", HexStr(keyImage).c_str());

    End:
    if (hG)
        EC_POINT_free(hG);
    BN_CTX_end(bnCtx);

    return rv;
};


int verifyRingSignature(std::vector<uint8_t>& keyImage, uint256& txnHash, int nRingSize, const uint8_t *pPubkeys, const uint8_t *pSigc, const uint8_t *pSigr)
{
    if (fDebugRingSig)
    {
        // printf("verifyRingSignature() size %d\n", nRingSize); // happens often
    };

    int rv = 0;

    BN_CTX_start(bnCtx);

    BIGNUM*   bnT      = BN_CTX_get(bnCtx);
    BIGNUM*   bnH      = BN_CTX_get(bnCtx);
    BIGNUM*   bnC      = BN_CTX_get(bnCtx);
    BIGNUM*   bnR      = BN_CTX_get(bnCtx);
    BIGNUM*   bnSum    = BN_CTX_get(bnCtx);
    EC_POINT* ptT1     = NULL;
    EC_POINT* ptT2     = NULL;
    EC_POINT* ptT3     = NULL;
    EC_POINT* ptPk     = NULL;
    EC_POINT* ptKi     = NULL;
    EC_POINT* ptL      = NULL;
    EC_POINT* ptR      = NULL;

    uint8_t tempData[66]; // hold raw point data to hash
    uint256 commitHash;
    CHashWriter ssCommitHash(SER_GETHASH, PROTOCOL_VERSION);

    ssCommitHash << txnHash;

    // zero sum
    if (!bnSum || !(BN_zero(bnSum)))
    {
        printf("verifyRingSignature(): BN_zero failed.\n");
        rv = 1; goto End;
    };

    if (   !(ptT1 = EC_POINT_new(ecGrp))
        || !(ptT2 = EC_POINT_new(ecGrp))
        || !(ptT3 = EC_POINT_new(ecGrp))
        || !(ptPk = EC_POINT_new(ecGrp))
        || !(ptKi = EC_POINT_new(ecGrp))
        || !(ptL  = EC_POINT_new(ecGrp))
        || !(ptR  = EC_POINT_new(ecGrp)))
    {
        printf("verifyRingSignature(): EC_POINT_new failed.\n");
        rv = 1; goto End;
    };

    // get keyimage as point
    if (!(bnT = BN_bin2bn(&keyImage[0], ec_compressed_size, bnT))
        || !(ptKi) || !(ptKi = EC_POINT_bn2point(ecGrp, bnT, ptKi, bnCtx)))
    {
        printf("verifyRingSignature(): extract ptKi failed\n");
        rv = 1; goto End;
    };

    for (int i = 0; i < nRingSize; ++i)
    {
        // Li = ci * Pi + ri * G
        // Ri = ci * I + ri * Hp(Pi)

        if (!bnC || !(bnC = BN_bin2bn(&pSigc[i * ec_secret_size], ec_secret_size, bnC))
            || !bnR || !(bnR = BN_bin2bn(&pSigr[i * ec_secret_size], ec_secret_size, bnR)))
        {
            printf("verifyRingSignature(): extract bnC and bnR failed\n");
            rv = 1; goto End;
        };

        // get Pk i as point
        if (!(bnT = BN_bin2bn(&pPubkeys[i * ec_compressed_size], ec_compressed_size, bnT))
            || !(ptPk) || !(ptPk = EC_POINT_bn2point(ecGrp, bnT, ptPk, bnCtx)))
        {
            printf("verifyRingSignature(): extract ptPk failed\n");
            rv = 1; goto End;
        };

        // ptT1 = ci * Pi
        if (!EC_POINT_mul(ecGrp, ptT1, NULL, ptPk, bnC, bnCtx))
        {
            printf("verifyRingSignature(): EC_POINT_mul failed.\n");
            rv = 1; goto End;
        };

        // ptT2 = ri * G
        if (!EC_POINT_mul(ecGrp, ptT2, bnR, NULL, NULL, bnCtx))
        {
            printf("verifyRingSignature(): EC_POINT_mul failed.\n");
            rv = 1; goto End;
        };

        // ptL = ptT1 + ptT2
        if (!EC_POINT_add(ecGrp, ptL, ptT1, ptT2, bnCtx))
        {
            printf("verifyRingSignature(): EC_POINT_add failed.\n");
            rv = 1; goto End;
        };

        // ptT3 = Hp(Pi)
        if (hashToEC(&pPubkeys[i * ec_compressed_size], ec_compressed_size, bnT, ptT3) != 0)
        {
            printf("verifyRingSignature(): hashToEC failed.\n");
            rv = 1; goto End;
        };

        // ptT1 = k1 * I
        if (!EC_POINT_mul(ecGrp, ptT1, NULL, ptKi, bnC, bnCtx))
        {
            printf("verifyRingSignature(): EC_POINT_mul failed.\n");
            rv = 1; goto End;
        };

        // ptT2 = k2 * ptT3
        if (!EC_POINT_mul(ecGrp, ptT2, NULL, ptT3, bnR, bnCtx))
        {
            printf("verifyRingSignature(): EC_POINT_mul failed.\n");
            rv = 1; goto End;
        };

        // ptR = ptT1 + ptT2
        if (!EC_POINT_add(ecGrp, ptR, ptT1, ptT2, bnCtx))
        {
            printf("verifyRingSignature(): EC_POINT_add failed.\n");
            rv = 1; goto End;
        };

        // sum = (sum + ci) % N
        if (!BN_mod_add(bnSum, bnSum, bnC, bnN, bnCtx))
        {
            printf("verifyRingSignature(): BN_mod_add failed.\n");
            rv = 1; goto End;
        };

        // -- add ptL and ptR to hash
        if (!(EC_POINT_point2bn(ecGrp, ptL, POINT_CONVERSION_COMPRESSED, bnT, bnCtx))
            || BN_num_bytes(bnT) != (int) ec_compressed_size
            || BN_bn2bin(bnT, &tempData[0]) != (int) ec_compressed_size
            || !(EC_POINT_point2bn(ecGrp, ptR, POINT_CONVERSION_COMPRESSED, bnT, bnCtx))
            || BN_num_bytes(bnT) != (int) ec_compressed_size
            || BN_bn2bin(bnT, &tempData[33]) != (int) ec_compressed_size)
        {
            printf("extract ptL and ptR failed.\n");
            rv = 1; goto End;
        };

        ssCommitHash.write((const char*)&tempData[0], 66);
    };

    commitHash = ssCommitHash.GetHash();

    if (!(bnH) || !(bnH = BN_bin2bn(commitHash.begin(), ec_secret_size, bnH)))
    {
        printf("verifyRingSignature(): commitHash -> bnH failed\n");
        rv = 1; goto End;
    };

    if (!BN_mod(bnH, bnH, bnN, bnCtx))
    {
        printf("verifyRingSignature(): BN_mod failed.\n");
        rv = 1; goto End;
    };

    // bnT = (bnH - bnSum) % N
    if (!BN_mod_sub(bnT, bnH, bnSum, bnN, bnCtx))
    {
        printf("verifyRingSignature(): BN_mod_sub failed.\n");
        rv = 1; goto End;
    };

    // test bnT == 0  (bnSum == bnH)
    if (!BN_is_zero(bnT))
    {
        printf("verifyRingSignature(): signature does not verify.\n");
        rv = 2;
    };

    End:

    if (ptT1)
        EC_POINT_free(ptT1);
    if (ptT2)
        EC_POINT_free(ptT2);
    if (ptT3)
        EC_POINT_free(ptT3);
    if (ptPk)
        EC_POINT_free(ptPk);
    if (ptKi)
        EC_POINT_free(ptKi);
    if (ptL)
        EC_POINT_free(ptL);
    if (ptR)
        EC_POINT_free(ptR);

    BN_CTX_end(bnCtx);

    return rv;
};
