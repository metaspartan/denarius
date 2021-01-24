// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2017-2020 The Denarius developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "denariusrpc.h"
#include "init.h"
#include "txdb.h"
#include <errno.h>

#include <boost/filesystem.hpp>
#include <fstream>

#ifdef USE_IPFS
#include <ipfs/client.h>
#include <ipfs/http/transport.h>
#include <ipfs/test/utils.h>
#endif

using namespace json_spirit;
using namespace std;

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
    ipfs::Json id;
    bool connected = false;
    Object obj, peerinfo;

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

        client.Id(&id);
        
        obj.push_back(Pair("connected",          connected));
        obj.push_back(Pair("jupiterlocal",       "true"));
        obj.push_back(Pair("ipfspeer",           ipfsip));
        obj.push_back(Pair("ipfsversion",        version["Version"].dump().c_str()));

        peerinfo.push_back(Pair("peerid",             id["ID"].dump().c_str()));
        peerinfo.push_back(Pair("addresses",          id["Addresses"].dump().c_str()));
        peerinfo.push_back(Pair("publickey",          id["PublicKey"].dump().c_str()));
        obj.push_back(Pair("peerinfo",                peerinfo));

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
        obj.push_back(Pair("peerinfo",           "Peer ID Info only supported with jupiterlocal=1"));

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
                return 0;
            }

            std::string filename = userFile.c_str();

            boost::filesystem::path p(filename);
            std::string basename = p.filename().string();

            printf("Jupiter Upload File Start: %s\n", basename.c_str());
            //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

            client.FilesAdd(
            {{basename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
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

                    obj.push_back(Pair("filename",           basename.c_str()));
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
                    return 0;
                }

                std::string filename = userFile.c_str();

                boost::filesystem::path p(filename);
                std::string basename = p.filename().string();

                printf("Jupiter Upload File Start: %s\n", basename.c_str());
                //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

                client.FilesAdd(
                {{basename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
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

                        obj.push_back(Pair("filename",           basename.c_str()));
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
                return 0;
            }

            std::string filename = userFile.c_str();

            boost::filesystem::path p(filename);
            std::string basename = p.filename().string();

            printf("Jupiter Upload File Start: %s\n", basename.c_str());
            //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

            client.FilesAdd(
            {{basename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
            &add_result);
            
            const std::string& hash = add_result[0]["hash"];
            int size = add_result[0]["size"];

            std::string r = add_result.dump();
            printf("Jupiter Successfully Added IPFS File(s): %s\n", r.c_str());

            std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
            std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
            std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

            obj.push_back(Pair("filename",           basename.c_str()));
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
                    return 0;
                }

                std::string filename = userFile.c_str();

                boost::filesystem::path p(filename);
                std::string basename = p.filename().string();

                printf("Jupiter Upload File Start: %s\n", basename.c_str());
                //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

                client.FilesAdd(
                {{basename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
                &add_result);
                
                const std::string& hash = add_result[0]["hash"];
                int size = add_result[0]["size"];

                std::string r = add_result.dump();
                printf("Jupiter Successfully Added IPFS File(s): %s\n", r.c_str());

                std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
                std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
                std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

                obj.push_back(Pair("filename",           basename.c_str()));
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
                return 0;
            }

            std::string filename = userFile.c_str();

            boost::filesystem::path p(filename);
            std::string basename = p.filename().string();

            printf("Jupiter Upload File Start: %s\n", basename.c_str());
            //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

            client.FilesAdd(
            {{basename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
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
            first.push_back(Pair("filename",           basename.c_str()));
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
                    return 0;
                }

                std::string filename = userFile.c_str();

                boost::filesystem::path p(filename);
                std::string basename = p.filename().string();

                printf("Jupiter Upload File Start: %s\n", basename.c_str());
                //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

                client.FilesAdd(
                {{basename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
                &add_result);
                
                const std::string& hash = add_result[0]["hash"];
                int size = add_result[0]["size"];

                std::string r = add_result.dump();
                printf("Jupiter Duo Successfully Added IPFS File(s): %s\n", r.c_str());

                std::string filelink = "https://ipfs.infura.io/ipfs/" + hash;
                std::string cloudlink = "https://cloudflare-ipfs.com/ipfs/" + hash;
                std::string ipfsoglink = "https://ipfs.io/ipfs/" + hash;

                second.push_back(Pair("nodeip",             "https://ipfs.infura.io:5001"));
                second.push_back(Pair("filename",           basename.c_str()));
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
                return 0;
            }

            std::string filename = userFile.c_str();

            boost::filesystem::path p(filename);
            std::string basename = p.filename().string();

            printf("Jupiter Upload File Start: %s\n", basename.c_str());
            //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

            client.FilesAdd(
            {{basename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
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
                    first.push_back(Pair("filename",           basename.c_str()));
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
                    return 0;
                }

                std::string filename = userFile.c_str();

                boost::filesystem::path p(filename);
                std::string basename = p.filename().string();

                printf("Jupiter Upload File Start: %s\n", basename.c_str());
                //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

                client.FilesAdd(
                {{basename.c_str(), ipfs::http::FileUpload::Type::kFileName, userFile.c_str()}},
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
                        second.push_back(Pair("filename",           basename.c_str()));
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
