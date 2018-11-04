// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2018 The Denarius developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DNR_CORE_H
#define DNR_CORE_H

#include "uint256.h"
#include "serialize.h"
#include "util.h"
#include "script.h"
#include "ringsig.h"

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <inttypes.h>

class CScript;
class CTransaction;

enum GetMinFee_mode
{
    GMF_BLOCK,
    GMF_RELAY,
    GMF_SEND,
    GMF_ANON,
};

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint
{
public:
    uint256 hash;
    unsigned int n;

    COutPoint() { SetNull(); }
    COutPoint(uint256 hashIn, unsigned int nIn) { hash = hashIn; n = nIn; }
    IMPLEMENT_SERIALIZE( READWRITE(FLATDATA(*this)); )
    void SetNull() { hash = 0; n = (unsigned int) -1; }
    bool IsNull() const { return (hash == 0 && n == (unsigned int) -1); }

    friend bool operator<(const COutPoint& a, const COutPoint& b)
    {
        return (a.hash < b.hash || (a.hash == b.hash && a.n < b.n));
    }

    friend bool operator==(const COutPoint& a, const COutPoint& b)
    {
        return (a.hash == b.hash && a.n == b.n);
    }

    friend bool operator!=(const COutPoint& a, const COutPoint& b)
    {
        return !(a == b);
    }

    std::string ToString() const
    {
        return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0,10).c_str(), n);
    }
};

/** An inpoint - a combination of a transaction and an index n into its vin */
class CInPoint
{
public:
    CTransaction* ptx;
    unsigned int n;

    CInPoint() { SetNull(); }
    CInPoint(CTransaction* ptxIn, unsigned int nIn) { ptx = ptxIn; n = nIn; }
    void SetNull() { ptx = NULL; n = (unsigned int) -1; }
    bool IsNull() const { return (ptx == NULL && n == (unsigned int) -1); }
};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    CScript prevPubKey;
    unsigned int nSequence;

    CTxIn()
    {
        nSequence = std::numeric_limits<unsigned int>::max();
    }

    explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn=CScript(), unsigned int nSequenceIn=std::numeric_limits<unsigned int>::max())
    {
        prevout = prevoutIn;
        scriptSig = scriptSigIn;
        nSequence = nSequenceIn;
    }

    explicit CTxIn(uint256 hashPrevTx, unsigned int nOut, CScript scriptSigIn=CScript(), unsigned int nSequenceIn=std::numeric_limits<unsigned int>::max())
    {
        prevout = COutPoint(hashPrevTx, nOut);
        scriptSig = scriptSigIn;
        nSequence = nSequenceIn;
    }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(prevout);
        READWRITE(scriptSig);
        READWRITE(nSequence);
    )

    bool IsFinal() const
    {
        return (nSequence == std::numeric_limits<unsigned int>::max());
    }

    bool IsAnonInput() const
    {
        return (scriptSig.size() >= 2 + (33 + 32 + 32) // 2byte marker (cpubkey + sigc + sigr)
            && scriptSig[0] == OP_RETURN
            && scriptSig[1] == OP_ANON_MARKER);
    }

    friend bool operator==(const CTxIn& a, const CTxIn& b)
    {
        return (a.prevout   == b.prevout &&
                a.scriptSig == b.scriptSig &&
                a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn& a, const CTxIn& b)
    {
        return !(a == b);
    }

    std::string ToString() const
    {
        std::string str;
        str += "CTxIn(";
        str += prevout.ToString();
        if (prevout.IsNull())
            str += strprintf(", coinbase %s", HexStr(scriptSig).c_str());
        else
            str += strprintf(", scriptSig=%s", scriptSig.ToString().substr(0,24).c_str());
        if (nSequence != std::numeric_limits<unsigned int>::max())
            str += strprintf(", nSequence=%u", nSequence);
        str += ")";
        return str;
    }

    void ExtractKeyImage(ec_point& kiOut) const
    {
        kiOut.resize(ec_compressed_size);
        memcpy(&kiOut[0], prevout.hash.begin(), 32);
        kiOut[32] = prevout.n & 0xFF;
    };

    int ExtractRingSize() const
    {
        return (prevout.n >> 16) & 0xFFFF;
    };
};




/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
    int64_t nValue;
    CScript scriptPubKey;

    CTxOut()
    {
        SetNull();
    }

    CTxOut(int64_t nValueIn, CScript scriptPubKeyIn)
    {
        nValue = nValueIn;
        scriptPubKey = scriptPubKeyIn;
    }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(nValue);
        READWRITE(scriptPubKey);
    )

    void SetNull()
    {
        nValue = -1;
        scriptPubKey.clear();
    }

    bool IsNull() const
    {
        return (nValue == -1);
    }

    void SetEmpty()
    {
        nValue = 0;
        scriptPubKey.clear();
    }

    bool IsEmpty() const
    {
        return (nValue == 0 && scriptPubKey.empty());
    }

    bool IsAnonOutput() const
    {
        return (scriptPubKey.size() >= MIN_ANON_OUT_SIZE
            && scriptPubKey[0] == OP_RETURN
            && scriptPubKey[1] == OP_ANON_MARKER);
    }

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    bool IsUnspendable() const
    {
        return IsEmpty() ||
               (scriptPubKey.size() > 0 && *scriptPubKey.begin() == OP_RETURN);
    }

    friend bool operator==(const CTxOut& a, const CTxOut& b)
    {
        return (a.nValue       == b.nValue &&
                a.scriptPubKey == b.scriptPubKey);
    }

    friend bool operator!=(const CTxOut& a, const CTxOut& b)
    {
        return !(a == b);
    }
    friend bool operator<(const CTxOut& a, const CTxOut& b)
    {
        return (a.nValue < b.nValue);
    }

    std::string ToString() const
    {
        if (IsEmpty()) return "CTxOut(empty)";
        return strprintf("CTxOut(nValue=%s, scriptPubKey=%s)", FormatMoney(nValue).c_str(), scriptPubKey.ToString().c_str());
    }

    CPubKey ExtractAnonPk() const
    {
        // always use IsAnonOutput to check length
        return CPubKey(&scriptPubKey[2+1], EC_COMPRESSED_SIZE);
    };
};

// D e n a r i u s - Anon

class CKeyImageSpent
{
// stored in txdb, key is keyimage
public:
    CKeyImageSpent() {};

    CKeyImageSpent(uint256& txnHash_, uint32_t inputNo_, int64_t nValue_)
    {
        txnHash = txnHash_;
        inputNo = inputNo_;
        nValue  = nValue_;
    };

    uint256 txnHash;    // hash of spending transaction
    uint32_t inputNo;   // keyimage is for inputNo of txnHash
    int64_t nValue;     // reporting only

    IMPLEMENT_SERIALIZE
    (
        READWRITE(txnHash);
        READWRITE(inputNo);
        READWRITE(nValue);
    )
};

class CAnonOutput
{
// stored in txdb, key is pubkey
public:

    CAnonOutput() {};

    CAnonOutput(COutPoint& outpoint_, int64_t nValue_, int nBlockHeight_, uint8_t nCompromised_)
    {
        outpoint = outpoint_;
        nValue = nValue_;
        nBlockHeight = nBlockHeight_;
        nCompromised = nCompromised_;
    };

    COutPoint outpoint;
    int64_t nValue;         // rather store 2 bytes, digit + power 10 ?
    int nBlockHeight;
    uint8_t nCompromised;   // TODO: mark if output can be identified (spent with ringsig 1)
    IMPLEMENT_SERIALIZE
    (
        READWRITE(outpoint);
        READWRITE(nValue);
        READWRITE(nBlockHeight);
        READWRITE(nCompromised);
    )
};

class CAnonOutputCount
{ // CountAllAnonOutputs
public:

    CAnonOutputCount()
    {
        nValue = 0;
        nExists = 0;
        nSpends = 0;
        nOwned = 0;
        nLeastDepth = 0;
    }

    CAnonOutputCount(int64_t nValue_, int nExists_, int nSpends_, int nOwned_, int nLeastDepth_)
    {
        nValue = nValue_;
        nExists = nExists_;
        nSpends = nSpends_;
        nOwned = nOwned_;
        nLeastDepth = nLeastDepth_;
    }

    void set(int64_t nValue_, int nExists_, int nSpends_, int nOwned_, int nLeastDepth_)
    {
        nValue = nValue_;
        nExists = nExists_;
        nSpends = nSpends_;
        nOwned = nOwned_;
        nLeastDepth = nLeastDepth_;
    }

    void addCoin(int nCoinDepth, int64_t nCoinValue)
    {
        nExists++;
        nValue = nCoinValue;
        if (nCoinDepth < nLeastDepth)
            nLeastDepth = nCoinDepth;
    }

    void updateDepth(int nCoinDepth, int64_t nCoinValue)
    {
        nValue = nCoinValue;
        if (nLeastDepth == 0
            || nCoinDepth < nLeastDepth)
            nLeastDepth = nCoinDepth;
    }

    void incSpends(int64_t nCoinValue)
    {
        nSpends++;
        nValue = nCoinValue;
    }

    void decSpends(int64_t nCoinValue)
    {
        nSpends--;
        nValue = nCoinValue;
    }

    void incExists(int64_t nCoinValue)
    {
        nExists++;
        nValue = nCoinValue;
    }

    void decExists(int64_t nCoinValue)
    {
        nExists--;
        nValue = nCoinValue;
    }


    int64_t nValue;
    int nExists;
    int nSpends;
    int nOwned; // todo
    int nLeastDepth;

};

#endif
