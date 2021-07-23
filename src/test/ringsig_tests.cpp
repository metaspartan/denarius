#include <boost/test/unit_test.hpp>

#include <boost/atomic.hpp>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

#include <ctime>

#include "../ringsig.h"
#include "../main.h"

#if BOOST_VERSION > 105300
#define BOOST_MESSAGE(msg) BOOST_TEST_MESSAGE(msg)
#endif

using namespace boost::chrono;

// test_shadow --log_level=all  --run_test=ringsig_tests

clock_t totalGenerate;
clock_t totalVerify;
clock_t start, stop;

void testRingSigs(int nRingSize)
{
    uint8_t *pPubkeys = (uint8_t*) malloc(sizeof(uint8_t) * EC_COMPRESSED_SIZE * nRingSize);
    uint8_t *pSigc    = (uint8_t*) malloc(sizeof(uint8_t) *     EC_SECRET_SIZE * nRingSize);
    uint8_t *pSigr    = (uint8_t*) malloc(sizeof(uint8_t) *     EC_SECRET_SIZE * nRingSize);

    BOOST_REQUIRE(NULL != pPubkeys);
    BOOST_REQUIRE(NULL != pSigc);
    BOOST_REQUIRE(NULL != pSigr);

    CKey key[nRingSize];
    for (int i = 0; i < nRingSize; ++i)
    {
        key[i].MakeNewKey(true);

        CPubKey pk = key[i].GetPubKey();

        memcpy(&pPubkeys[i * EC_COMPRESSED_SIZE], pk.begin(), EC_COMPRESSED_SIZE);
    };

    uint256 preimage;
    BOOST_CHECK(1 == RAND_bytes((uint8_t*) preimage.begin(), 32));
    //BOOST_MESSAGE("Txn preimage: " << HexStr(preimage));

    //BOOST_MESSAGE("nRingSize: " << nRingSize);
    int iSender = GetRandInt(nRingSize);
    //BOOST_MESSAGE("sender: " << iSender);

    ec_secret sSpend;
    ec_point pkSpend;
    ec_point keyImage;

    memcpy(&sSpend.e[0], key[iSender].begin(), EC_SECRET_SIZE);

    BOOST_REQUIRE(0 == SecretToPublicKey(sSpend, pkSpend));

    BOOST_REQUIRE(0 == generateKeyImage(pkSpend, sSpend, keyImage));

    start = clock();
    BOOST_REQUIRE(0 == generateRingSignature(keyImage, preimage, nRingSize, iSender, sSpend, pPubkeys, pSigc, pSigr));
    stop = clock();
    totalGenerate += stop - start;

    start = clock();
    BOOST_REQUIRE(0 == verifyRingSignature(keyImage, preimage, nRingSize, pPubkeys, pSigc, pSigr));
    stop = clock();
    totalVerify += stop - start;

    int sigSize = EC_COMPRESSED_SIZE + EC_SECRET_SIZE + (EC_SECRET_SIZE + EC_SECRET_SIZE + EC_COMPRESSED_SIZE) * nRingSize;

    BOOST_MESSAGE("nRingSize " << nRingSize << ", sigSize: " << bytesReadable(sigSize));

    if (pPubkeys)
        free(pPubkeys);
    if (pSigc)
        free(pSigc);
    if (pSigr)
        free(pSigr);
};

void testRingSigABs(int nRingSize)
{
    uint8_t *pPubkeys = (uint8_t*) malloc(sizeof(uint8_t) * EC_COMPRESSED_SIZE * nRingSize);
    uint8_t *pSigS    = (uint8_t*) malloc(sizeof(uint8_t) *     EC_SECRET_SIZE * nRingSize);

    BOOST_CHECK(NULL != pPubkeys);
    BOOST_CHECK(NULL != pSigS);

    CKey key[nRingSize];
    for (int i = 0; i < nRingSize; ++i)
    {
        key[i].MakeNewKey(true);

        CPubKey pk = key[i].GetPubKey();

        memcpy(&pPubkeys[i * EC_COMPRESSED_SIZE], pk.begin(), EC_COMPRESSED_SIZE);
    };

    uint256 preimage;
    BOOST_CHECK(1 == RAND_bytes((uint8_t*) preimage.begin(), 32));
    //BOOST_MESSAGE("Txn preimage: " << HexStr(preimage));

    int iSender = GetRandInt(nRingSize);
    //BOOST_MESSAGE("sender: " << iSender);

    ec_point pSigC;

    ec_secret sSpend;
    ec_point pkSpend;
    ec_point keyImage;

    memcpy(&sSpend.e[0], key[iSender].begin(), EC_SECRET_SIZE);

    BOOST_CHECK(0 == SecretToPublicKey(sSpend, pkSpend));

    BOOST_REQUIRE(0 == generateKeyImage(pkSpend, sSpend, keyImage));

    start = clock();
    BOOST_REQUIRE(0 == generateRingSignatureAB(keyImage, preimage, nRingSize, iSender, sSpend, pPubkeys, pSigC, pSigS));
    stop = clock();
    totalGenerate += stop - start;

    start = clock();
    BOOST_REQUIRE(0 == verifyRingSignatureAB(keyImage, preimage, nRingSize, pPubkeys, pSigC, pSigS));
    stop = clock();
    totalVerify += stop - start;

    int sigSize = EC_COMPRESSED_SIZE + EC_SECRET_SIZE + EC_SECRET_SIZE + (EC_SECRET_SIZE + EC_COMPRESSED_SIZE) * nRingSize;

    BOOST_MESSAGE("nRingSize " << nRingSize << ", sigSize: " << bytesReadable(sigSize));

    if (pPubkeys)
        free(pPubkeys);
    if (pSigS)
        free(pSigS);

};

BOOST_AUTO_TEST_SUITE(ringsig_tests)

BOOST_AUTO_TEST_CASE(ringsig)
{
    BOOST_REQUIRE(0 == initialiseRingSigs());

    BOOST_MESSAGE("testRingSigs");

    for (int k = 1; k < 4; ++k)
    {
        //BOOST_MESSAGE("ringSize " << (k % 126 + 2));
        testRingSigs(k % 126 + 2);
    };
    //testRingSigs(16);

    BOOST_MESSAGE("totalGenerate " << (double(totalGenerate) / CLOCKS_PER_SEC));
    BOOST_MESSAGE("totalVerify   " << (double(totalVerify)   / CLOCKS_PER_SEC));

    totalGenerate = 0;
    totalVerify = 0;
    BOOST_MESSAGE("testRingSigABs");

    for (int k = 0; k < 32; ++k)
    {
        //BOOST_MESSAGE("ringSize " << (k % 126 + 2));
        //testRingSigABs(k % 126 + 2);
    };
    //testRingSigABs(16);

    BOOST_MESSAGE("totalGenerate " << (double(totalGenerate) / CLOCKS_PER_SEC));
    BOOST_MESSAGE("totalVerify   " << (double(totalVerify)   / CLOCKS_PER_SEC));

    BOOST_CHECK(0 == finaliseRingSigs());
}

BOOST_AUTO_TEST_SUITE_END() 