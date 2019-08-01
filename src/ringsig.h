// Copyright (c) 2018 Denarius developers
// Copyright (c) 2014 The ShadowCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef D_RINGSIG_H
#define D_RINGSIG_H

#include "stealth.h"
#include "types.h"

class CPubKey;

const uint32_t MIN_ANON_OUT_SIZE = 1 + 1 + 1 + 33 + 1 + 33; // OP_RETURN ANON_TOKEN lenPk pkTo lenR R [lenEnarr enarr]
const uint32_t MIN_ANON_IN_SIZE = 2 + (33 + 32 + 32); // 2byte marker (cpubkey + sigc + sigr)
const uint32_t MAX_ANON_NARRATION_SIZE = 48;
const uint32_t MIN_RING_SIZE = 5; // Minimum Ring Signature Size, Recommended at least 5 - D e n a r i u s
const uint32_t MAX_RING_SIZE_OLD = 200;
const uint32_t MAX_RING_SIZE = 32; // already overkill

const int MIN_ANON_SPEND_DEPTH = 10;
const int ANON_TXN_VERSION = 1000;

// MAX_MONEY = 200000000000000000; most complex possible value can be represented by 36 outputs

int initialiseRingSigs();
int finaliseRingSigs();

int splitAmount(int64_t nValue, std::vector<int64_t>& vOut);

int generateKeyImage(ec_point &publicKey, ec_secret secret, ec_point &keyImage);


int generateRingSignature(std::vector<uint8_t>& keyImage, uint256& txnHash, int nRingSize, int nSecretOffset, ec_secret secret, const uint8_t *pPubkeys, uint8_t *pSigc, uint8_t *pSigr);

int verifyRingSignature(std::vector<uint8_t>& keyImage, uint256& txnHash, int nRingSize, const uint8_t *pPubkeys, const uint8_t *pSigc, const uint8_t *pSigr);

#endif  // D_RINGSIG_H
