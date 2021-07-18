// Copyright (c) 2009-2012 Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "net.h"
#include "denariusrpc.h"
#include "alert.h"
#include "wallet.h"
#include "db.h"
#include "walletdb.h"
#include "ui_interface.h"

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
        obj.push_back(Pair("services", strprintf("%08" PRIx64, stats.nServices)));
        obj.push_back(Pair("lastsend", (int64_t)stats.nLastSend));
        obj.push_back(Pair("lastrecv", (int64_t)stats.nLastRecv));
        obj.push_back(Pair("bytessend", (int64_t)stats.nSendBytes));
        obj.push_back(Pair("bytesrecv", (int64_t)stats.nRecvBytes));
        obj.push_back(Pair("conntime", (int64_t)stats.nTimeConnected));
        obj.push_back(Pair("pingtime", stats.dPingTime)); //return nodes ping time
        if (stats.dPingWait > 0.0)
            obj.push_back(Pair("pingwait", stats.dPingWait));
        obj.push_back(Pair("version", stats.nVersion));
        obj.push_back(Pair("subver", stats.strSubVer));
        obj.push_back(Pair("inbound", stats.fInbound));
        obj.push_back(Pair("chainheight", stats.nChainHeight));
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
        (strCommand != "onetry" && strCommand != "add" && strCommand != "remove" && strCommand != "ban"))
        throw runtime_error(
            "addnode <node> <add|remove|onetry|ban>\n"
            "Attempts add or remove <node> from the addnode list or try a connection to <node> once, 'ban' to ban connected node\n");

    string strNode = params[0].get_str();

    if (strCommand == "onetry")
    {
        CAddress addr;
        ConnectNode(addr, strNode.c_str());
        return Value::null;
    }
    else if(strCommand == "ban")
    {
        CNode* pnode = FindNode(strNode);
        if(pnode) {
            int bantime;
            CNode::Ban(pnode->addr, BanReasonManuallyAdded, bantime);
            pnode->CloseSocketDisconnect();
        } else {
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Error: Node is not connected.");
        }
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

Value disconnectnode(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "disconnectnode \"node\" \n"
                "\nImmediately disconnects from the specified node.\n"
                "\nArguments:\n"
                "1. \"node\"     (string, required) The node (see getpeerinfo for nodes)\n"
        );

    string theNode = params[0].get_str();

    CNode* pNode = FindNode(theNode);
    if (pNode == NULL)
        throw JSONRPCError(RPC_CLIENT_NODE_NOT_CONNECTED, "Node not found in connected nodes");

    pNode->fDisconnect = true;

    return Value::null;
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

    Object outboundLimit;
    outboundLimit.push_back(Pair("timeframe", CNode::GetMaxOutboundTimeframe()));
    outboundLimit.push_back(Pair("target", CNode::GetMaxOutboundTarget()));
    outboundLimit.push_back(Pair("target_reached", CNode::OutboundTargetReached(false)));
    outboundLimit.push_back(Pair("serve_historical_blocks", !CNode::OutboundTargetReached(true)));
    outboundLimit.push_back(Pair("bytes_left_in_cycle", CNode::GetOutboundTargetBytesLeft()));
    outboundLimit.push_back(Pair("time_left_in_cycle", CNode::GetMaxOutboundTimeLeftInCycle()));
    obj.push_back(Pair("uploadtarget", outboundLimit));

    return obj;
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

Value setban(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() < 2 ||
        (strCommand != "add" && strCommand != "remove"))
        throw runtime_error(
                "setban \"ip(/netmask)\" \"add|remove\" (bantime) (absolute)\n"
                "\nAttempts add or remove a IP/Subnet from the banned list.\n"
                "\nArguments:\n"
                "1. \"ip(/netmask)\" (string, required) The IP/Subnet (see getpeerinfo for nodes ip) with a optional netmask (default is /32 = single ip)\n"
                "2. \"command\"      (string, required) 'add' to add a IP/Subnet to the list, 'remove' to remove a IP/Subnet from the list\n"
                "3. \"bantime\"      (numeric, optional) time in seconds how long (or until when if [absolute] is set) the ip is banned (0 or empty means using the default time of 24h which can also be overwritten by the -bantime startup argument)\n"
                "4. \"absolute\"     (boolean, optional) If set, the bantime must be a absolute timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
        );

    CSubNet subNet;
    CNetAddr netAddr;
    bool isSubnet = false;

    if (params[0].get_str().find("/") != string::npos)
        isSubnet = true;

    if (!isSubnet)
        netAddr = CNetAddr(params[0].get_str());
    else
        subNet = CSubNet(params[0].get_str());

    if (! (isSubnet ? subNet.IsValid() : netAddr.IsValid()) )
        throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: Invalid IP/Subnet");

    if (strCommand == "add")
    {
        if (isSubnet ? CNode::IsBanned(subNet) : CNode::IsBanned(netAddr))
            throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: IP/Subnet already banned");

        int64_t banTime = 0; //use standard bantime if not specified
        if (params.size() >= 3 && !(params[2] == NULL))
            banTime = params[2].get_int64();

        bool absolute = false;
        if (params.size() == 4)
            absolute = params[3].get_bool();

        isSubnet ? CNode::Ban(subNet, BanReasonManuallyAdded, banTime, absolute) : CNode::Ban(netAddr, BanReasonManuallyAdded, banTime, absolute);

        //disconnect possible nodes
        while(CNode *bannedNode = (isSubnet ? FindNode(subNet) : FindNode(netAddr)))
            bannedNode->fDisconnect = true;
    }
    else if(strCommand == "remove")
    {
        if (!( isSubnet ? CNode::Unban(subNet) : CNode::Unban(netAddr) ))
            throw JSONRPCError(RPC_MISC_ERROR, "Error: Unban failed");
    }

    DumpBanlist(); //store banlist to disk
    uiInterface.BannedListChanged();

    return Value::null;
}

Value listbanned(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "listbanned\n"
                "\nList all banned IPs/Subnets.\n"
        );

    banmap_t banMap;
    CNode::GetBanned(banMap);

    Array bannedAddresses;
    for (banmap_t::iterator it = banMap.begin(); it != banMap.end(); it++)
    {
        CBanEntry banEntry = (*it).second;
        Object rec;
        rec.push_back(Pair("address", (*it).first.ToString()));
        rec.push_back(Pair("banned_until", banEntry.nBanUntil));
        rec.push_back(Pair("ban_created", banEntry.nCreateTime));
        rec.push_back(Pair("ban_reason", banEntry.banReasonToString()));

        bannedAddresses.push_back(rec);
    }

    return bannedAddresses;
}

Value clearbanned(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "clearbanned\n"
            "\nClear all banned IPs.\n");

    CNode::ClearBanned();
    DumpBanlist(); //store banlist to disk
    uiInterface.BannedListChanged();


    return Value::null;
}
