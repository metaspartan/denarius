// Copyright (c) 2009-2012 Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "net.h"
#include "bitcoinrpc.h"
#include "alert.h"
#include "wallet.h"
#include "db.h"
#include "walletdb.h"

using namespace json_spirit;
using namespace std;

Value getconnectioncount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getconnectioncount\n"
            "Returns the number of connections to other nodes.");

    LOCK(cs_vNodes);
    return (int)vNodes.size();
}

Value ping(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "ping\n"
            "Requests that a ping be sent to all other nodes, to measure ping time.\n"
            "Results provided in getpeerinfo, pingtime and pingwait fields are decimal seconds.\n"
            "Ping command is handled in queue with all other commands, so it measures processing backlog, not just network ping.");

    // Request that each node send a ping during next message processing pass
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pNode, vNodes) {
        pNode->fPingQueued = true;
    }

    return Value::null;
}

/*
#ifdef USE_NATIVE_I2P
Value destination(const Array& params, bool fHelp)
{
   if (fHelp || params.size() > 2)
        throw runtime_error(
            "destination [\"match|good|attempt|connect\"] [\"b32.i2p|base64|ip:port\"]\n"
            "\n Returns I2P destination details stored in your b32.i2p address manager lookup system.\n"
            "\nArguments:\n"
            "  If no arguments are provided, the command returns all the b32.i2p addresses. NOTE: Results will not include base64\n"
            "  1st argument = \"match\" then a 2nd argument is also required.\n"
            "  2nd argument = Any string. If a match is found in any of the address, source or base64 fields, that result will be returned.\n"
            "  1st argument = \"good\" destinations that has been tried, connected and found to be good will be returned.\n"
            "  1st argument = \"attempt\" destinations that have been attempted, will be returned.\n"
            "  1st argument = \"connect\" destinations that have been connected to in the past, will be returned.\n"
            "\nResults are returned as a json array of object(s).\n"
            "  The 1st result pair is the total size of the address hash map.\n"
            "  The 2nd result pair is the number of objects which follow, as matching this query.  It can be zero, if no match was found.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"tablesize\": nnn,             (numeric) The total number of destinations in the i2p address book\n"
            "    \"matchsize\": nnn,             (numeric) The number of results returned, which matched your query\n"
            "  }\n"
            "  {\n"
            "    \"address\":\"b32.i2p\",          (string)  Base32 hash of a i2p destination, a possible peer\n"
            "    \"good\": true|false,           (boolean) Has this address been tried & found to be good\n"
            "    \"attempt\": nnn,               (numeric) The number of times it has been attempted\n"
            "    \"lasttry\": ttt,               (numeric) The time of a last attempted connection (memory only)\n"
            "    \"connect\": ttt,               (numeric) The time of a last successful connection\n"
            "    \"source\":\"b32.i2p|ip:port\",   (string)  The source of information about this address\n"
            "    \"base64\":\"destination\",       (string)  The full Base64 Public Key of this peers b32.i2p address\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nNOTE: The results obtained are only a snapshot, while you are connected to the network.\n"
            "      Peers are updating addresses & destinations all the time.\n"
            "\nExamples: destination good|attempt|connect|match <ip> returns all I2P destinations currently known about on the system.\n"
            "\nExamples: destination good returns the I2P destinations marked as 'good', happens if they have been tried and a successful version handshake made.\n"
            "\nExample: destination attempt returns I2P destinations marked as having made an attempt to connect\n"
            "\nExample: destination connect returns I2P destinations which are marked as having been connected to.\n"
            "\nExamples: destination match <ip> returns I2P destination entries which came from the 'source' IP address 215.49.103.xxx\n"
            "\nExamples: destination match <i2p b32> returns all I2P b32.i2p destinations which match the patter, these could be found in the 'source' or the 'address' fields.\n"
        );
    //! We must not have node or main processing as Addrman needs to
    //! be considered static for the time required to process this.
    LOCK2(cs_main, cs_vNodes);

    bool fSelectedMatch = false;
    bool fMatchStr = false;
    bool fMatchTried = false;
    bool fMatchAttempt = false;
    bool fMatchConnect = false;
    bool fUnknownCmd = false;
    string sMatchStr;

    if( params.size() > 0 ) {                                   // Lookup the address and return the one object if found
        string sCmdStr = params[0].get_str();
        if( sCmdStr == "match" ) {
            if( params.size() > 1 ) {
                sMatchStr = params[1].get_str();
                fMatchStr = true;
            } else
                fUnknownCmd = true;
        } else if( sCmdStr == "good" )
            fMatchTried = true;
        else if( sCmdStr == "attempt" )
            fMatchAttempt = true;
        else if( sCmdStr == "connect")
            fMatchConnect = true;
        else
            fUnknownCmd = true;
        fSelectedMatch = true;
    }

    Array ret;
    // Load the vector with all the objects we have and return with
    // the total number of addresses we have on file
    vector<CDestinationStats> vecStats;
    int nTableSize = addrman.CopyDestinationStats(vecStats);
    if( !fUnknownCmd ) {       // If set, throw runtime error
        for( int i = 0; i < 2; i++ ) {      // Loop through the data twice
            bool fMatchFound = false;       // Assume no match
            int nMatchSize = 0;             // the match counter
            BOOST_FOREACH(const CDestinationStats& stats, vecStats) {
                if( fSelectedMatch ) {
                    if( fMatchStr ) {
                        if( stats.sAddress.find(sMatchStr) != string::npos ||
                            stats.sSource.find(sMatchStr) != string::npos ||
                            stats.sBase64.find(sMatchStr) != string::npos )
                                fMatchFound = true;
                    } else if( fMatchTried ) {
                        if( stats.fInTried ) fMatchFound = true;
                    }
                    else if( fMatchAttempt ) {
                        if( stats.nAttempts > 0 ) fMatchFound = true;
                    }
                    else if( fMatchConnect ) {
                        if( stats.nSuccessTime > 0 ) fMatchFound = true;
                    }
                } else          // Match everything
                    fMatchFound = true;

                if( i == 1 && fMatchFound ) {
                    Object obj;
                    obj.push_back(Pair("address", stats.sAddress));
                    obj.push_back(Pair("good", stats.fInTried));
                    obj.push_back(Pair("attempt", stats.nAttempts));
                    obj.push_back(Pair("lasttry", stats.nLastTry));
                    obj.push_back(Pair("connect", stats.nSuccessTime));
                    obj.push_back(Pair("source", stats.sSource));
                    //! Do to an RPC buffer limit of 65535 with stream output, we can not send these and ever get a result
                    //! This should be considered a short term fix ToDo:  Allocate bigger iostream buffer for the output
                    if( fSelectedMatch )
                        obj.push_back(Pair("base64", stats.sBase64));
                    ret.push_back(obj);
                }
                if( fMatchFound ) {
                    nMatchSize++;
                    fMatchFound = false;
                }
            }
            // The 1st time we get a count of the matches, so we can list that first in the results,
            // then we finally build the output objects, on the 2nd pass...and don't put this in there twice
            if( i == 0 ) {
                Object objSizes;
                objSizes.push_back(Pair("tablesize", nTableSize));
                objSizes.push_back(Pair("matchsize", nMatchSize));
                ret.push_back(objSizes);                            // This is the 1st object put on the Array
            }
        }
    } else
        throw runtime_error( "Unknown subcommand or argument missing" );

    return ret;
}
#endif
*/

static void CopyNodeStats(std::vector<CNodeStats>& vstats)
{
    vstats.clear();

    LOCK(cs_vNodes);
    vstats.reserve(vNodes.size());
    BOOST_FOREACH(CNode* pnode, vNodes) {
        CNodeStats stats;
        pnode->copyStats(stats);
        vstats.push_back(stats);
    }
}

static Array GetNetworksInfo()
{
    Array networks;
    for(int n=0; n<NET_MAX; ++n)
    {
        enum Network network = static_cast<enum Network>(n);
        if(network == NET_UNROUTABLE)
            continue;
        proxyType proxy;
        Object obj;
        GetProxy(network, proxy);
        obj.push_back(Pair("name", GetNetworkName(network)));
        obj.push_back(Pair("limited", IsLimited(network)));
        //obj.push_back(Pair("reachable", IsReachable(network)));
        //obj.push_back(Pair("proxy", proxy.IsValid() ? proxy.ToStringIPPort() : string()));
        networks.push_back(obj);
    }
    return networks;
}

static const string SingleAlertSubVersionsString( const std::set<std::string>& setVersions )
{
    std::string strSetSubVer;
    BOOST_FOREACH(std::string str, setVersions) {
        if(strSetSubVer.size())                 // Must be more than one
            strSetSubVer += " or ";
        strSetSubVer += str;
    }
    return strSetSubVer;
}

Value getnetworkinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getnetworkinfo\n"
            " Returns an object containing various state info regarding P2P networking.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,            (numeric) the server version\n"
            "  \"subver\": \"/s:n.n.n.n/\",     (string)  this clients subversion string\n"
            "  \"protocolversion\": xxxxx,    (numeric) the protocol version\n"
            "  \"localservices\": \"xxxx\",     (hex string) Our local service bits as a 16 char string.\n"
            "  \"timeoffset\": xxxxx,         (numeric) the time offset\n"
            "  \"connections\": xxxxx,        (numeric) the number of connections\n"
            "  \"networkconnections\": [      (array)  the state of each possible network connection type\n"
            "    \"name\": \"xxx\",             (string) network name\n"
            "    \"limited\" : true|false,    (boolean) if service is limited\n"
            "  ]\n"
            "  \"localaddresses\": [          (array) list of local addresses\n"
            "    \"address\": \"xxxx\",         (string) network address\n"
            "    \"port\": xxx,               (numeric) network port\n"
            "    \"score\": xxx               (numeric) relative score\n"
            "  ]\n"
            "}\n"
        );

    LOCK(cs_main);

    Object obj;
    obj.push_back(Pair("version",        (int)CLIENT_VERSION));
    obj.push_back(Pair("subversion",     FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, std::vector<string>())));
    obj.push_back(Pair("protocolversion",(int)PROTOCOL_VERSION));
    obj.push_back(Pair("localservices",  strprintf("%016", PRIx64, nLocalServices)));
    obj.push_back(Pair("timeoffset",     GetTimeOffset()));
    obj.push_back(Pair("connections",    (int)vNodes.size()));
    //obj.push_back(Pair("relayfee",       ValueFromAmount(minRelayTxFee.GetFeePerK())));
    obj.push_back(Pair("networkconnections",GetNetworksInfo()));
    Array localAddresses;
    {
        LOCK(cs_mapLocalHost);
        BOOST_FOREACH(const PAIRTYPE(CNetAddr, LocalServiceInfo) &item, mapLocalHost)
        {
            Object rec;
            rec.push_back(Pair("address", item.first.ToString()));
            rec.push_back(Pair("port", item.second.nPort));
            rec.push_back(Pair("score", item.second.nScore));
            localAddresses.push_back(rec);
        }
    }
    obj.push_back(Pair("localaddresses", localAddresses));

    return obj;
}

Value getpeerinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpeerinfo\n"
            "Returns data about each connected network node.");

    vector<CNodeStats> vstats;
    CopyNodeStats(vstats);

    Array ret;

    BOOST_FOREACH(const CNodeStats& stats, vstats) {
        Object obj;

        obj.push_back(Pair("addr", stats.addrName));
        if (!(stats.addrLocal.empty()))
            obj.push_back(Pair("addrlocal", stats.addrLocal));
        obj.push_back(Pair("services", strprintf("%016" PRIx64, stats.nServices)));
        obj.push_back(Pair("lastsend", (int64_t)stats.nLastSend));
        obj.push_back(Pair("lastrecv", (int64_t)stats.nLastRecv));
        obj.push_back(Pair("conntime", (int64_t)stats.nTimeConnected));
        obj.push_back(Pair("pingtime", stats.dPingTime)); //return nodes ping time
        if (stats.dPingWait > 0.0)
            obj.push_back(Pair("pingwait", stats.dPingWait));
        obj.push_back(Pair("version", stats.nVersion));
        obj.push_back(Pair("subver", stats.strSubVer));
        obj.push_back(Pair("inbound", stats.fInbound));
        obj.push_back(Pair("startingheight", stats.nStartingHeight));
        obj.push_back(Pair("banscore", stats.nMisbehavior));

        ret.push_back(obj);
    }

    return ret;
}

Value addnode(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() == 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() != 2 ||
        (strCommand != "onetry" && strCommand != "add" && strCommand != "remove"))
        throw runtime_error(
            "addnode <node> <add|remove|onetry>\n"
            "Attempts add or remove <node> from the addnode list or try a connection to <node> once.");

    string strNode = params[0].get_str();

    if (strCommand == "onetry")
    {
        CAddress addr;
        ConnectNode(addr, strNode.c_str());
        return Value::null;
    }

    LOCK(cs_vAddedNodes);
    vector<string>::iterator it = vAddedNodes.begin();
    for(; it != vAddedNodes.end(); it++)
        if (strNode == *it)
            break;

    if (strCommand == "add")
    {
        if (it != vAddedNodes.end())
            throw JSONRPCError(-23, "Error: Node already added");
        vAddedNodes.push_back(strNode);
    }
    else if(strCommand == "remove")
    {
        if (it == vAddedNodes.end())
            throw JSONRPCError(-24, "Error: Node has not been added.");
        vAddedNodes.erase(it);
    }

    return Value::null;
}

Value setdebug(const Array& params, bool fHelp)
{
    string strType;
    string strOn;
    if (params.size() == 2)
    {
        strType = params[0].get_str();
        strOn = params[1].get_str();
    }
    if (fHelp || params.size() != 2 ||
        (strOn != "on" && strOn != "off" && (strType != "all" && strType != "fs" && strType != "net" && strType != "chain" && strType != "ringsig" && strType != "smsg") ))
        throw runtime_error(
            "setdebug <type> <on|off>\n"
            "Sets the debug mode on the wallet. type 'all' includes 'fs', 'net', 'chain', 'ringsig', 'smsg'");


    fDebug = strType == "all" && strOn == "on";
    fDebugFS = (strType == "all" || strType == "fs") && strOn == "on";
    fDebugChain = (strType == "all" || strType == "chain") && strOn == "on";
    fDebugNet = (strType == "all" || strType == "net") && strOn == "on";
    fDebugRingSig = (strType == "all" || strType == "ringsig") && strOn == "on";
    fDebugSmsg = (strType == "all" || strType == "smsg") && strOn == "on";

    return Value::null;
}

Value getaddednodeinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getaddednodeinfo <dns> [node]\n"
            "Returns information about the given added node, or all added nodes\n"
            "(note that onetry addnodes are not listed here)\n"
            "If dns is false, only a list of added nodes will be provided,\n"
            "otherwise connected information will also be available.");

    bool fDns = params[0].get_bool();

    list<string> laddedNodes(0);
    if (params.size() == 1)
    {
        LOCK(cs_vAddedNodes);
        BOOST_FOREACH(string& strAddNode, vAddedNodes)
            laddedNodes.push_back(strAddNode);
    }
    else
    {
        string strNode = params[1].get_str();
        LOCK(cs_vAddedNodes);
        BOOST_FOREACH(string& strAddNode, vAddedNodes)
            if (strAddNode == strNode)
            {
                laddedNodes.push_back(strAddNode);
                break;
            }
        if (laddedNodes.size() == 0)
            throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
    }

    if (!fDns)
    {
        Object ret;
        BOOST_FOREACH(string& strAddNode, laddedNodes)
            ret.push_back(Pair("addednode", strAddNode));
        return ret;
    }

    Array ret;

    list<pair<string, vector<CService> > > laddedAddreses(0);
    BOOST_FOREACH(string& strAddNode, laddedNodes)
    {
        vector<CService> vservNode(0);
        if(Lookup(strAddNode.c_str(), vservNode, GetDefaultPort(), fNameLookup, 0))
            laddedAddreses.push_back(make_pair(strAddNode, vservNode));
        else
        {
            Object obj;
            obj.push_back(Pair("addednode", strAddNode));
            obj.push_back(Pair("connected", false));
            Array addresses;
            obj.push_back(Pair("addresses", addresses));
        }
    }

    LOCK(cs_vNodes);
    for (list<pair<string, vector<CService> > >::iterator it = laddedAddreses.begin(); it != laddedAddreses.end(); it++)
    {
        Object obj;
        obj.push_back(Pair("addednode", it->first));

        Array addresses;
        bool fConnected = false;
        BOOST_FOREACH(CService& addrNode, it->second)
        {
            bool fFound = false;
            Object node;
            node.push_back(Pair("address", addrNode.ToString()));
            BOOST_FOREACH(CNode* pnode, vNodes)
                if (pnode->addr == addrNode)
                {
                    fFound = true;
                    fConnected = true;
                    node.push_back(Pair("connected", pnode->fInbound ? "inbound" : "outbound"));
                    break;
                }
            if (!fFound)
                node.push_back(Pair("connected", "false"));
            addresses.push_back(node);
        }
        obj.push_back(Pair("connected", fConnected));
        obj.push_back(Pair("addresses", addresses));
        ret.push_back(obj);
    }

    return ret;
}

Value getnettotals(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw runtime_error(
            "getnettotals\n"
            "Returns information about network traffic, including bytes in, bytes out,\n"
            "and current time.");

    Object obj;
    obj.push_back(Pair("totalbytesrecv", CNode::GetTotalBytesRecv()));
    obj.push_back(Pair("totalbytessent", CNode::GetTotalBytesSent()));
    obj.push_back(Pair("timemillis", GetTimeMillis()));
    return obj;
}

// ppcoin: send alert.  
// There is a known deadlock situation with ThreadMessageHandler
// ThreadMessageHandler: holds cs_vSend and acquiring cs_main in SendMessages()
// ThreadRPCServer: holds cs_main and acquiring cs_vSend in alert.RelayTo()/PushMessage()/BeginMessage()
Value sendalert(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 6)
        throw runtime_error(
            "sendalert <message> <privatekey> <minver> <maxver> <priority> <id> [cancelupto]\n"
            "<message> is the alert text message\n"
            "<privatekey> is hex string of alert master private key\n"
            "<minver> is the minimum applicable internal client version\n"
            "<maxver> is the maximum applicable internal client version\n"
            "<priority> is integer priority number\n"
            "<id> is the alert id\n"
            "[cancelupto] cancels all alert id's up to this number\n"
            "Returns true or false.");

    CAlert alert;
    CKey key;

    alert.strStatusBar = params[0].get_str();
    alert.nMinVer = params[2].get_int();
    alert.nMaxVer = params[3].get_int();
    alert.nPriority = params[4].get_int();
    alert.nID = params[5].get_int();
    if (params.size() > 6)
        alert.nCancel = params[6].get_int();
    alert.nVersion = PROTOCOL_VERSION;
    alert.nRelayUntil = GetAdjustedTime() + 365*24*60*60;
    alert.nExpiration = GetAdjustedTime() + 365*24*60*60;

    CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
    sMsg << (CUnsignedAlert)alert;
    alert.vchMsg = vector<unsigned char>(sMsg.begin(), sMsg.end());

    vector<unsigned char> vchPrivKey = ParseHex(params[1].get_str());
    key.SetPrivKey(CPrivKey(vchPrivKey.begin(), vchPrivKey.end()), false); // if key is not correct openssl may crash
    if (!key.Sign(Hash(alert.vchMsg.begin(), alert.vchMsg.end()), alert.vchSig))
        throw runtime_error(
            "Unable to sign alert, check private key?\n");  
    if(!alert.ProcessAlert()) 
        throw runtime_error(
            "Failed to process alert.\n");
    // Relay alert
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
            alert.RelayTo(pnode);
    }

    Object result;
    result.push_back(Pair("strStatusBar", alert.strStatusBar));
    result.push_back(Pair("nVersion", alert.nVersion));
    result.push_back(Pair("nMinVer", alert.nMinVer));
    result.push_back(Pair("nMaxVer", alert.nMaxVer));
    result.push_back(Pair("nPriority", alert.nPriority));
    result.push_back(Pair("nID", alert.nID));
    if (alert.nCancel > 0)
        result.push_back(Pair("nCancel", alert.nCancel));
    return result;
}
