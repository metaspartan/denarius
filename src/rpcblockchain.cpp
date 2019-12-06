// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "denariusrpc.h"
#include "spork.h"
#include "init.h"
#include "txdb.h"
#include <errno.h>

#ifdef USE_IPFS
#include <ipfs/client.h>
#include <ipfs/http/transport.h>
#include <ipfs/test/utils.h>
#endif

using namespace json_spirit;
using namespace std;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry);
extern enum Checkpoints::CPMode CheckpointsMode;
extern void spj(const CScript& scriptPubKey, Object& out, bool fIncludeHex);

double BitsToDouble(unsigned int nBits)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    int nShift = (nBits >> 24) & 0xff;

    double dDiff = (double)0x0000ffff / (double)(nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    };
    
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    };

    return dDiff;
};

double GetDifficulty(const CBlockIndex* blockindex)
{
    if (blockindex == NULL)
    {
        if (pindexBest == NULL)
            return 1.0;
        else
            blockindex = GetLastBlockIndex(pindexBest, false);
    };

    return BitsToDouble(blockindex->nBits);
}

double GetHeaderDifficulty(const CBlockThinIndex* blockindex)
{
    if (blockindex == NULL)
    {
        if (pindexBest == NULL)
            return 1.0;
        else
            blockindex = GetLastBlockThinIndex(pindexBestHeader, false);
    };

    return BitsToDouble(blockindex->nBits);
}

double GetPoWMHashPS()
{
    if (pindexBest->nHeight >= LAST_POW_BLOCK)
        return 0;

    int nPoWInterval = 72;
    int64_t nTargetSpacingWorkMin = 30, nTargetSpacingWork = 30;

    CBlockIndex* pindex = pindexGenesisBlock;
    CBlockIndex* pindexPrevWork = pindexGenesisBlock;

    while (pindex)
    {
        if (pindex->IsProofOfWork())
        {
            int64_t nActualSpacingWork = pindex->GetBlockTime() - pindexPrevWork->GetBlockTime();
            nTargetSpacingWork = ((nPoWInterval - 1) * nTargetSpacingWork + nActualSpacingWork + nActualSpacingWork) / (nPoWInterval + 1);
            nTargetSpacingWork = max(nTargetSpacingWork, nTargetSpacingWorkMin);
            pindexPrevWork = pindex;
        }

        pindex = pindex->pnext;
    }

    return GetDifficulty() * 4294.967296 / nTargetSpacingWork;
}

double GetPoSKernelPS()
{
    int nPoSInterval = 72;
    double dStakeKernelsTriedAvg = 0;
    int nStakesHandled = 0, nStakesTime = 0;

    if (nNodeMode == NT_THIN)
    {
        CBlockThinIndex* pindex = pindexBestHeader;;
        CBlockThinIndex* pindexPrevStake = NULL;

        while (pindex && nStakesHandled < nPoSInterval)
        {
            if (pindex->IsProofOfStake())
            {
                dStakeKernelsTriedAvg += GetHeaderDifficulty(pindex) * 4294967296.0;
                nStakesTime += pindexPrevStake ? (pindexPrevStake->nTime - pindex->nTime) : 0;
                pindexPrevStake = pindex;
                nStakesHandled++;
            };

            pindex = pindex->pprev;
        };

    } else {

        CBlockIndex* pindex = pindexBest;;
        CBlockIndex* pindexPrevStake = NULL;

        while (pindex && nStakesHandled < nPoSInterval)
        {
            if (pindex->IsProofOfStake())
            {
                dStakeKernelsTriedAvg += GetDifficulty(pindex) * 4294967296.0;
                nStakesTime += pindexPrevStake ? (pindexPrevStake->nTime - pindex->nTime) : 0;
                pindexPrevStake = pindex;
                nStakesHandled++;
            };

            pindex = pindex->pprev;
        };

    }
    return nStakesTime ? dStakeKernelsTriedAvg / nStakesTime : 0;
}

Object blockHeaderToJSON(const CBlockThin& block, const CBlockThinIndex* blockindex)
{
    Object result;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    //CMerkleTx txGen(block.vtx[0]);
    //txGen.SetMerkleBranch(&block);
    //result.push_back(Pair("confirmations", (int)txGen.GetDepthInMainChain()));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    //result.push_back(Pair("mint", ValueFromAmount(blockindex->nMint)));
    result.push_back(Pair("time", (int64_t)block.GetBlockTime()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", HexBits(block.nBits)));
    result.push_back(Pair("difficulty", GetHeaderDifficulty(blockindex)));
    result.push_back(Pair("blocktrust", leftTrim(blockindex->GetBlockTrust().GetHex(), '0')));
    result.push_back(Pair("chaintrust", leftTrim(blockindex->nChainTrust.GetHex(), '0')));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    if (blockindex->pnext)
        result.push_back(Pair("nextblockhash", blockindex->pnext->GetBlockHash().GetHex()));

    result.push_back(Pair("flags", strprintf("%s%s", blockindex->IsProofOfStake()? "proof-of-stake" : "proof-of-work", blockindex->GeneratedStakeModifier()? " stake-modifier": "")));
    result.push_back(Pair("proofhash", blockindex->hashProof.GetHex()));
    result.push_back(Pair("entropybit", (int)blockindex->GetStakeEntropyBit()));
    result.push_back(Pair("modifier", strprintf("%016"PRIx64, blockindex->nStakeModifier)));
    result.push_back(Pair("modifierchecksum", strprintf("%08x", blockindex->nStakeModifierChecksum)));

    //if (block.IsProofOfStake())
    //    result.push_back(Pair("signature", HexStr(block.vchBlockSig.begin(), block.vchBlockSig.end())));

    return result;
}

Object diskBlockThinIndexToJSON(CDiskBlockThinIndex& diskBlock)
{
    CBlock block = diskBlock.GetBlock();

    Object result;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    //CMerkleTx txGen(block.vtx[0]);
    //txGen.SetMerkleBranch(&block);
    //result.push_back(Pair("confirmations", (int)txGen.GetDepthInMainChain()));
    //result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", diskBlock.nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    //result.push_back(Pair("mint", ValueFromAmount(blockindex->nMint)));
    result.push_back(Pair("time", (int64_t)block.GetBlockTime()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", HexBits(block.nBits)));
    result.push_back(Pair("difficulty", BitsToDouble(diskBlock.nBits)));
    result.push_back(Pair("blocktrust", leftTrim(diskBlock.GetBlockTrust().GetHex(), '0')));
    result.push_back(Pair("chaintrust", leftTrim(diskBlock.nChainTrust.GetHex(), '0')));

    result.push_back(Pair("previousblockhash", diskBlock.hashPrev.GetHex()));
    result.push_back(Pair("nextblockhash", diskBlock.hashNext.GetHex()));


    result.push_back(Pair("flags", strprintf("%s%s", diskBlock.IsProofOfStake()? "proof-of-stake" : "proof-of-work", diskBlock.GeneratedStakeModifier()? " stake-modifier": "")));
    result.push_back(Pair("proofhash", diskBlock.hashProof.GetHex()));
    result.push_back(Pair("entropybit", (int)diskBlock.GetStakeEntropyBit()));
    result.push_back(Pair("modifier", strprintf("%016"PRIx64, diskBlock.nStakeModifier)));
    //result.push_back(Pair("modifierchecksum", strprintf("%08x", diskBlock.nStakeModifierChecksum)));

    //if (block.IsProofOfStake())
    //    result.push_back(Pair("signature", HexStr(block.vchBlockSig.begin(), block.vchBlockSig.end())));

    return result;
}

Object blockHeader2ToJSON(const CBlock& block, const CBlockIndex* blockindex)
{
    Object result;
    result.push_back(Pair("version", block.nVersion));
    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    return result;
}

Object blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool fPrintTransactionDetail)
{
    Object result;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    CMerkleTx txGen(block.vtx[0]);
    txGen.SetMerkleBranch(&block);
    result.push_back(Pair("confirmations", (int)txGen.GetDepthInMainChain()));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(Pair("mint", ValueFromAmount(blockindex->nMint)));
    result.push_back(Pair("time", (int64_t)block.GetBlockTime()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", HexBits(block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("blocktrust", leftTrim(blockindex->GetBlockTrust().GetHex(), '0')));
    result.push_back(Pair("chaintrust", leftTrim(blockindex->nChainTrust.GetHex(), '0')));
    result.push_back(Pair("chainwork", leftTrim(blockindex->nChainWork.GetHex(), '0')));
    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    if (blockindex->pnext)
        result.push_back(Pair("nextblockhash", blockindex->pnext->GetBlockHash().GetHex()));

    result.push_back(Pair("flags", strprintf("%s%s", blockindex->IsProofOfStake()? "proof-of-stake" : "proof-of-work", blockindex->GeneratedStakeModifier()? " stake-modifier": "")));
    result.push_back(Pair("proofhash", blockindex->hashProof.GetHex()));
    result.push_back(Pair("entropybit", (int)blockindex->GetStakeEntropyBit()));
    result.push_back(Pair("modifier", strprintf("%016" PRIx64, blockindex->nStakeModifier)));
    result.push_back(Pair("modifierchecksum", strprintf("%08x", blockindex->nStakeModifierChecksum)));
    Array txinfo;
    BOOST_FOREACH (const CTransaction& tx, block.vtx)
    {
        if (fPrintTransactionDetail)
        {
            Object entry;

            entry.push_back(Pair("txid", tx.GetHash().GetHex()));
            TxToJSON(tx, 0, entry);

            txinfo.push_back(entry);
        }
        else
            txinfo.push_back(tx.GetHash().GetHex());
    }

    result.push_back(Pair("tx", txinfo));

    if (block.IsProofOfStake())
        result.push_back(Pair("signature", HexStr(block.vchBlockSig.begin(), block.vchBlockSig.end())));

    return result;
}

Value dumpbootstrap(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "dumpbootstrap \"destination\" \"blocks\"\n"
            "\nCreates a bootstrap format block dump of the blockchain in destination, which can be a directory or a path with filename, up to the given block number.");

    string strDest = params[0].get_str();
    int nBlocks = params[1].get_int();
    if (nBlocks < 0 || nBlocks > nBestHeight)
        throw runtime_error("Block number out of range.");

    boost::filesystem::path pathDest(strDest);
    if (boost::filesystem::is_directory(pathDest))
        pathDest /= "bootstrap.dat";

    try {
        FILE* file = fopen(pathDest.string().c_str(), "wb");
        if (!file)
            throw JSONRPCError(RPC_MISC_ERROR, "Error: Could not open bootstrap file for writing.");

        CAutoFile fileout = CAutoFile(file, SER_DISK, CLIENT_VERSION);
        if (!fileout)
            throw JSONRPCError(RPC_MISC_ERROR, "Error: Could not open bootstrap file for writing.");

        for (int nHeight = 0; nHeight <= nBlocks; nHeight++)
        {
            CBlock block;
            CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
            block.ReadFromDisk(pblockindex, true);
            fileout << FLATDATA(pchMessageStart) << fileout.GetSerializeSize(block) << block;
        }
    } catch(const boost::filesystem::filesystem_error &e) {
        throw JSONRPCError(RPC_MISC_ERROR, "Error: Bootstrap dump failed!");
    }

    return Value::null;
}

#ifdef USE_IPFS
Value jupitergetstat(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1)
        throw runtime_error(
            "jupitergetstat\n"
            "\nArguments:\n"
            "1. \"ipfshash\"          (string, required) The IPFS Hash/Block\n"
            "Returns the IPFS block stats of the inputted IPFS CID/Hash/Block");

    Object obj;
    std::string userHash = params[0].get_str();
    ipfs::Json stat_result;

    fJupiterLocal = GetBoolArg("-jupiterlocal");

    if (fJupiterLocal) {
        std::string ipfsip = GetArg("-jupiterip", "localhost:5001"); //Default Localhost

        ipfs::Client client(ipfsip);

        /* An example output:
        Stat: {"Key":"QmQpWo5TL9nivqvL18Bq8bS34eewAA6jcgdVsUu4tGeVHo","Size":15}
        */
        client.BlockStat(userHash, &stat_result);
        obj.push_back(Pair("key",        stat_result["Key"].dump().c_str()));
        obj.push_back(Pair("size",       stat_result["Size"].dump().c_str()));

        return obj;
    } else {
        ipfs::Client client("https://ipfs.infura.io:5001");

        /* An example output:
        Stat: {"Key":"QmQpWo5TL9nivqvL18Bq8bS34eewAA6jcgdVsUu4tGeVHo","Size":15}
        */
        client.BlockStat(userHash, &stat_result);
        obj.push_back(Pair("key",        stat_result["Key"].dump().c_str()));
        obj.push_back(Pair("size",       stat_result["Size"].dump().c_str()));

        return obj;
    }
}
Value jupitergetblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1)
        throw runtime_error(
            "jupitergetblock\n"
            "\nArguments:\n"
            "1. \"ipfshash\"          (string, required) The IPFS Hash/Block\n"
            "Returns the IPFS hash/block data hex of the inputted IPFS CID/Hash/Block");

    Object obj;
    std::string userHash = params[0].get_str();
    std::stringstream block_contents;

    fJupiterLocal = GetBoolArg("-jupiterlocal");

    if (fJupiterLocal) {
        std::string ipfsip = GetArg("-jupiterip", "localhost:5001"); //Default Localhost

        ipfs::Client client(ipfsip);

        client.BlockGet(userHash, &block_contents);
        obj.push_back(Pair("blockhex", ipfs::test::string_to_hex(block_contents.str()).c_str()));

        return obj;
    } else {
        ipfs::Client client("https://ipfs.infura.io:5001");

        /* E.g. userHash is "QmQpWo5TL9nivqvL18Bq8bS34eewAA6jcgdVsUu4tGeVHo". */
        client.BlockGet(userHash, &block_contents);
        obj.push_back(Pair("blockhex", ipfs::test::string_to_hex(block_contents.str()).c_str()));

        return obj;
        /* An example output:
        Block (hex): 426c6f636b2070757420746573742e
        */
    }
}
Value jupiterversion(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "jupiterversion\n"
            "Returns the version of the connected IPFS node within the Denarius Jupiter");

    ipfs::Json version;
    bool connected = false;
    Object obj;

    fJupiterLocal = GetBoolArg("-jupiterlocal");

    if (fJupiterLocal) {
        std::string ipfsip = GetArg("-jupiterip", "localhost:5001"); //Default Localhost

        ipfs::Client client(ipfsip);

        client.Version(&version);
        const std::string& vv = version["Version"].dump();
        printf("Jupiter: IPFS Peer Version: %s\n", vv.c_str());
        std::string versionj = version["Version"].dump();

        if (version["Version"].dump() != "") {
            connected = true;
        }
        
        obj.push_back(Pair("connected",          connected));
        obj.push_back(Pair("jupiterlocal",       "true"));
        obj.push_back(Pair("ipfspeer",           ipfsip));
        obj.push_back(Pair("ipfsversion",        version["Version"].dump().c_str()));

        return obj;
    } else {
        ipfs::Client client("https://ipfs.infura.io:5001");

        client.Version(&version);
        const std::string& vv = version["Version"].dump();
        printf("Jupiter: IPFS Peer Version: %s\n", vv.c_str());
        std::string versionj = version["Version"].dump();

        if (version["Version"].dump() != "") {
            connected = true;
        }
        
        obj.push_back(Pair("connected",          connected));
        obj.push_back(Pair("jupiterlocal",       "false"));
        obj.push_back(Pair("ipfspeer",           "https://ipfs.infura.io:5001"));
        obj.push_back(Pair("ipfsversion",        version["Version"].dump().c_str()));

        return obj;
    }
}

Value jupiterpod(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1)
    throw runtime_error(
        "jupiterpod\n"
        "\nArguments:\n"
        "1. \"filelocation\"          (string, required) The file location of the file to upload (e.g. /home/name/file.jpg)\n"
        "Returns the uploaded IPFS file CID/Hash of the uploaded file and public gateway link if successful, along with the Jupiter POD TX ID Timestamp.");

    Object obj;
    std::string userFile = params[0].get_str();


    //Ensure IPFS connected
    fJupiterLocal = GetBoolArg("-jupiterlocal");

    if (fJupiterLocal) {
        try {
            ipfs::Json add_result;

            std::string ipfsip = GetArg("-jupiterip", "localhost:5001"); //Default Localhost

            ipfs::Client client(ipfsip);

            if(userFile == "")
            { 
            return;
            }

            std::string filename = userFile.c_str();

            // Remove directory if present.
            // Do this before extension removal incase directory has a period character.
            const size_t last_slash_idx = filename.find_last_of("\\/");
            if (std::string::npos != last_slash_idx)
            {
                filename.erase(0, last_slash_idx + 1);
            }

            printf("Jupiter Upload File Start: %s\n", filename.c_str());
            //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

            client.FilesAdd(
            {{filename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
            &add_result);
            
            const std::string& hash = add_result[0]["hash"];
            int size = add_result[0]["size"];

            std::string r = add_result.dump();
            printf("Jupiter POD Successfully Added IPFS File(s): %s\n", r.c_str());

            //Jupiter POD
            if (hash != "") {
                //Hash the file for Denarius Jupiter POD
                //uint256 imagehash = SerializeHash(ipfsContents);
                CKeyID keyid(Hash160(hash.begin(), hash.end()));
                CBitcoinAddress baddr = CBitcoinAddress(keyid);
                std::string addr = baddr.ToString();

                //ui->lineEdit_2->setText(QString::fromStdString(addr));

                CAmount nAmount = 0.001 * COIN; // 0.001 D Fee
                
                // Wallet comments
                CWalletTx wtx;
                wtx.mapValue["comment"] = hash;
                std::string sNarr = "Jupiter POD";
                wtx.mapValue["to"]      = "Jupiter POD";
                
                if (pwalletMain->IsLocked())
                {
                    obj.push_back(Pair("error",  "Error, Your wallet is locked! Please unlock your wallet!"));
                    //ui->txLineEdit->setText("ERROR: Your wallet is locked! Cannot send Jupiter POD. Unlock your wallet!");
                } else if (pwalletMain->GetBalance() < 0.001) {
                    obj.push_back(Pair("error",  "Error, You need at least 0.001 D to send Jupiter POD!"));
                    //ui->txLineEdit->setText("ERROR: You need at least a 0.001 D balance to send Jupiter POD.");
                } else {          
                    //std::string sNarr;
                    std::string strError = pwalletMain->SendMoneyToDestination(baddr.Get(), nAmount, sNarr, wtx);

                    if(strError != "")
                    {
                        obj.push_back(Pair("error",  strError.c_str()));
                    }

                    //ui->lineEdit_3->setText(QString::fromStdString(wtx.GetHash().GetHex()));

                    std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
                    std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
                    std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

                    obj.push_back(Pair("filename",           filename.c_str()));
                    obj.push_back(Pair("sizebytes",          size));
                    obj.push_back(Pair("ipfshash",           hash));
                    obj.push_back(Pair("infuralink",         filelink));
                    obj.push_back(Pair("cflink",             cloudlink));
                    obj.push_back(Pair("ipfslink",           ipfsoglink));
                    obj.push_back(Pair("podaddress",         addr.c_str()));
                    obj.push_back(Pair("podtxid",            wtx.GetHash().GetHex()));
                }
            }

            /*
            std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
            std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;

            obj.push_back(Pair("filename",           filename.c_str()));
            obj.push_back(Pair("sizebytes",          size));
            obj.push_back(Pair("ipfshash",           hash));
            obj.push_back(Pair("infuralink",         filelink));
            obj.push_back(Pair("cflink",             cloudlink));
            */

            } catch (const std::exception& e) {
                std::cerr << e.what() << std::endl; //302 error on large files: passing null and throwing exception
                obj.push_back(Pair("error",          e.what()));
            }

            return obj;
        } else {
            try {
                ipfs::Json add_result;
                ipfs::Client client("https://ipfs.infura.io:5001");

                if(userFile == "")
                { 
                return;
                }

                std::string filename = userFile.c_str();

                // Remove directory if present.
                // Do this before extension removal incase directory has a period character.
                const size_t last_slash_idx = filename.find_last_of("\\/");
                if (std::string::npos != last_slash_idx)
                {
                    filename.erase(0, last_slash_idx + 1);
                }

                printf("Jupiter Upload File Start: %s\n", filename.c_str());
                //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

                client.FilesAdd(
                {{filename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
                &add_result);
                
                const std::string& hash = add_result[0]["hash"];
                int size = add_result[0]["size"];

                std::string r = add_result.dump();
                printf("Jupiter POD Successfully Added IPFS File(s): %s\n", r.c_str());

                //Jupiter POD
                if (hash != "") {
                    //Hash the file for Denarius Jupiter POD
                    //uint256 imagehash = SerializeHash(ipfsContents);
                    CKeyID keyid(Hash160(hash.begin(), hash.end()));
                    CBitcoinAddress baddr = CBitcoinAddress(keyid);
                    std::string addr = baddr.ToString();

                    //ui->lineEdit_2->setText(QString::fromStdString(addr));

                    CAmount nAmount = 0.001 * COIN; // 0.001 D Fee
                    
                    // Wallet comments
                    CWalletTx wtx;
                    wtx.mapValue["comment"] = hash;
                    std::string sNarr = "Jupiter POD";
                    wtx.mapValue["to"]      = "Jupiter POD";
                    
                    if (pwalletMain->IsLocked())
                    {
                        obj.push_back(Pair("error",  "Error, Your wallet is locked! Please unlock your wallet!"));
                        //ui->txLineEdit->setText("ERROR: Your wallet is locked! Cannot send Jupiter POD. Unlock your wallet!");
                    } else if (pwalletMain->GetBalance() < 0.001) {
                        obj.push_back(Pair("error",  "Error, You need at least 0.001 D to send Jupiter POD!"));
                        //ui->txLineEdit->setText("ERROR: You need at least a 0.001 D balance to send Jupiter POD.");
                    } else {          
                        //std::string sNarr;
                        std::string strError = pwalletMain->SendMoneyToDestination(baddr.Get(), nAmount, sNarr, wtx);

                        if(strError != "")
                        {
                            obj.push_back(Pair("error",  strError.c_str()));
                        }

                        //ui->lineEdit_3->setText(QString::fromStdString(wtx.GetHash().GetHex()));

                        std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
                        std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
                        std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

                        obj.push_back(Pair("filename",           filename.c_str()));
                        obj.push_back(Pair("sizebytes",          size));
                        obj.push_back(Pair("ipfshash",           hash));
                        obj.push_back(Pair("infuralink",         filelink));
                        obj.push_back(Pair("cflink",             cloudlink));
                        obj.push_back(Pair("ipfslink",           ipfsoglink));
                        obj.push_back(Pair("podaddress",         addr.c_str()));
                        obj.push_back(Pair("podtxid",            wtx.GetHash().GetHex()));
                    }
                }
                
                } catch (const std::exception& e) {
                std::cerr << e.what() << std::endl; //302 error on large files: passing null and throwing exception
                obj.push_back(Pair("error",          e.what()));
            }

            return obj;
        }
}

Value jupiterupload(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1)
    throw runtime_error(
        "jupiterupload\n"
        "\nArguments:\n"
        "1. \"filelocation\"          (string, required) The file location of the file to upload (e.g. /home/name/file.jpg)\n"
        "Returns the uploaded IPFS file CID/Hash of the uploaded file and public gateway link if successful.");

    Object obj;
    std::string userFile = params[0].get_str();


    //Ensure IPFS connected
    fJupiterLocal = GetBoolArg("-jupiterlocal");

    if (fJupiterLocal) {
        try {
            ipfs::Json add_result;

            std::string ipfsip = GetArg("-jupiterip", "localhost:5001"); //Default Localhost

            ipfs::Client client(ipfsip);

            if(userFile == "")
            { 
            return;
            }

            std::string filename = userFile.c_str();

            // Remove directory if present.
            // Do this before extension removal incase directory has a period character.
            const size_t last_slash_idx = filename.find_last_of("\\/");
            if (std::string::npos != last_slash_idx)
            {
                filename.erase(0, last_slash_idx + 1);
            }

            printf("Jupiter Upload File Start: %s\n", filename.c_str());
            //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

            client.FilesAdd(
            {{filename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
            &add_result);
            
            const std::string& hash = add_result[0]["hash"];
            int size = add_result[0]["size"];

            std::string r = add_result.dump();
            printf("Jupiter Successfully Added IPFS File(s): %s\n", r.c_str());

            std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
            std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
            std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

            obj.push_back(Pair("filename",           filename.c_str()));
            obj.push_back(Pair("sizebytes",          size));
            obj.push_back(Pair("ipfshash",           hash));
            obj.push_back(Pair("infuralink",         filelink));
            obj.push_back(Pair("cflink",             cloudlink));
            obj.push_back(Pair("ipfslink",           ipfsoglink));
            
            } catch (const std::exception& e) {
                std::cerr << e.what() << std::endl; //302 error on large files: passing null and throwing exception
                obj.push_back(Pair("error",          e.what()));
            }

            return obj;
        } else {
            try {
                ipfs::Json add_result;
                ipfs::Client client("https://ipfs.infura.io:5001");

                if(userFile == "")
                { 
                return;
                }

                std::string filename = userFile.c_str();

                // Remove directory if present.
                // Do this before extension removal incase directory has a period character.
                const size_t last_slash_idx = filename.find_last_of("\\/");
                if (std::string::npos != last_slash_idx)
                {
                    filename.erase(0, last_slash_idx + 1);
                }

                printf("Jupiter Upload File Start: %s\n", filename.c_str());
                //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

                client.FilesAdd(
                {{filename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
                &add_result);
                
                const std::string& hash = add_result[0]["hash"];
                int size = add_result[0]["size"];

                std::string r = add_result.dump();
                printf("Jupiter Successfully Added IPFS File(s): %s\n", r.c_str());

                std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
                std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
                std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

                obj.push_back(Pair("filename",           filename.c_str()));
                obj.push_back(Pair("sizebytes",          size));
                obj.push_back(Pair("ipfshash",           hash));
                obj.push_back(Pair("infuralink",         filelink));
                obj.push_back(Pair("cflink",             cloudlink));
                obj.push_back(Pair("ipfslink",           ipfsoglink));
                
                } catch (const std::exception& e) {
                std::cerr << e.what() << std::endl; //302 error on large files: passing null and throwing exception
                obj.push_back(Pair("error",          e.what()));
            }

            return obj;
        }

        /*     ￼
        jupiterupload C:/users/NAME/Dropbox/Denarius/denarius-128.png
        15:45:55        ￼
        {
        "filename" : "denarius-128.png",
        "results" : "[{\"hash\":\"QmYKi7A9PyqywRA4aBWmqgSCYrXgRzri2QF25JKzBMjCxT\",\"path\":\"denarius-128.png\",\"size\":47555}]",
        "ipfshash" : "QmYKi7A9PyqywRA4aBWmqgSCYrXgRzri2QF25JKzBMjCxT",
        "ipfslink" : "https://ipfs.infura.io/ipfs/QmYKi7A9PyqywRA4aBWmqgSCYrXgRzri2QF25JKzBMjCxT"
        }
        */
}

Value jupiterduo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1)
    throw runtime_error(
        "jupiterduo\n"
        "\nArguments:\n"
        "1. \"filelocation\"          (string, required) The file location of the file to upload (e.g. /home/name/file.jpg)\n"
        "Returns the uploaded IPFS file CID/Hashes of the uploaded file and public gateway links if successful from submission to two IPFS API nodes.");

    Object obj, second, first;
    std::string userFile = params[0].get_str();


    //Ensure IPFS connected
    fJupiterLocal = GetBoolArg("-jupiterlocal");

    if (fJupiterLocal) {
        try {
            ipfs::Json add_result;

            std::string ipfsip = GetArg("-jupiterip", "localhost:5001"); //Default Localhost

            ipfs::Client client(ipfsip);

            if(userFile == "")
            { 
            return;
            }

            std::string filename = userFile.c_str();

            // Remove directory if present.
            // Do this before extension removal incase directory has a period character.
            const size_t last_slash_idx = filename.find_last_of("\\/");
            if (std::string::npos != last_slash_idx)
            {
                filename.erase(0, last_slash_idx + 1);
            }

            printf("Jupiter Upload File Start: %s\n", filename.c_str());
            //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

            client.FilesAdd(
            {{filename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
            &add_result);
            
            const std::string& hash = add_result[0]["hash"];
            int size = add_result[0]["size"];

            std::string r = add_result.dump();
            printf("Jupiter Duo Successfully Added IPFS File(s): %s\n", r.c_str());

            std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
            std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
            std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

            obj.push_back(Pair("duoupload",          "true"));

            first.push_back(Pair("nodeip",             ipfsip));
            first.push_back(Pair("filename",           filename.c_str()));
            first.push_back(Pair("sizebytes",          size));
            first.push_back(Pair("ipfshash",           hash));
            first.push_back(Pair("infuralink",         filelink));
            first.push_back(Pair("cflink",             cloudlink));
            first.push_back(Pair("ipfslink",           ipfsoglink));
            obj.push_back(Pair("first",                first));
            
            } catch (const std::exception& e) {
                std::cerr << e.what() << std::endl; //302 error on large files: passing null and throwing exception
                obj.push_back(Pair("error",          e.what()));
            }

            try {
                ipfs::Json add_result;
                ipfs::Client client("https://ipfs.infura.io:5001");

                if(userFile == "")
                { 
                return;
                }

                std::string filename = userFile.c_str();

                // Remove directory if present.
                // Do this before extension removal incase directory has a period character.
                const size_t last_slash_idx = filename.find_last_of("\\/");
                if (std::string::npos != last_slash_idx)
                {
                    filename.erase(0, last_slash_idx + 1);
                }

                printf("Jupiter Upload File Start: %s\n", filename.c_str());
                //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

                client.FilesAdd(
                {{filename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
                &add_result);
                
                const std::string& hash = add_result[0]["hash"];
                int size = add_result[0]["size"];

                std::string r = add_result.dump();
                printf("Jupiter Duo Successfully Added IPFS File(s): %s\n", r.c_str());

                std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
                std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
                std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

                second.push_back(Pair("nodeip",             "https://ipfs.infura.io:5001"));
                second.push_back(Pair("filename",           filename.c_str()));
                second.push_back(Pair("sizebytes",          size));
                second.push_back(Pair("ipfshash",           hash));
                second.push_back(Pair("infuralink",         filelink));
                second.push_back(Pair("cflink",             cloudlink));
                second.push_back(Pair("ipfslink",           ipfsoglink));
                obj.push_back(Pair("second",                second));
                
                } catch (const std::exception& e) {
                std::cerr << e.what() << std::endl; //302 error on large files: passing null and throwing exception
                obj.push_back(Pair("error",          e.what()));
            }

            return obj;


        } else {
            obj.push_back(Pair("error",          "jupiterduo is only available with -jupiterlocal=1"));
            return obj;
        }

        /*     ￼
        jupiterupload C:/users/NAME/Dropbox/Denarius/denarius-128.png
        15:45:55        ￼
        {
        "filename" : "denarius-128.png",
        "results" : "[{\"hash\":\"QmYKi7A9PyqywRA4aBWmqgSCYrXgRzri2QF25JKzBMjCxT\",\"path\":\"denarius-128.png\",\"size\":47555}]",
        "ipfshash" : "QmYKi7A9PyqywRA4aBWmqgSCYrXgRzri2QF25JKzBMjCxT",
        "ipfslink" : "https://ipfs.infura.io/ipfs/QmYKi7A9PyqywRA4aBWmqgSCYrXgRzri2QF25JKzBMjCxT"
        }
        */
}

Value jupiterduopod(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1)
    throw runtime_error(
        "jupiterduopod\n"
        "\nArguments:\n"
        "1. \"filelocation\"          (string, required) The file location of the file to upload (e.g. /home/name/file.jpg)\n"
        "Returns the uploaded IPFS file CID/Hashes of the uploaded file and public gateway links if successful from submission to two IPFS API nodes and PODs with D");

    Object obj, second, first;
    std::string userFile = params[0].get_str();


    //Ensure IPFS connected
    fJupiterLocal = GetBoolArg("-jupiterlocal");

    if (fJupiterLocal) {
        try {
            ipfs::Json add_result;

            std::string ipfsip = GetArg("-jupiterip", "localhost:5001"); //Default Localhost

            ipfs::Client client(ipfsip);

            if(userFile == "")
            { 
            return;
            }

            std::string filename = userFile.c_str();

            // Remove directory if present.
            // Do this before extension removal incase directory has a period character.
            const size_t last_slash_idx = filename.find_last_of("\\/");
            if (std::string::npos != last_slash_idx)
            {
                filename.erase(0, last_slash_idx + 1);
            }

            printf("Jupiter Upload File Start: %s\n", filename.c_str());
            //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

            client.FilesAdd(
            {{filename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
            &add_result);
            
            const std::string& hash = add_result[0]["hash"];
            int size = add_result[0]["size"];

            std::string r = add_result.dump();
            printf("Jupiter Duo Pod Successfully Added IPFS File(s): %s\n", r.c_str());

            //Jupiter POD for Duo
            if (hash != "") {
                //Hash the file for Denarius Jupiter POD
                //uint256 imagehash = SerializeHash(ipfsContents);
                CKeyID keyid(Hash160(hash.begin(), hash.end()));
                CBitcoinAddress baddr = CBitcoinAddress(keyid);
                std::string addr = baddr.ToString();

                //ui->lineEdit_2->setText(QString::fromStdString(addr));

                CAmount nAmount = 0.0005 * COIN; // 0.0005 D Fee - 0.001 D Total for Jupiter Duo Pod
                
                // Wallet comments
                CWalletTx wtx;
                wtx.mapValue["comment"] = hash;
                std::string sNarr = "Jupiter Duo POD";
                wtx.mapValue["to"]      = "Jupiter Duo POD";
                
                if (pwalletMain->IsLocked())
                {
                    obj.push_back(Pair("error",  "Error, Your wallet is locked! Please unlock your wallet!"));
                    //ui->txLineEdit->setText("ERROR: Your wallet is locked! Cannot send Jupiter POD. Unlock your wallet!");
                } else if (pwalletMain->GetBalance() < 0.001) {
                    obj.push_back(Pair("error",  "Error, You need at least 0.001 D to send Jupiter POD!"));
                    //ui->txLineEdit->setText("ERROR: You need at least a 0.001 D balance to send Jupiter POD.");
                } else {          
                    //std::string sNarr;
                    std::string strError = pwalletMain->SendMoneyToDestination(baddr.Get(), nAmount, sNarr, wtx);

                    if(strError != "")
                    {
                        obj.push_back(Pair("error",  strError.c_str()));
                    }

                    std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
                    std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
                    std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

                    obj.push_back(Pair("duoupload",          "true"));

                    first.push_back(Pair("nodeip",             ipfsip));
                    first.push_back(Pair("filename",           filename.c_str()));
                    first.push_back(Pair("sizebytes",          size));
                    first.push_back(Pair("ipfshash",           hash));
                    first.push_back(Pair("infuralink",         filelink));
                    first.push_back(Pair("cflink",             cloudlink));
                    first.push_back(Pair("ipfslink",           ipfsoglink));
                    first.push_back(Pair("podaddress",         addr.c_str()));
                    first.push_back(Pair("podtxid",            wtx.GetHash().GetHex()));
                    obj.push_back(Pair("first",                first));

                }
            }
            
            } catch (const std::exception& e) {
                std::cerr << e.what() << std::endl; //302 error on large files: passing null and throwing exception
                obj.push_back(Pair("error",          e.what()));
            }

            try {
                ipfs::Json add_result;
                ipfs::Client client("https://ipfs.infura.io:5001");

                if(userFile == "")
                { 
                return;
                }

                std::string filename = userFile.c_str();

                // Remove directory if present.
                // Do this before extension removal incase directory has a period character.
                const size_t last_slash_idx = filename.find_last_of("\\/");
                if (std::string::npos != last_slash_idx)
                {
                    filename.erase(0, last_slash_idx + 1);
                }

                printf("Jupiter Upload File Start: %s\n", filename.c_str());
                //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

                client.FilesAdd(
                {{filename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
                &add_result);
                
                const std::string& hash = add_result[0]["hash"];
                int size = add_result[0]["size"];

                std::string r = add_result.dump();
                printf("Jupiter Duo POD Successfully Added IPFS File(s): %s\n", r.c_str());

                //Jupiter POD for Duo
                if (hash != "") {
                    //Hash the file for Denarius Jupiter POD
                    //uint256 imagehash = SerializeHash(ipfsContents);
                    CKeyID keyid(Hash160(hash.begin(), hash.end()));
                    CBitcoinAddress baddr = CBitcoinAddress(keyid);
                    std::string addr = baddr.ToString();

                    //ui->lineEdit_2->setText(QString::fromStdString(addr));

                    CAmount nAmount = 0.0005 * COIN; // 0.0005 D Fee - 0.001 D Total for Jupiter Duo Pod
                    
                    // Wallet comments
                    CWalletTx wtx;
                    wtx.mapValue["comment"] = hash;
                    std::string sNarr = "Jupiter Duo POD";
                    wtx.mapValue["to"]      = "Jupiter Duo POD";
                    
                    if (pwalletMain->IsLocked())
                    {
                        obj.push_back(Pair("error",  "Error, Your wallet is locked! Please unlock your wallet!"));
                        //ui->txLineEdit->setText("ERROR: Your wallet is locked! Cannot send Jupiter POD. Unlock your wallet!");
                    } else if (pwalletMain->GetBalance() < 0.001) {
                        obj.push_back(Pair("error",  "Error, You need at least 0.001 D to send Jupiter POD!"));
                        //ui->txLineEdit->setText("ERROR: You need at least a 0.001 D balance to send Jupiter POD.");
                    } else {          
                        //std::string sNarr;
                        std::string strError = pwalletMain->SendMoneyToDestination(baddr.Get(), nAmount, sNarr, wtx);

                        if(strError != "")
                        {
                            obj.push_back(Pair("error",  strError.c_str()));
                        }

                        std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
                        std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
                        std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

                        second.push_back(Pair("nodeip",             "https://ipfs.infura.io:5001"));
                        second.push_back(Pair("filename",           filename.c_str()));
                        second.push_back(Pair("sizebytes",          size));
                        second.push_back(Pair("ipfshash",           hash));
                        second.push_back(Pair("infuralink",         filelink));
                        second.push_back(Pair("cflink",             cloudlink));
                        second.push_back(Pair("ipfslink",           ipfsoglink));
                        second.push_back(Pair("podaddress",         addr.c_str()));
                        second.push_back(Pair("podtxid",            wtx.GetHash().GetHex()));
                        obj.push_back(Pair("second",                second));

                    }
                }
                
                } catch (const std::exception& e) {
                std::cerr << e.what() << std::endl; //302 error on large files: passing null and throwing exception
                obj.push_back(Pair("error",          e.what()));
            }

            return obj;


        } else {
            obj.push_back(Pair("error",          "jupiterduopod is only available with -jupiterlocal=1"));
            return obj;
        }
}
#endif

Value getbestblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbestblockhash\n"
            "Returns the hash of the best block in the longest block chain.");

    return hashBestChain.GetHex();
}

Value getblockcount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "Returns the number of blocks in the longest block chain.");

    return nBestHeight;
}


Value getdifficulty(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "Returns the difficulty as a multiple of the minimum difficulty.");

    Object obj;
    obj.push_back(Pair("proof-of-work",        GetDifficulty()));
    obj.push_back(Pair("proof-of-stake",       GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    obj.push_back(Pair("search-interval",      (int)nLastCoinStakeSearchInterval));
    return obj;
}


Value settxfee(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1 || AmountFromValue(params[0]) < MIN_TX_FEE)
        throw runtime_error(
            "settxfee <amount>\n"
            "<amount> is a real and is rounded to the nearest 0.01");

    nTransactionFee = AmountFromValue(params[0]);
    nTransactionFee = (nTransactionFee / CENT) * CENT;  // round to cent

    return true;
}

Value getrawmempool(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getrawmempool\n"
            "Returns all transaction ids in memory pool.");

    vector<uint256> vtxid;
    mempool.queryHashes(vtxid);

    Array a;
    BOOST_FOREACH(const uint256& hash, vtxid)
        a.push_back(hash.ToString());

    return a;
}

Value getblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash <index>\n"
            "Returns hash of block in best-block-chain at <index>.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
    return pblockindex->phashBlock->GetHex();
}

//New getblock RPC Command for Denariium Compatibility
Value getblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock \"blockhash\" ( verbosity ) \n"
            "\nIf verbosity is 0, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbosity is 1, returns an Object with information about block <hash>.\n"
            "If verbosity is 2, returns an Object with information about block <hash> and information about each transaction. \n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash\n"
            "2. verbosity              (numeric, optional, default=1) 0 for hex encoded data, 1 for a json object, and 2 for json object with transaction data\n"
            "\nResult (for verbosity = 0):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nResult (for verbosity = 1):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"strippedsize\" : n,    (numeric) The block size excluding witness data\n"
            "  \"weight\" : n           (numeric) The block weight as defined in BIP 141\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbosity = 2):\n"
            "{\n"
            "  ...,                     Same output as verbosity = 1.\n"
            "  \"tx\" : [               (array of Objects) The transactions in the format of the getrawtransaction RPC. Different from verbosity = 1 \"tx\" result.\n"
            "         ,...\n"
            "  ],\n"
            "  ,...                     Same output as verbosity = 1.\n"
            "}\n"
            "\nExamples:\n"
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(strHash);
    //std::string strHash = params[0].get_str();
	//uint256 hash(uint256S(strHash));

    int verbosity = 1;
    if (params.size() > 1) {
            verbosity = params[1].get_bool() ? 1 : 0;
    }

    if (nNodeMode == NT_THIN)
    {
        CDiskBlockThinIndex diskindex;
        CTxDB txdb("r");
        if (txdb.ReadBlockThinIndex(hash, diskindex))
            return diskBlockThinIndexToJSON(diskindex);

        throw runtime_error("Read header from db failed.\n");
    };

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
	
	if(!block.ReadFromDisk(pblockindex, true)){
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the
        // block).
		throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");
	}

	block.ReadFromDisk(pblockindex, true);
	
    if (verbosity <= 0)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
		//strHex.insert(0, "testar ");
        return strHex;
    }

    //return blockToJSON(block, pblockindex, verbosity >= 2);
	return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

Value getblockheader(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for block 'hash' header.\n"
            "If verbose is true, returns an Object with information about block <hash> header.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash' header.\n"
            "\nExamples:\n"
            );

    std::string strHash = params[0].get_str();
    uint256 hash(strHash);

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (nNodeMode == NT_THIN)
    {
        CDiskBlockThinIndex diskindex;
        CTxDB txdb("r");
        if (txdb.ReadBlockThinIndex(hash, diskindex))
            return diskBlockThinIndexToJSON(diskindex);

        throw runtime_error("Read header from db failed.\n");
    };

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

	if(!block.ReadFromDisk(pblockindex, true)){
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the
        // block).
		throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");
	}

	block.ReadFromDisk(pblockindex, true);

    if (!fVerbose) {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockHeader2ToJSON(block, pblockindex);
}

//Old getblock RPC Command, Not deprecated
Value getblock_old(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock <hash> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-hash.");

    std::string strHash = params[0].get_str();
    uint256 hash(strHash);

    if (nNodeMode == NT_THIN)
    {
        CDiskBlockThinIndex diskindex;
        CTxDB txdb("r");
        if (txdb.ReadBlockThinIndex(hash, diskindex))
            return diskBlockThinIndexToJSON(diskindex);

        throw runtime_error("Read header from db failed.\n");
    };

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

Value getblockbynumber(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockbynumber <number> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-number.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    if (nNodeMode == NT_THIN)
    {
        if (!fThinFullIndex
            && pindexRear
            && nHeight < pindexRear->nHeight)
        {
            CDiskBlockThinIndex diskindex;
            uint256 hashPrev = pindexRear->GetBlockHash();

            // -- find closest checkpoint
            Checkpoints::MapCheckpoints& checkpoints = (fTestNet ? Checkpoints::mapCheckpointsTestnet : Checkpoints::mapCheckpoints);
            Checkpoints::MapCheckpoints::reverse_iterator rit;

            for (rit = checkpoints.rbegin(); rit != checkpoints.rend(); ++rit)
            {
                if (rit->first < nHeight)
                    break;
                hashPrev = rit->second;
            };

            CTxDB txdb("r");
            while (hashPrev != 0)
            {
                if (!txdb.ReadBlockThinIndex(hashPrev, diskindex))
                    throw runtime_error("Read header from db failed.\n");

                if (diskindex.nHeight == nHeightFilteredNeeded)
                    return diskBlockThinIndexToJSON(diskindex);

                hashPrev = diskindex.hashPrev;
            };

            throw runtime_error("block not found.");
        };


        CBlockThin block;
        std::map<uint256, CBlockThinIndex*>::iterator mi = mapBlockThinIndex.find(hashBestChain);
        if (mi != mapBlockThinIndex.end())
        {
            CBlockThinIndex* pblockindex = mi->second;
            while (pblockindex->pprev && pblockindex->nHeight > nHeight)
                pblockindex = pblockindex->pprev;

            if (nHeight != pblockindex->nHeight)
            {
                throw runtime_error("block not in chain index.");
            }
            return blockHeaderToJSON(block, pblockindex);
        } else
        {
            throw runtime_error("hashBestChain not in chain index.");
        }


    };

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;

    uint256 hash = *pblockindex->phashBlock;

    pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

Value setbestblockbyheight(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setbestblockbyheight <height>\n"
            "Sets the tip of the chain with a block at <height>.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block height out of range.");

    if (nNodeMode == NT_THIN)
    {
        throw runtime_error("Must be in full mode.");
    };

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;

    uint256 hash = *pblockindex->phashBlock;

    pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);


    Object result;

    CTxDB txdb;
    {
        LOCK(cs_main);

        if (!block.SetBestChain(txdb, pblockindex))
            result.push_back(Pair("result", "failure"));
        else
            result.push_back(Pair("result", "success"));

    };

    return result;
}

Value thinscanmerkleblocks(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "thinscanmerkleblocks <height>\n"
            "Request and rescan merkle blocks from peers starting from <height>.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block height out of range.");

    if (nNodeMode != NT_THIN)
        throw runtime_error("Must be in thin mode.");

    if (nNodeState == NS_GET_FILTERED_BLOCKS)
        throw runtime_error("Wait for current merkle block scan to complete.");

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        pwalletMain->nLastFilteredHeight = nHeight;
        nHeightFilteredNeeded = nHeight;
        CWalletDB walletdb(pwalletMain->strWalletFile);
        walletdb.WriteLastFilteredHeight(nHeight);

        ChangeNodeState(NS_GET_FILTERED_BLOCKS, false);
    }

    Object result;
    result.push_back(Pair("result", "Success."));
    result.push_back(Pair("startheight", nHeight));
    return result;
}

Value thinforcestate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "thinforcestate <state>\n"
            "force into state <state>.\n"
            "2 get headers, 3 get filtered blocks, 4 ready");

    if (nNodeMode != NT_THIN)
        throw runtime_error("Must be in thin mode.");

    int nState = params[0].get_int();
    if (nState <= NS_STARTUP || nState >= NS_UNKNOWN)
        throw runtime_error("unknown state.");



    Object result;
    if (ChangeNodeState(nState))
        result.push_back(Pair("result", "Success."));
    else
        result.push_back(Pair("result", "Failed."));

    return result;
}

// ppcoin: get information of sync-checkpoint
Value getcheckpoint(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getcheckpoint\n"
            "Show info of synchronized checkpoint.\n");

    Object result;
    CBlockIndex* pindexCheckpoint;

    result.push_back(Pair("synccheckpoint", Checkpoints::hashSyncCheckpoint.ToString().c_str()));
    pindexCheckpoint = mapBlockIndex[Checkpoints::hashSyncCheckpoint];
    result.push_back(Pair("height", pindexCheckpoint->nHeight));
    result.push_back(Pair("timestamp", DateTimeStrFormat(pindexCheckpoint->GetBlockTime()).c_str()));

    // Check that the block satisfies synchronized checkpoint
    if (CheckpointsMode == Checkpoints::STRICT)
        result.push_back(Pair("policy", "strict"));

    if (CheckpointsMode == Checkpoints::ADVISORY)
        result.push_back(Pair("policy", "advisory"));

    if (CheckpointsMode == Checkpoints::PERMISSIVE)
        result.push_back(Pair("policy", "permissive"));

    if (mapArgs.count("-checkpointkey"))
        result.push_back(Pair("checkpointmaster", true));

    return result;
}

Value gettxout(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "gettxout \"txid\" n ( includemempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id\n"
            "2. n              (numeric, required) vout value\n"
            "3. includemempool  (boolean, optional) Whether to included the mem pool\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\" : \"hash\",    (string) the block hash\n"
            "  \"confirmations\" : n,       (numeric) The number of confirmations\n"
            "  \"value\" : x.xxx,           (numeric) The transaction value in btc\n"
            "  \"scriptPubKey\" : {         (json object)\n"
            "     \"asm\" : \"code\",       (string) \n"
            "     \"hex\" : \"hex\",        (string) \n"
            "     \"reqSigs\" : n,          (numeric) Number of required signatures\n"
            "     \"type\" : \"pubkeyhash\", (string) The type, eg pubkeyhash\n"
            "     \"addresses\" : [          (array of string) array of bitcoin addresses\n"
            "        \"bitcoinaddress\"     (string) bitcoin address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"version\" : n,            (numeric) The version\n"
            "  \"coinbase\" : true|false   (boolean) Coinbase or not\n"
            "  \"coinstake\" : true|false  (boolean) Coinstake or not\n"
            "}\n"
        );

    LOCK(cs_main);

    Object ret;

    uint256 hash;
    hash.SetHex(params[0].get_str());
    int n = params[1].get_int();
    bool mem = true;
    if (params.size() == 3)
        mem = params[2].get_bool();

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock, mem))
      return Value::null;  

    if (n<0 || (unsigned int)n>=tx.vout.size() || tx.vout[n].IsNull())
      return Value::null;

    ret.push_back(Pair("bestblock", pindexBest->GetBlockHash().GetHex()));
    if (hashBlock == 0)
      ret.push_back(Pair("confirmations", 0));
    else
    {
      map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
      if (mi != mapBlockIndex.end() && (*mi).second)
      {
        CBlockIndex* pindex = (*mi).second;
        if (pindex->IsInMainChain())
        {
          bool isSpent=false;
          CBlockIndex* p = pindex;
          p=p->pnext;
          for (; p; p = p->pnext)
          {
            CBlock block;
            CBlockIndex* pblockindex = mapBlockIndex[p->GetBlockHash()];
            block.ReadFromDisk(pblockindex, true);
            BOOST_FOREACH(const CTransaction& tx, block.vtx)
            {
              BOOST_FOREACH(const CTxIn& txin, tx.vin)
              {
                if( hash == txin.prevout.hash &&
                   (int64_t)txin.prevout.n )
                {
                  printf("spent at block %s\n", block.GetHash().GetHex().c_str());
                  isSpent=true; break;
                }
              }

              if(isSpent) break;
            }

            if(isSpent) break;
          }

          if(isSpent)
            return Value::null;

          ret.push_back(Pair("confirmations", pindexBest->nHeight - pindex->nHeight + 1));
        }
        else
          return Value::null;
      }
    }

    ret.push_back(Pair("value", ValueFromAmount(tx.vout[n].nValue)));
    Object o;
    spj(tx.vout[n].scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("coinbase", tx.IsCoinBase()));
    ret.push_back(Pair("coinstake", tx.IsCoinStake()));

    return ret;
}

Value getblockchaininfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "getblockchaininfo\n"
                "Returns an object containing various state info regarding block chain processing.\n"
                "\nResult:\n"
                "{\n"
                "  \"chain\": \"xxxx\",        (string) current chain (main, testnet)\n"
                "  \"blocks\": xxxxxx,         (numeric) the current number of blocks processed in the server\n"
                "  \"bestblockhash\": \"...\", (string) the hash of the currently best block\n"
                "  \"difficulty\": xxxxxx,     (numeric) the current difficulty\n"
                "  \"initialblockdownload\": xxxx, (bool) estimate of whether this D node is in Initial Block Download mode.\n"
                "  \"verificationprogress\": xxxx, (numeric) estimate of verification progress [0..1]\n"
                "  \"chainwork\": \"xxxx\"     (string) total amount of work in active chain, in hexadecimal\n"
                "  \"moneysupply\": xxxx, (numeric) the current supply of D in circulation\n"
                "}\n"
        );

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    Object obj, diff;
    std::string chain = "testnet";
    if(!fTestNet)
        chain = "main";
    obj.push_back(Pair("chain",          chain));
    obj.push_back(Pair("blocks",         (int)nBestHeight));
    if (nNodeMode == NT_FULL)
    {
        obj.push_back(Pair("bestblockhash",  hashBestChain.GetHex()));
    }
    if (nNodeMode == NT_THIN)
    {
        obj.push_back(Pair("headers",          pindexBestHeader ? pindexBestHeader->nHeight : -1));
        obj.push_back(Pair("filteredblocks",   (int)nHeightFilteredNeeded));
    }
    if (nNodeMode == NT_FULL)
    {
        diff.push_back(Pair("proof-of-work",  GetDifficulty()));
        diff.push_back(Pair("proof-of-stake", GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    } else
    {
        diff.push_back(Pair("proof-of-work",  GetHeaderDifficulty()));
        diff.push_back(Pair("proof-of-stake", GetHeaderDifficulty(GetLastBlockThinIndex(pindexBestHeader, true))));  
    };
    obj.push_back(Pair("difficulty",     diff));
    obj.push_back(Pair("initialblockdownload",  IsInitialBlockDownload()));
    if (nNodeMode == NT_FULL)
    {
        obj.push_back(Pair("verificationprogress", Checkpoints::GuessVerificationProgress(pindexBest)));
        obj.push_back(Pair("chainwork",      leftTrim(pindexBest->nChainWork.GetHex(), '0')));
        obj.push_back(Pair("moneysupply",   ValueFromAmount(pindexBest->nMoneySupply)));
    }
    //obj.push_back(Pair("size_on_disk",   CalculateCurrentUsage()));
    return obj;
}