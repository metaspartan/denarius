// Copyright (c) 2019 Denarius developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "db.h"
#include "net.h"
#include "init.h"
#include "strlcpy.h"
#include "addrman.h"
#include "ui_interface.h"
#include "fortuna.h"
#include <sys/stat.h>

#ifdef WIN32
#include <string.h>
#endif

#ifdef USE_NATIVE_I2P
#include "i2p.h"
#endif

#ifdef USE_UPNP
#include <miniupnpc/miniwget.h>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#endif

using namespace std;
using namespace boost;

namespace fs = boost::filesystem;

#ifdef USE_NATIVETOR
extern "C" {
    int tor_main(int argc, char *argv[]);
}
#endif

static const int MAX_OUTBOUND_CONNECTIONS = 27; //Bumping up to 27 from 16

void ThreadMessageHandler2(void* parg);
void ThreadSocketHandler2(void* parg);
void ThreadOpenConnections2(void* parg);
void ThreadOpenAddedConnections2(void* parg);
#ifdef USE_UPNP
void ThreadMapPort2(void* parg);
#endif
void ThreadDNSAddressSeed2(void* parg);
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound = NULL, const char *strDest = NULL, bool fOneShot = false);

//
// Global state variables
//
bool fDiscover = true;
bool fUseUPnP = false;
#ifdef USE_NATIVE_I2P
uint64_t nLocalServices = NODE_I2P | NODE_NETWORK;
#else
uint64_t nLocalServices = NODE_NETWORK;
#endif
CCriticalSection cs_mapLocalHost;
map<CNetAddr, LocalServiceInfo> mapLocalHost;
static bool vfReachable[NET_MAX] = {};
static bool vfLimited[NET_MAX] = {};
static CNode* pnodeLocalHost = NULL;
static CNode* pnodeSync = NULL;
CAddress addrSeenByPeer(CService("0.0.0.0", 0), nLocalServices);
uint64_t nLocalHostNonce = 0;
boost::array<int, THREAD_MAX> vnThreadsRunning;
static std::vector<SOCKET> vhListenSocket;
//CAddrMan addrman; in netbase.cpp now

#ifdef USE_NATIVE_I2P
static std::vector<SOCKET> vhI2PListenSocket;
int nI2PNodeCount = 0;
#endif

vector<CNode*> vNodes;
CCriticalSection cs_vNodes;
map<CInv, CDataStream> mapRelay;
deque<pair<int64_t, CInv> > vRelayExpiration;
CCriticalSection cs_mapRelay;
map<CInv, int64_t> mapAlreadyAskedFor;

static deque<string> vOneShots;
CCriticalSection cs_vOneShots;

set<CNetAddr> setservAddNodeAddresses;
CCriticalSection cs_setservAddNodeAddresses;

vector<std::string> vAddedNodes;
CCriticalSection cs_vAddedNodes;

static CSemaphore *semOutbound = NULL;

// Signals for message handling
static CNodeSignals g_signals;
CNodeSignals& GetNodeSignals() { return g_signals; }

void AddOneShot(string strDest)
{
    LOCK(cs_vOneShots);
    vOneShots.push_back(strDest);
}

unsigned short GetListenPort()
{
    return (unsigned short)(GetArg("-port", GetDefaultPort()));
}


void CNode::PushGetBlocks(CBlockIndex* pindexBegin, uint256 hashEnd)
{
    // Filter out duplicate requests
    if (pindexBegin == pindexLastGetBlocksBegin && hashEnd == hashLastGetBlocksEnd)
        return;
    pindexLastGetBlocksBegin = pindexBegin;
    hashLastGetBlocksEnd = hashEnd;

    getBlocksIndex.push_back(pindexBegin);
    getBlocksHash.push_back(hashEnd);

    //PushMessage("getblocks", CBlockLocator(pindexBegin), hashEnd);
}


// find 'best' local address for a particular peer
bool GetLocal(CService& addr, const CNetAddr *paddrPeer)
{
#ifdef USE_NATIVE_I2P
    if( !paddrPeer->IsNativeI2P() && fNoListen)
#else
    if( fNoListen )
#endif
        return false;

    int nBestScore = -1;
    int nBestReachability = -1;
    {
        LOCK(cs_mapLocalHost);
        for (map<CNetAddr, LocalServiceInfo>::iterator it = mapLocalHost.begin(); it != mapLocalHost.end(); it++)
        {
            int nScore = (*it).second.nScore;
            int nReachability = (*it).first.GetReachabilityFrom(paddrPeer);
            if (nReachability > nBestReachability || (nReachability == nBestReachability && nScore > nBestScore))
            {
                addr = CService((*it).first, (*it).second.nPort);
                nBestReachability = nReachability;
                nBestScore = nScore;
            }
        }
    }
    return nBestScore >= 0;
}

// get best local address for a particular peer as a CAddress
CAddress GetLocalAddress(const CNetAddr *paddrPeer)
{
    CAddress ret(CService("0.0.0.0",0),0);
    CService addr;
    if (GetLocal(addr, paddrPeer))
    {
        ret = CAddress(addr);
        ret.nServices = nLocalServices;
        ret.nTime = GetAdjustedTime();
    }
    return ret;
}

bool RecvLine(SOCKET hSocket, string& strLine)
{
    strLine = "";
    while (true)
    {
        char c;
        int nBytes = recv(hSocket, &c, 1, 0);
        if (nBytes > 0)
        {
            if (c == '\n')
                continue;
            if (c == '\r')
                return true;
            strLine += c;
            if (strLine.size() >= 9000)
                return true;
        }
        else if (nBytes <= 0)
        {
            if (fShutdown)
                return false;
            if (nBytes < 0)
            {
                int nErr = WSAGetLastError();
                if (nErr == WSAEMSGSIZE)
                    continue;
                if (nErr == WSAEWOULDBLOCK || nErr == WSAEINTR || nErr == WSAEINPROGRESS)
                {
                    MilliSleep(10);
                    continue;
                }
            }
            if (!strLine.empty())
                return true;
            if (nBytes == 0)
            {
                // socket closed
                printf("socket closed\n");
                return false;
            }
            else
            {
                // socket error
                int nErr = WSAGetLastError();
                printf("recv failed: %d\n", nErr);
                return false;
            }
        }
    }
}

// used when scores of local addresses may have changed
// pushes better local address to peers
void static AdvertizeLocal()
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if (pnode->fSuccessfullyConnected)
        {
            CAddress addrLocal = GetLocalAddress(&pnode->addr);
            if (addrLocal.IsRoutable() && (CService)addrLocal != (CService)pnode->addrLocal)
            {
                pnode->PushAddress(addrLocal);
                pnode->addrLocal = addrLocal;
            }
        }
    }
}

void SetReachable(enum Network net, bool fFlag)
{
    LOCK(cs_mapLocalHost);
    vfReachable[net] = fFlag;
    if (net == NET_IPV6 && fFlag)
        vfReachable[NET_IPV4] = true;
}

// learn a new local address
bool AddLocal(const CService& addr, int nScore)
{
    if (!addr.IsRoutable())
        return false;

#ifdef USE_NATIVE_I2P
    if( !addr.IsI2P() && !fDiscover && nScore < LOCAL_MANUAL)
#else
    if (fDiscover && nScore < LOCAL_MANUAL)
#endif
        return false;

    if (IsLimited(addr))
        return false;

    if (fNativeI2P)
        printf(addr.IsI2P() ? "Accepting I2P peers at: %s Score=%d\n" : "AddLocal(%s,%i)\n", addr.ToString().c_str(), nScore);

    printf("AddLocal(%s,%i)\n", addr.ToString().c_str(), nScore);

    {
        LOCK(cs_mapLocalHost);
        bool fAlready = mapLocalHost.count(addr) > 0;
        LocalServiceInfo &info = mapLocalHost[addr];
        if (!fAlready || nScore >= info.nScore) {
            info.nScore = nScore + (fAlready ? 1 : 0);
            info.nPort = addr.GetPort();
        }
        SetReachable(addr.GetNetwork());
    }

    AdvertizeLocal();

    return true;
}

bool AddLocal(const CNetAddr &addr, int nScore)
{
    return AddLocal(CService(addr, GetListenPort()), nScore);
}

/** Make a particular network entirely off-limits (no automatic connects to it) */
void SetLimited(enum Network net, bool fLimited)
{
    if (net == NET_UNROUTABLE)
        return;
    LOCK(cs_mapLocalHost);
    vfLimited[net] = fLimited;
}

bool IsLimited(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return vfLimited[net];
}

bool IsLimited(const CNetAddr &addr)
{
    return IsLimited(addr.GetNetwork());
}

/** vote for a local address */
bool SeenLocal(const CService& addr)
{
    {
        LOCK(cs_mapLocalHost);
        if (mapLocalHost.count(addr) == 0)
            return false;
        mapLocalHost[addr].nScore++;
    }

    AdvertizeLocal();

    return true;
}

/** check whether a given address is potentially local */
bool IsLocal(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    return mapLocalHost.count(addr) > 0;
}

/** check whether a given address is in a network we can probably connect to */
bool IsReachable(const CNetAddr& addr)
{
    LOCK(cs_mapLocalHost);
    enum Network net = addr.GetNetwork();
    return vfReachable[net] && !vfLimited[net];
}

bool GetMyExternalIP2(const CService& addrConnect, const char* pszGet, const char* pszKeyword, CNetAddr& ipRet)
{
    SOCKET hSocket;
	  bool bProxyConnectionFailed = false;
	  int ntimeout = 1000;
    if (!ConnectSocket(addrConnect, hSocket, ntimeout, &bProxyConnectionFailed))
        return error("GetMyExternalIP() : connection to %s failed", addrConnect.ToString().c_str());

    send(hSocket, pszGet, strlen(pszGet), MSG_NOSIGNAL);

    string strLine;
    while (RecvLine(hSocket, strLine))
    {
        if (strLine.empty()) // HTTP response is separated from headers by blank line
        {
            while (true)
            {
                if (!RecvLine(hSocket, strLine))
                {
                    closesocket(hSocket);
                    return false;
                }
                if (pszKeyword == NULL)
                    break;
                if (strLine.find(pszKeyword) != string::npos)
                {
                    strLine = strLine.substr(strLine.find(pszKeyword) + strlen(pszKeyword));
                    break;
                }
            }
            closesocket(hSocket);
            if (strLine.find("<") != string::npos)
                strLine = strLine.substr(0, strLine.find("<"));
            strLine = strLine.substr(strspn(strLine.c_str(), " \t\n\r"));
            while (strLine.size() > 0 && isspace(strLine[strLine.size()-1]))
                strLine.resize(strLine.size()-1);
            CService addr(strLine,0,true);
            printf("GetMyExternalIP() received [%s] %s\n", strLine.c_str(), addr.ToString().c_str());
            if (!addr.IsValid() || !addr.IsRoutable())
                return false;
            ipRet.SetIP(addr);
            return true;
        }
    }
    closesocket(hSocket);
    return error("GetMyExternalIP() : connection closed");
}

// We now get our external IP from the IRC server first and only use this as a backup
bool GetMyExternalIP(CNetAddr& ipRet)
{
    CService addrConnect;
    const char* pszGet;
    const char* pszKeyword;

    for (int nLookup = 0; nLookup <= 1; nLookup++)
    for (int nHost = 1; nHost <= 2; nHost++)
    {
        // We should be phasing out our use of sites like these.  If we need
        // replacements, we should ask for volunteers to put this simple
        // php file on their web server that prints the client IP:
        //  <?php echo $_SERVER["REMOTE_ADDR"]; ?>
        if (nHost == 1)
        {
            addrConnect = CService("91.198.22.70",80); // checkip.dyndns.org

            if (nLookup == 1)
            {
                CService addrIP("checkip.dyndns.org", 80, true);
                if (addrIP.IsValid())
                    addrConnect = addrIP;
            }

            pszGet = "GET / HTTP/1.1\r\n"
                     "Host: checkip.dyndns.org\r\n"
                     "User-Agent: Denarius\r\n"
                     "Connection: close\r\n"
                     "\r\n";

            pszKeyword = "Address:";
        }
        else if (nHost == 2)
        {
            addrConnect = CService("74.208.43.192", 80); // www.showmyip.com

            if (nLookup == 1)
            {
                CService addrIP("www.showmyip.com", 80, true);
                if (addrIP.IsValid())
                    addrConnect = addrIP;
            }

            pszGet = "GET /simple/ HTTP/1.1\r\n"
                     "Host: www.showmyip.com\r\n"
                     "User-Agent: Denarius\r\n"
                     "Connection: close\r\n"
                     "\r\n";

            pszKeyword = NULL; // Returns just IP address
        }

        if (GetMyExternalIP2(addrConnect, pszGet, pszKeyword, ipRet))
            return true;
    }

    return false;
}

void ThreadGetMyExternalIP(void* parg)
{
    // Make this thread recognisable as the external IP detection thread
    RenameThread("denarius-ext-ip");
#ifdef USE_NATIVE_I2P

    if (IsI2POnly() && fNativeI2P)
        return;
#endif

    CNetAddr addrLocalHost;
    if (GetMyExternalIP(addrLocalHost))
    {
        printf("GetMyExternalIP() returned %s\n", addrLocalHost.ToStringIP().c_str());
        AddLocal(addrLocalHost, LOCAL_HTTP);
    }
}





void AddressCurrentlyConnected(const CService& addr)
{
    addrman.Connected(addr);
}





uint64_t CNode::nTotalBytesRecv = 0;
uint64_t CNode::nTotalBytesSent = 0;
CCriticalSection CNode::cs_totalBytesRecv;
CCriticalSection CNode::cs_totalBytesSent;

CNode* FindNode(const CNetAddr& ip)
{
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
            if ((CNetAddr)pnode->addr == ip)
                return (pnode);
    }
    return NULL;
}

CNode* FindNode(std::string addrName)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        if (pnode->addrName == addrName)
            return (pnode);
    return NULL;
}

CNode* FindNode(const CService& addr)
{
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
            if ((CService)pnode->addr == addr)
                return (pnode);
    }
    return NULL;
}

CNode* ConnectNode(CAddress addrConnect, const char *pszDest, bool forTunaMaster)
{
    if (pszDest == NULL) {
        if (IsLocal(addrConnect))
            return NULL;

        // Look for an existing connection
        CNode* pnode = FindNode((CService)addrConnect);
        if (pnode)
        {

        if(forTunaMaster)
                pnode->fForTunaMaster = true;
            pnode->AddRef();

            pnode->PushMessage("mktinv", GetTime() - (7 * 24 * 60 * 60));

            return pnode;
        }
    }


    /// debug print
        if (fDebugNet) printf("net: trying connection %s lastseen=%.1fhrs\n",
        pszDest ? pszDest : addrConnect.ToString().c_str(),
        pszDest ? 0 : (double)(GetAdjustedTime() - addrConnect.nTime)/3600.0);

    // Connect
    SOCKET hSocket;
    bool proxyConnectionFailed = false;

#ifdef USE_NATIVE_I2P
    int nPort = pszDest && isStringI2pDestination(string(pszDest)) ? 0 : GetDefaultPort();
    if (pszDest ? ConnectSocketByName(addrConnect, hSocket, pszDest, nPort, nConnectTimeout, &proxyConnectionFailed) :
                  ConnectSocket(addrConnect, hSocket, nConnectTimeout, &proxyConnectionFailed))
#else
    if (pszDest ? ConnectSocketByName(addrConnect, hSocket, pszDest, GetDefaultPort(), nConnectTimeout, &proxyConnectionFailed) :
                  ConnectSocket(addrConnect, hSocket, nConnectTimeout, &proxyConnectionFailed))    
#endif
    {
        addrman.Attempt(addrConnect);

        if (fDebugNet) printf("net: connected %s\n", pszDest ? pszDest : addrConnect.ToString().c_str());

        // Set to non-blocking
#ifdef WIN32
        u_long nOne = 1;
        if (ioctlsocket(hSocket, FIONBIO, &nOne) == SOCKET_ERROR)
            printf("ConnectSocket() : ioctlsocket non-blocking setting failed, error %d\n", WSAGetLastError());
#else
        if (fcntl(hSocket, F_SETFL, O_NONBLOCK) == SOCKET_ERROR)
            printf("ConnectSocket() : fcntl non-blocking setting failed, error %d\n", errno);
#endif

        // Add node
        CNode* pnode = new CNode(hSocket, addrConnect, pszDest ? pszDest : "", false);
        pnode->AddRef();

        {
            LOCK(cs_vNodes);
            vNodes.push_back(pnode);
#ifdef USE_NATIVE_I2P
            if (fNativeI2P) {
                if (addrConnect.IsNativeI2P())
                    ++nI2PNodeCount;
            }
#endif
        }

        if(forTunaMaster)
                pnode->fForTunaMaster = true;
        pnode->nTimeConnected = GetTime();
        return pnode;
    } else if (!proxyConnectionFailed) {
        // If connecting to the node failed, and failure is not caused by a problem connecting to
        // the proxy, mark this as an attempt.
        addrman.Attempt(addrConnect);
    }

    return NULL;
}

void CNode::CloseSocketDisconnect()
{
    fDisconnect = true;
    if (hSocket != INVALID_SOCKET)
    {
        printf("disconnecting node %s\n", addrName.c_str());
        closesocket(hSocket);
        hSocket = INVALID_SOCKET;

        // in case this fails, we'll empty the recv buffer when the CNode is deleted
        TRY_LOCK(cs_vRecvMsg, lockRecv);
        if (lockRecv)
            vRecvMsg.clear();
    }
}

void CNode::Cleanup()
{
}


void CNode::PushVersion()
{
    /// when NTP implemented, change to just nTime = GetAdjustedTime()
    int64_t nTime = (fInbound ? GetAdjustedTime() : GetTime());
    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService("0.0.0.0",0)));
    CAddress addrMe = GetLocalAddress(&addr);
    RAND_bytes((unsigned char*)&nLocalHostNonce, sizeof(nLocalHostNonce));
    printf("send version message: version %d, blocks=%d, us=%s, them=%s, peer=%s\n", PROTOCOL_VERSION, nBestHeight, addrMe.ToString().c_str(), addrYou.ToString().c_str(), addr.ToString().c_str());
    PushMessage("version", PROTOCOL_VERSION, nLocalServices, nTime, addrYou, addrMe,
                nLocalHostNonce, FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, std::vector<string>()), nBestHeight);
}





std::map<CNetAddr, int64_t> CNode::setBanned;
CCriticalSection CNode::cs_setBanned;

void CNode::ClearBanned()
{
    setBanned.clear();
}

bool CNode::IsBanned(CNetAddr ip)
{
    bool fResult = false;
    {
        LOCK(cs_setBanned);
        std::map<CNetAddr, int64_t>::iterator i = setBanned.find(ip);
        if (i != setBanned.end())
        {
            int64_t t = (*i).second;
            if (GetTime() < t)
                fResult = true;
        }
    }
    return fResult;
}

bool CNode::Misbehaving(int howmuch)
{
    if (addr.IsLocal())
    {
        printf("Warning: Local node %s misbehaving (delta: %d)!\n", addrName.c_str(), howmuch);
        return false;
    }

    nMisbehavior += howmuch;
    if (nMisbehavior >= GetArg("-banscore", 100))
    {
        int64_t banTime = GetTime()+GetArg("-bantime", 60*60*24);  // Default 24-hour ban
        printf("Misbehaving: %s (%d -> %d) DISCONNECTING\n", addr.ToString().c_str(), nMisbehavior-howmuch, nMisbehavior);
        {
            LOCK(cs_setBanned);
            if (setBanned[addr] < banTime)
                setBanned[addr] = banTime;
        }
        CloseSocketDisconnect();
        return true;
    } else
        printf("Misbehaving: %s (%d -> %d)\n", addr.ToString().c_str(), nMisbehavior-howmuch, nMisbehavior);
    return false;
}

#undef X
#define X(name) stats.name = name
void CNode::copyStats(CNodeStats &stats)
{
	stats.nodeid = this->GetId();
    X(nServices);
    X(nLastSend);
    X(nLastRecv);
    X(nSendBytes);
    X(nRecvBytes);
    X(nTimeConnected);
    X(addrName);
    X(nVersion);
    X(strSubVer);
    X(fInbound);
    X(nStartingHeight);
    X(nMisbehavior);

    // It is common for nodes with good ping times to suddenly become lagged,
    // due to a new block arriving or other large transfer.
    // Merely reporting pingtime might fool the caller into thinking the node was still responsive,
    // since pingtime does not update until the ping is complete, which might take a while.
    // So, if a ping is taking an unusually long time in flight,
    // the caller can immediately detect that this is happening.
    int64_t nPingUsecWait = 0;
    if ((0 != nPingNonceSent) && (0 != nPingUsecStart)) {
        nPingUsecWait = GetTimeMicros() - nPingUsecStart;
    }

    // Raw ping time is in microseconds, but show it to user as whole seconds (Bitcoin users should be well used to small numbers with many decimal places by now :)
    stats.dPingTime = (((double)nPingUsecTime) / 1e6);
    stats.dPingWait = (((double)nPingUsecWait) / 1e6);
}
#undef X

void CNode::RecordBytesRecv(uint64_t bytes)
{
    LOCK(cs_totalBytesRecv);
    nTotalBytesRecv += bytes;
}

void CNode::RecordBytesSent(uint64_t bytes)
{
    LOCK(cs_totalBytesSent);
    nTotalBytesSent += bytes;
}

uint64_t CNode::GetTotalBytesRecv()
{
    LOCK(cs_totalBytesRecv);
    return nTotalBytesRecv;
}

uint64_t CNode::GetTotalBytesSent()
{
    LOCK(cs_totalBytesSent);
    return nTotalBytesSent;
}

// requires LOCK(cs_vRecvMsg)
bool CNode::ReceiveMsgBytes(const char *pch, unsigned int nBytes)
{
    while (nBytes > 0) {

        // get current incomplete message, or create a new one
        if (vRecvMsg.empty() ||
            vRecvMsg.back().complete())
#ifdef USE_NATIVE_I2P
        if (fNativeI2P) {
            vRecvMsg.push_back(CNetMessage(nRecvStreamType, nRecvVersion));
        } else {
            vRecvMsg.push_back(CNetMessage(SER_NETWORK, nRecvVersion));
        }
#else
        vRecvMsg.push_back(CNetMessage(SER_NETWORK, nRecvVersion));
#endif

        CNetMessage& msg = vRecvMsg.back();

        // absorb network data
        int handled;
        if (!msg.in_data)
            handled = msg.readHeader(pch, nBytes);
        else
            handled = msg.readData(pch, nBytes);

        if (handled < 0)
                return false;

        pch += handled;
        nBytes -= handled;

        if (msg.complete())
            msg.nTime = GetTimeMicros();
    }

    return true;
}

#ifdef USE_NATIVE_I2P
void AddIncomingConnection(SOCKET hSocket, const CAddress& addr)
{
    int nInbound = 0;

    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
            if (pnode->fInbound)
                nInbound++;
    }

    if (hSocket == INVALID_SOCKET)
    {
        int nErr = WSAGetLastError();
        if (nErr != WSAEWOULDBLOCK)
            printf("I2P socket error accept failed: %d\n", nErr);
    }
    else if (nInbound >= GetArg("-maxconnections", 125) - MAX_OUTBOUND_CONNECTIONS)
    {
        {
            LOCK(cs_setservAddNodeAddresses);
            if (!setservAddNodeAddresses.count(addr))
                closesocket(hSocket);
        }
    }
    else if (CNode::IsBanned(addr))
    {
        printf("I2P connection from %s dropped (banned)\n", addr.ToString().c_str());
        closesocket(hSocket);
    }
    else
    {
        printf("I2P accepted connection %s\n", addr.ToString().c_str());
        CNode* pnode = new CNode(hSocket, addr, "", true);
        pnode->AddRef();
        {
            LOCK(cs_vNodes);
            vNodes.push_back(pnode);
            ++nI2PNodeCount;
        }
    }
}

#endif

int CNetMessage::readHeader(const char *pch, unsigned int nBytes)
{
    // copy data to temporary parsing buffer
    unsigned int nRemaining = 24 - nHdrPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    memcpy(&hdrbuf[nHdrPos], pch, nCopy);
    nHdrPos += nCopy;

    // if header incomplete, exit
    if (nHdrPos < 24)
        return nCopy;

    // deserialize to CMessageHeader
    try {
        hdrbuf >> hdr;
    }
    catch (std::exception &e) {
        return -1;
    }

    // reject messages larger than MAX_SIZE
    if (hdr.nMessageSize > MAX_SIZE)
            return -1;

    // switch state to reading message data
    in_data = true;
    vRecv.resize(hdr.nMessageSize);

    return nCopy;
}

int CNetMessage::readData(const char *pch, unsigned int nBytes)
{
    unsigned int nRemaining = hdr.nMessageSize - nDataPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    memcpy(&vRecv[nDataPos], pch, nCopy);
    nDataPos += nCopy;

    return nCopy;
}









// requires LOCK(cs_vSend)
void SocketSendData(CNode *pnode)
{
    std::deque<CSerializeData>::iterator it = pnode->vSendMsg.begin();

    while (it != pnode->vSendMsg.end()) {
        const CSerializeData &data = *it;
        assert(data.size() > pnode->nSendOffset);
        int nBytes = send(pnode->hSocket, &data[pnode->nSendOffset], data.size() - pnode->nSendOffset, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (nBytes > 0) {
            pnode->nLastSend = GetTime();
            pnode->nSendOffset += nBytes;

            pnode->nSendBytes += nBytes;
            pnode->RecordBytesSent(nBytes);

            if (pnode->nSendOffset == data.size()) {
                pnode->nSendOffset = 0;
                pnode->nSendSize -= data.size();
                it++;
            } else {
                // could not send full message; stop sending more
                break;
            }
        } else {
            if (nBytes < 0) {
                // error
                int nErr = WSAGetLastError();
                if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
                {
                    printf("socket send error %d\n", nErr);
                    pnode->CloseSocketDisconnect();
                }
            }
            // couldn't send anything at all
            break;
        }
    }

    if (it == pnode->vSendMsg.end()) {
        assert(pnode->nSendOffset == 0);
        assert(pnode->nSendSize == 0);
    }
    pnode->vSendMsg.erase(pnode->vSendMsg.begin(), it);
}

#ifdef USE_NATIVE_I2P
static void AddIncomingI2pConnection(SOCKET hSocket, const CAddress& addr)
{
    int nInbound = 0;

    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
            if (pnode->fInbound)
                nInbound++;
    }

    if (hSocket == INVALID_SOCKET)
    {
        int nErr = WSAGetLastError();
        if (nErr != WSAEWOULDBLOCK)
            LogPrintf("I2P socket error accept failed: %d\n", nErr);
    }
    else if (nInbound >= GetArg("-maxconnections", 125) - MAX_OUTBOUND_CONNECTIONS)
    {
        {
            LOCK(cs_setservAddNodeAddresses);
            if (!setservAddNodeAddresses.count(addr))
                CloseSocket(hSocket);
        }
    }
    else if (CNode::IsBanned(addr))
    {
        LogPrintf("connection from %s dropped (banned)\n", addr.ToString().c_str());
        CloseSocket(hSocket);
    }
    else
    {
        // At this point, the CAddress object has been correctly setup, the garlic field installed, the port zero'd out
        // and the native i2p destination address verified.  So we can simply add a new node and know that the correct stream
        // type and associated logic will all work correctly.
        LogPrintf("accepted connection %s\n", addr.ToString().c_str());
        CNode* pnode = new CNode(hSocket, addr, "", true);
        pnode->AddRef();
        {
            LOCK(cs_vNodes);
            vNodes.push_back(pnode);
        }
    }
}
#endif

void ThreadSocketHandler(void* parg)
{
    // Make this thread recognisable as the networking thread
    RenameThread("denarius-net");

    try
    {
        vnThreadsRunning[THREAD_SOCKETHANDLER]++;
        ThreadSocketHandler2(parg);
        vnThreadsRunning[THREAD_SOCKETHANDLER]--;
    }
    catch (std::exception& e) {
        vnThreadsRunning[THREAD_SOCKETHANDLER]--;
        PrintException(&e, "ThreadSocketHandler()");
    } catch (...) {
        vnThreadsRunning[THREAD_SOCKETHANDLER]--;
        throw; // support pthread_cancel()
    }
    printf("ThreadSocketHandler exited\n");
}

void ThreadSocketHandler2(void* parg)
{
    printf("ThreadSocketHandler started\n");
    list<CNode*> vNodesDisconnected;
    unsigned int nPrevNodeCount = 0;
#ifdef USE_NATIVE_I2P
    int nPrevI2PNodeCount = 0;
#endif

    while (true)
    {
        //
        // Disconnect nodes
        //
        {
            LOCK(cs_vNodes);
            // Disconnect unused nodes
            vector<CNode*> vNodesCopy = vNodes;
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
            {
                if (pnode->fDisconnect ||
                    (pnode->GetRefCount() <= 0 && pnode->vRecvMsg.empty() && pnode->nSendSize == 0 && pnode->ssSend.empty()))
                {
                    // remove from vNodes
                    vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());

                    // release outbound grant (if any)
                    pnode->grantOutbound.Release();

                    // close socket and cleanup
                    pnode->CloseSocketDisconnect();
                    pnode->Cleanup();

                    // hold in disconnected pool until all refs are released
                    if (pnode->fNetworkNode || pnode->fInbound)
                        pnode->Release();
                    vNodesDisconnected.push_back(pnode);
#ifdef USE_NATIVE_I2P
                    if (fNativeI2P) {
                        if (pnode->addr.IsNativeI2P())
                            --nI2PNodeCount;
                    }
#endif
                }
            }

            // Delete disconnected nodes
            list<CNode*> vNodesDisconnectedCopy = vNodesDisconnected;
            BOOST_FOREACH(CNode* pnode, vNodesDisconnectedCopy)
            {
                // wait until threads are done using it
                if (pnode->GetRefCount() <= 0)
                {
                    bool fDelete = false;
                    {
                        TRY_LOCK(pnode->cs_vSend, lockSend);
                        if (lockSend)
                        {
                            TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                            if (lockRecv)
                            {
                                TRY_LOCK(pnode->cs_mapRequests, lockReq);
                                if (lockReq)
                                {
                                    TRY_LOCK(pnode->cs_inventory, lockInv);
                                    if (lockInv)
                                        fDelete = true;
                                }
                            }
                        }
                    }
                    if (fDelete)
                    {
                        vNodesDisconnected.remove(pnode);
                        delete pnode;
                    }
                }
            }
        }
        if (vNodes.size() != nPrevNodeCount)
        {
            nPrevNodeCount = vNodes.size();
            uiInterface.NotifyNumConnectionsChanged(vNodes.size());
        }
#ifdef USE_NATIVE_I2P
if (fNativeI2P) {
        if (nPrevI2PNodeCount != nI2PNodeCount)
        {
            nPrevI2PNodeCount = nI2PNodeCount;
            uiInterface.NotifyNumI2PConnectionsChanged(nI2PNodeCount);
        }
}
#endif


        //
        // Find which sockets have data to receive
        //
        struct timeval timeout;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 50000; // frequency to poll pnode->vSend

        fd_set fdsetRecv;
        fd_set fdsetSend;
        fd_set fdsetError;
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        FD_ZERO(&fdsetError);
        SOCKET hSocketMax = 0;
        bool have_fds = false;

#ifdef USE_NATIVE_I2P
        if (fNativeI2P) {
                BOOST_FOREACH(SOCKET hI2PListenSocket, vhI2PListenSocket) {
                    if (hI2PListenSocket != INVALID_SOCKET)
                    {
                        FD_SET(hI2PListenSocket, &fdsetRecv);
                        hSocketMax = max(hSocketMax, hI2PListenSocket);
                        have_fds = true;
                    }
                }
        }
#endif

        BOOST_FOREACH(SOCKET hListenSocket, vhListenSocket) {
            FD_SET(hListenSocket, &fdsetRecv);
            hSocketMax = max(hSocketMax, hListenSocket);
            have_fds = true;
        }
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
            {
                if (pnode->hSocket == INVALID_SOCKET)
                    continue;
                {
                    TRY_LOCK(pnode->cs_vSend, lockSend);
                    if (lockSend) {
                        // do not read, if draining write queue
                        if (!pnode->vSendMsg.empty())
                            FD_SET(pnode->hSocket, &fdsetSend);
                        else
                            FD_SET(pnode->hSocket, &fdsetRecv);
                        FD_SET(pnode->hSocket, &fdsetError);
                        hSocketMax = max(hSocketMax, pnode->hSocket);
                        have_fds = true;
                    }
                }
            }
        }

        vnThreadsRunning[THREAD_SOCKETHANDLER]--;
        int nSelect = select(have_fds ? hSocketMax + 1 : 0,
                             &fdsetRecv, &fdsetSend, &fdsetError, &timeout);
        vnThreadsRunning[THREAD_SOCKETHANDLER]++;
        if (fShutdown)
            return;
        if (nSelect == SOCKET_ERROR)
        {
            if (have_fds)
            {
                int nErr = WSAGetLastError();
                printf("socket select error %d\n", nErr);
                for (unsigned int i = 0; i <= hSocketMax; i++)
                    FD_SET(i, &fdsetRecv);
            }
            FD_ZERO(&fdsetSend);
            FD_ZERO(&fdsetError);
            MilliSleep(timeout.tv_usec/1000);
        }


        //
        // Accept new connections
        //
#ifdef USE_NATIVE_I2P
        if( !IsI2POnly() )
#endif // Without I2P onlynet, execute the original code
            BOOST_FOREACH(SOCKET hListenSocket, vhListenSocket)
            if (hListenSocket != INVALID_SOCKET && FD_ISSET(hListenSocket, &fdsetRecv))
            {
                struct sockaddr_storage sockaddr;
                socklen_t len = sizeof(sockaddr);
                SOCKET hSocket = accept(hListenSocket, (struct sockaddr*)&sockaddr, &len);
                CAddress addr;
                int nInbound = 0;

                if (hSocket != INVALID_SOCKET)
                    if (!addr.SetSockAddr((const struct sockaddr*)&sockaddr))
                        printf("Warning: Unknown socket family\n");

                {
                    LOCK(cs_vNodes);
                    BOOST_FOREACH(CNode* pnode, vNodes)
                        if (pnode->fInbound)
                            nInbound++;
                }

                if (hSocket == INVALID_SOCKET)
                {
                    int nErr = WSAGetLastError();
                    if (nErr != WSAEWOULDBLOCK)
                        printf("socket error accept failed: %d\n", nErr);
                }
                else if (nInbound >= GetArg("-maxconnections", 125) - MAX_OUTBOUND_CONNECTIONS)
                {
                    closesocket(hSocket);
                }
                else if (CNode::IsBanned(addr))
                {
                    printf("connection from %s dropped (banned)\n", addr.ToString().c_str());
                    closesocket(hSocket);
                }
                else
                {
                    printf("accepted connection %s\n", addr.ToString().c_str());
                    CNode* pnode = new CNode(hSocket, addr, "", true);
                    pnode->AddRef();
                    {
                        LOCK(cs_vNodes);
                        vNodes.push_back(pnode);
                    }
                }
            }
#ifdef USE_NATIVE_I2P
        //
        // Accept new I2P connections
        //
if (fNativeI2P) {
    std::vector<SOCKET>::iterator it = vhI2PListenSocket.begin();       // Start at the beginning of the list of listening sockets
    // As long as the router is still up & there are some sockets to listen on, keep trying to accept new connections
    while( !IsLimited( NET_NATIVE_I2P ) &&  it != vhI2PListenSocket.end() )
    {
        SOCKET& hI2PListenSocket = *it;
        if( hI2PListenSocket == INVALID_SOCKET ) {
            it = vhI2PListenSocket.erase(it);
            if( !BindListenNativeI2P() )
                break;
            it = vhI2PListenSocket.begin();             // Start over from the beginning
            continue;
        }
        // At this point we have a valid socket setup to accept inbound connections, lets see if anyone is knocking...
        if (FD_ISSET(hI2PListenSocket, &fdsetRecv))
        {
            const size_t bufSize = 1024;            // Same as i2pd has set on the other end
            char pchBuf[bufSize];
            memset(pchBuf, 0, bufSize);             // Yap someone is trying, lets find out who
            int nBytes = recv(hI2PListenSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
            if (nBytes > 0)
            {
                // When a '/n' return shows up we got their destination identity
                // See this url for destination specifications https://geti2p.net/en/docs/spec/common-structures#struct_Destination
                // Although over I2P Sam we get it as a base64 string.
                char *pNewLine = strchr( pchBuf, '\n' );
                if( pNewLine ) {                     // Yap the address is all here, pNewLine would be null otherwise.
                    // Lets make sure it looks correct as a base64 i2p destination string
                    if( strlen(pchBuf) == NATIVE_I2P_DESTINATION_SIZE + 1 ) // we're waiting for dest-hash + '\n' symbol
                    {
                        // Fantastic if it checks out, we have another node!
                        // The socket will be bound to that and used for message communications
                        std::string incomingAddr(pchBuf, pchBuf + NATIVE_I2P_DESTINATION_SIZE);
                        CAddress addr;
                        if( addr.SetSpecial(incomingAddr) && addr.IsI2P() )
                            AddIncomingI2pConnection(hI2PListenSocket, addr);
                        else {
                            LogPrintf("WARNING - Invalid incoming destination address, unable to setup node.  Received (%s)\n", incomingAddr.c_str());
                            closesocket(hI2PListenSocket);
                        }
                    } else {
                        LogPrintf("WARNING - New destination addresses not yet supported, size & data received (%d) [%s]", nBytes, pchBuf);
                        closesocket(hI2PListenSocket);
                    }
                } else {
                    LogPrintf("WARNING - No eol found in destination address from router, size & data received (%d) [%s]\n", nBytes, pchBuf);
                    closesocket(hI2PListenSocket);
                }
            }
            else if (nBytes == 0)
            {
                // socket closed gracefully, but why?  This shouldn't have happened
                LogPrintf("WARNING - I2P listen socket was closed unexpectedly, with no data received.  Will attempt to open a new one.\n");
                closesocket(hI2PListenSocket);
            }
            else if (nBytes < 0)
            {
                // error
                const int nErr = WSAGetLastError();
                if (nErr == WSAEWOULDBLOCK || nErr == WSAEMSGSIZE || nErr == WSAEINTR || nErr == WSAEINPROGRESS)
                    continue;

                LogPrintf("WARNING - I2P listen socket recv error %d, Will attempt to open a new one.\n", nErr);
                closesocket(hI2PListenSocket);
            }
            // We now need to invalidate that socket from accepting new inbound connections,
            // it was either closed do to an error, or hopefully a new node was added to our peers list as inbound.
            // It will be sweep away and erased, then a new acceptor added later
            *it++ = INVALID_SOCKET;
        } else                                      // Just keep looking
            it++;
    }
}
#endif


        //
        // Service each socket
        //
        vector<CNode*> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->AddRef();
        }
        BOOST_FOREACH(CNode* pnode, vNodesCopy)
        {
            if (fShutdown)
                return;

            //
            // Receive
            //
            if (pnode->hSocket == INVALID_SOCKET)
                continue;
            if (FD_ISSET(pnode->hSocket, &fdsetRecv) || FD_ISSET(pnode->hSocket, &fdsetError))
            {
                TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                if (lockRecv)
                {
                    if (pnode->GetTotalRecvSize() > ReceiveFloodSize()) {
                        if (!pnode->fDisconnect)
                            printf("socket recv flood control disconnect (%u bytes)\n", pnode->GetTotalRecvSize());
                        pnode->CloseSocketDisconnect();
                    }
                    else {
                        // typical socket buffer is 8K-64K
                        char pchBuf[0x10000];
                        int nBytes = recv(pnode->hSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
                        if (nBytes > 0)
                        {
                            if (!pnode->ReceiveMsgBytes(pchBuf, nBytes))
                                pnode->CloseSocketDisconnect();
                            pnode->nLastRecv = GetTime();
                            pnode->nRecvBytes += nBytes;
                            pnode->RecordBytesRecv(nBytes);
                        }
                        else if (nBytes == 0)
                        {
                            // socket closed gracefully
                            if (!pnode->fDisconnect)
                                printf("socket closed\n");
                            pnode->CloseSocketDisconnect();
                        }
                        else if (nBytes < 0)
                        {
                            // error
                            int nErr = WSAGetLastError();
                            if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
                            {
                                if (!pnode->fDisconnect)
                                    printf("socket recv error %d\n", nErr);
                                pnode->CloseSocketDisconnect();
                            }
                        }
                    }
                }
            }

            //
            // Send
            //
            if (pnode->hSocket == INVALID_SOCKET)
                continue;
            if (FD_ISSET(pnode->hSocket, &fdsetSend))
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)
                    SocketSendData(pnode);
            }

            //
            // Inactivity checking
            //
            int64_t nTime = GetTime();
            if (nTime - pnode->nTimeConnected > 60)
            {
                if (pnode->nLastRecv == 0 || pnode->nLastSend == 0)
                {
                    printf("socket no message in first 60 seconds, %d %d\n", pnode->nLastRecv != 0, pnode->nLastSend != 0);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastSend > TIMEOUT_INTERVAL)
                {
                    printf("socket sending timeout: %" PRId64"s\n", nTime - pnode->nLastSend);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastRecv > (pnode->nVersion > BIP0031_VERSION ? TIMEOUT_INTERVAL : 90*60))
                {
                    printf("socket receive timeout: %" PRId64"s\n", nTime - pnode->nLastRecv);
                    pnode->fDisconnect = true;
                }
                else if (pnode->nPingNonceSent && pnode->nPingUsecStart + TIMEOUT_INTERVAL * 1000000 < GetTimeMicros())
                {
                    printf("ping timeout: %fs\n", 0.000001 * (GetTimeMicros() - pnode->nPingUsecStart));
                    pnode->fDisconnect = true;
                }
            }
        }
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->Release();
        }

        MilliSleep(10);
    }
}


#ifdef USE_UPNP
void ThreadMapPort(void* parg)
{
    // Make this thread recognisable as the UPnP thread
    RenameThread("denarius-UPnP");

    try
    {
        vnThreadsRunning[THREAD_UPNP]++;
        ThreadMapPort2(parg);
        vnThreadsRunning[THREAD_UPNP]--;
    }
    catch (std::exception& e) {
        vnThreadsRunning[THREAD_UPNP]--;
        PrintException(&e, "ThreadMapPort()");
    } catch (...) {
        vnThreadsRunning[THREAD_UPNP]--;
        PrintException(NULL, "ThreadMapPort()");
    }
    printf("ThreadMapPort exited\n");
}

void ThreadMapPort2(void* parg)
{
    printf("ThreadMapPort started\n");

    std::string port = strprintf("%u", GetListenPort());
    const char * multicastif = 0;
    const char * minissdpdpath = 0;
    struct UPNPDev * devlist = 0;
    char lanaddr[64];

#ifndef UPNPDISCOVER_SUCCESS
    /* miniupnpc 1.5 */
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0);
#else
    #if MINIUPNPC_API_VERSION >= 14
        /* miniupnpc API_VERSION 14 and above requires ttl as arg */
        int error = 0;
        devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, 2, &error);
    #else
        /* miniupnpc 1.6 and above */
        int error = 0;
        devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, &error);
    #endif

#endif

    struct UPNPUrls urls;
    struct IGDdatas data;
    int r;

    r = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
    if (r == 1)
    {
        if (fDiscover) {
            char externalIPAddress[40];
            r = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIPAddress);
            if(r != UPNPCOMMAND_SUCCESS)
                printf("UPnP: GetExternalIPAddress() returned %d\n", r);
            else
            {
                if(externalIPAddress[0])
                {
                    printf("UPnP: ExternalIPAddress = %s\n", externalIPAddress);
                    AddLocal(CNetAddr(externalIPAddress), LOCAL_UPNP);
                }
                else
                    printf("UPnP: GetExternalIPAddress failed.\n");
            }
        }

        string strDesc = "Denarius " + FormatFullVersion();
#ifndef UPNPDISCOVER_SUCCESS
        /* miniupnpc 1.5 */
        r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                            port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0);
#else
        /* miniupnpc 1.6 */
        r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                            port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0, "0");
#endif

        if(r!=UPNPCOMMAND_SUCCESS)
            printf("AddPortMapping(%s, %s, %s) failed with code %d (%s)\n",
                port.c_str(), port.c_str(), lanaddr, r, strupnperror(r));
        else
            printf("UPnP Port Mapping successful.\n");
        int i = 1;
        while (true)
        {
            if (fShutdown || !fUseUPnP)
            {
                r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port.c_str(), "TCP", 0);
                printf("UPNP_DeletePortMapping() returned : %d\n", r);
                freeUPNPDevlist(devlist); devlist = 0;
                FreeUPNPUrls(&urls);
                return;
            }
            if (i % 600 == 0) // Refresh every 20 minutes
            {
#ifndef UPNPDISCOVER_SUCCESS
                /* miniupnpc 1.5 */
                r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                    port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0);
#else
                /* miniupnpc 1.6 */
                r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                    port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0, "0");
#endif

                if(r!=UPNPCOMMAND_SUCCESS)
                    printf("AddPortMapping(%s, %s, %s) failed with code %d (%s)\n",
                        port.c_str(), port.c_str(), lanaddr, r, strupnperror(r));
                else
                    printf("UPnP Port Mapping successful.\n");;
            }
            MilliSleep(2000);
            i++;
        }
    } else {
        printf("No valid UPnP IGDs found\n");
        freeUPNPDevlist(devlist); devlist = 0;
        if (r != 0)
            FreeUPNPUrls(&urls);
        while (true)
        {
            if (fShutdown || !fUseUPnP)
                return;
            MilliSleep(2000);
        }
    }
}

void MapPort()
{
    if (fUseUPnP && vnThreadsRunning[THREAD_UPNP] < 1)
    {
        if (!NewThread(ThreadMapPort, NULL))
            printf("Error: ThreadMapPort(ThreadMapPort) failed\n");
    }
}
#else
void MapPort()
{
    // Intentionally left blank.
}
#endif



/* Tor implementation ---------------------------------*/

// hidden service seeds
static const char *strMainNetOnionSeed[][1] = {
    {NULL}
};

static const char *strTestNetOnionSeed[][1] = {
    {NULL}
};

#ifdef USE_NATIVETOR
void ThreadOnionSeed(void* parg)
{
    if(fNativeTor)
    {
        // Make this thread recognisable as the Tor Onion Thread
        RenameThread("toronion");

        static const char *(*strOnionSeed)[1] = fTestNet ? strTestNetOnionSeed : strMainNetOnionSeed;

        int found = 0;
        printf("Loading addresses from .onion seeds\n");

        for (unsigned int seed_idx = 0; strOnionSeed[seed_idx][0] != NULL; seed_idx++) {
            CNetAddr parsed;
            if (!parsed.SetSpecial(strOnionSeed[seed_idx][0]))
            {
                throw runtime_error("ThreadOnionSeed() : invalid .onion seed");
            }
            int nOneDay = 24*3600;
            CAddress addr = CAddress(CService(parsed, GetDefaultPort()));
            addr.nTime = GetTime() - 3*nOneDay - GetRand(4*nOneDay); // use a random age between 3 and 7 days old
            found++;
            addrman.Add(addr, parsed);
        }

        printf("%d addresses found from .onion seeds\n", found);
    }
};
#endif




// DNS seeds
// Each pair gives a source name and a seed name.
// The first name is used as information source for addrman.
// The second name should resolve to a list of seed addresses.

#ifdef USE_NATIVE_I2P
static const char *strI2PDNSSeed[][2] = {
    {"d4gii55rnvv22qm2ojre2n67bzms5utr4k3ckafwjdoym2cqmv2q.b32.i2p", "d4gii55rnvv22qm2ojre2n67bzms5utr4k3ckafwjdoym2cqmv2q.b32.i2p"}, // need some valid I2P DNS seeds
    {"dd37luxnbh3hkihxfl2e7nwosebh5sbfvpvjqwn7c3g5kqftb5qq.b32.i2p", "dd37luxnbh3hkihxfl2e7nwosebh5sbfvpvjqwn7c3g5kqftb5qq.b32.i2p"}
};
#endif

static const char *strDNSSeed[][2] = {
    {"dnsseed.hashbag.cc", "dnsseed.hashbag.cc"},
    {"seed.denarius.host", "seed.denarius.host"},
	{"denariusseed.swarmpvp.com", "denariusseed.swarmpvp.com"}
};

void ThreadDNSAddressSeed(void* parg)
{
    if(!fNativeTor)
    {
        // Make this thread recognisable as the DNS seeding thread
        RenameThread("denarius-dnsseed");

        try
        {
            vnThreadsRunning[THREAD_DNSSEED]++;
            ThreadDNSAddressSeed2(parg);
            vnThreadsRunning[THREAD_DNSSEED]--;
        }
        catch (std::exception& e) {
            vnThreadsRunning[THREAD_DNSSEED]--;
            PrintException(&e, "ThreadDNSAddressSeed()");
        } catch (...) {
            vnThreadsRunning[THREAD_DNSSEED]--;
            throw; // support pthread_cancel()
        }
        printf("ThreadDNSAddressSeed exited\n");
    }
};

void ThreadDNSAddressSeed2(void* parg)
{
    if(!fNativeTor)
    {
        printf("ThreadDNSAddressSeed started\n");
        int found = 0;

        if (!fTestNet)
        {
            printf("Loading addresses from DNS seeds (could take a while)\n");
#ifdef USE_NATIVE_I2P
            if (fNativeI2P) {
                for (unsigned int seed_idx = 0; seed_idx < ARRAYLEN(strI2PDNSSeed); seed_idx++) {
                    if (HaveNameProxy()) {
                        AddOneShot(strI2PDNSSeed[seed_idx][1]);
                    } else {
                        vector<CNetAddr> vaddr;
                        vector<CAddress> vAdd;
                        if (LookupHost(strI2PDNSSeed[seed_idx][1], vaddr)) {
                            BOOST_FOREACH(CNetAddr& ip, vaddr) {
                                int nOneDay = 24*3600;
                                CAddress addr = CAddress(CService(ip, GetDefaultPort()));
                                addr.nTime = GetTime() - 3*nOneDay - GetRand(4*nOneDay); // use a random age between 3 and 7 days old
                                vAdd.push_back(addr);
                                found++;
                            }
                        }
                        addrman.Add(vAdd, CNetAddr(strI2PDNSSeed[seed_idx][0], true));
                    }
                }
                // Prefer I2P, if 4 is found, drop the clearnet dnsseed
                if (found>4)
                    return;
                if( IsI2POnly() )                                                           // Do what we normally would do on clearnet
                    printf("Skipping Clearnet DNS seeds, Running I2P net only.\n");
            }
#endif
            for (unsigned int seed_idx = 0; seed_idx < ARRAYLEN(strDNSSeed); seed_idx++) {
                if (HaveNameProxy()) {
                    AddOneShot(strDNSSeed[seed_idx][1]);
                } else {
                    vector<CNetAddr> vaddr;
                    vector<CAddress> vAdd;
                    if (LookupHost(strDNSSeed[seed_idx][1], vaddr))
                    {
                        BOOST_FOREACH(CNetAddr& ip, vaddr)
                        {
                            int nOneDay = 24*3600;
                            CAddress addr = CAddress(CService(ip, GetDefaultPort()));
                            addr.nTime = GetTime() - 3*nOneDay - GetRand(4*nOneDay); // use a random age between 3 and 7 days old
                            vAdd.push_back(addr);
                            found++;
                        }
                    }
                    addrman.Add(vAdd, CNetAddr(strDNSSeed[seed_idx][0], true));
                }
            }
        }

        printf("%d addresses found from DNS seeds\n", found);
    }
};


unsigned int pnSeed[] =
{
    0x42ac0c50,
};

#ifdef USE_NATIVE_I2P
const std::string pstrI2PSeed[] = {
    "Dw-NRnXaxoGZdyupTXX78~CmF8csc~RhJr8XR2vMbNpazKhGjWNfRjhCmwqXkkW9vwkNjovW2AAbof7PfVnMCff0sHSxMTiBNsH8cuHJS2ckBSJI3h4G4ffJLc5gflangrG1raHKrMXCw8Cn56pisx4RKokEKUYdeEPiMdyJUO5yjZW2oyk4NpaUaQCqFmcglIvNOCYzVe~LK124wjJQAJc5iME1Sg9sOHaGMPL5N2qkAm5osOg2S7cZNRdIkoNOq-ztxghrv5bDL0ybeC0sfIQzvxDiKugCrSHEHCvwkA~xOu9nhNlUvoPDyCRRi~ImeomdNoqke28di~h2JF7wBGE~3ACxxOMaa0I~c9LV3O7pRU2Xj9HDn76eMGL7YCcCU4dRByu97oqfB3E~qqmmFp8W1tgvnEAMtXFTFZPYc33ZaCaIJQD7UXcQRSRV7vjw39jhx49XFsmYV3K6~D8bN8U4sRJnKQpzOJGpOSEJWh88bII0XuA55bJsfrR4VEQ5AAAA", // need valid
    "dL51K8bxVvqX2Z4epXReUYlWTNUAK-1XdlbBd7e796s2A3icCQRhJvGlaa6PX~tgPvos-2IJp9hFQrVa0lyKyYmpQN9X7GOErtsL-JbMQpglQsEd94jDRAsiBuvgyPZij~NNdBxKRMDvNm9s7eovzhEFTAimTSB-sgeZ4Afxx2IrNXDFM6KS8AUm8YsaMldzX9zKQDeuV0slp4ZfIAVQhZZy9zTZSmUNPnXQR7XPh5w7FkXzmKMTKSyG~layJ3AorQWqzZXmykzf4z3CE4zkzQcwc0~ZIcAg9tYvM2AdQIdgeN6ISMim6L8q6ku6abuONkyw-NJTi3NopeGHZva21Tc3uHetsKoW434N24HBVUtIjJVjGsbZ7xBfz2xM5kyhPl6SlD-RJarCw47Rovmfc9Piq6q3S~Zw-rvRl-xDMJzwraIYNjAROouDwjI9Bqnguq9DH5uFBxf4uN69X7T~yWTAjdvelZKp6BGe~HGo7bNQjBmymhlH4erCKZEXOaxDAAAA"
};
#endif


void DumpAddresses()
{
    int64_t nStart = GetTimeMillis();

    CAddrDB adb;
    adb.Write(addrman);

    printf("Flushed %d addresses to peers.dat  %" PRId64"ms\n",
           addrman.size(), GetTimeMillis() - nStart);
}

void ThreadDumpAddress2(void* parg)
{
    vnThreadsRunning[THREAD_DUMPADDRESS]++;
    while (!fShutdown)
    {
        DumpAddresses();
        vnThreadsRunning[THREAD_DUMPADDRESS]--;
        MilliSleep(600000);
        vnThreadsRunning[THREAD_DUMPADDRESS]++;
    }
    vnThreadsRunning[THREAD_DUMPADDRESS]--;
}

void ThreadDumpAddress(void* parg)
{
    // Make this thread recognisable as the address dumping thread
    RenameThread("denarius-adrdump");

    try
    {
        ThreadDumpAddress2(parg);
    }
    catch (std::exception& e) {
        PrintException(&e, "ThreadDumpAddress()");
    }
    printf("ThreadDumpAddress exited\n");
}

void ThreadOpenConnections(void* parg)
{
    // Make this thread recognisable as the connection opening thread
    RenameThread("denarius-opencon");

    try
    {
        vnThreadsRunning[THREAD_OPENCONNECTIONS]++;
        ThreadOpenConnections2(parg);
        vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
    }
    catch (std::exception& e) {
        vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
        PrintException(&e, "ThreadOpenConnections()");
    } catch (...) {
        vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
        PrintException(NULL, "ThreadOpenConnections()");
    }
    printf("ThreadOpenConnections exited\n");
}

void static ProcessOneShot()
{
    string strDest;
    {
        LOCK(cs_vOneShots);
        if (vOneShots.empty())
            return;
        strDest = vOneShots.front();
        vOneShots.pop_front();
    }
    CAddress addr;
    CSemaphoreGrant grant(*semOutbound, true);
    if (grant) {
        if (!OpenNetworkConnection(addr, &grant, strDest.c_str(), true))
            AddOneShot(strDest);
    }
}

void static ThreadStakeMiner(void* parg)
{
    printf("ThreadStakeMiner started\n");
    CWallet* pwallet = (CWallet*)parg;
    try
    {
        vnThreadsRunning[THREAD_STAKE_MINER]++;
        StakeMiner(pwallet);
        vnThreadsRunning[THREAD_STAKE_MINER]--;
    }
    catch (std::exception& e) {
        vnThreadsRunning[THREAD_STAKE_MINER]--;
        PrintException(&e, "ThreadStakeMiner()");
    } catch (...) {
        vnThreadsRunning[THREAD_STAKE_MINER]--;
        PrintException(NULL, "ThreadStakeMiner()");
    }
    printf("ThreadStakeMiner exiting, %d threads remaining\n", vnThreadsRunning[THREAD_STAKE_MINER]);
}

void ThreadOpenConnections2(void* parg)
{
    printf("ThreadOpenConnections started\n");

    // Connect to specific addresses
    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0)
    {
        for (int64_t nLoop = 0;; nLoop++)
        {
            ProcessOneShot();
            BOOST_FOREACH(string strAddr, mapMultiArgs["-connect"])
            {
                CAddress addr;
                OpenNetworkConnection(addr, NULL, strAddr.c_str());
                for (int i = 0; i < 10 && i < nLoop; i++)
                {
                    MilliSleep(500);
                    if (fShutdown)
                        return;
                }
            }
            MilliSleep(500);
        }
    }

    // Initiate network connections
    int64_t nStart = GetTime();
    while (true)
    {
        ProcessOneShot();

        vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
        MilliSleep(500);
        vnThreadsRunning[THREAD_OPENCONNECTIONS]++;
        if (fShutdown)
            return;


        vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
        CSemaphoreGrant grant(*semOutbound);
        vnThreadsRunning[THREAD_OPENCONNECTIONS]++;
        if (fShutdown)
            return;

        // Add seed nodes if IRC isn't working
        if (addrman.size()==0 && (GetTime() - nStart > 60) && !fTestNet)
        {
            std::vector<CAddress> vAdd;
            for (unsigned int i = 0; i < ARRAYLEN(pnSeed); i++)
            {
                // It'll only connect to one or two seed nodes because once it connects,
                // it'll get a pile of addresses with newer timestamps.
                // Seed nodes are given a random 'last seen time' of between one and two
                // weeks ago.
                const int64_t nOneWeek = 7*24*60*60;
                struct in_addr ip;
                memcpy(&ip, &pnSeed[i], sizeof(ip));
                CAddress addr(CService(ip, GetDefaultPort()));
                addr.nTime = GetTime()-GetRand(nOneWeek)-nOneWeek;
                vAdd.push_back(addr);
            }
#ifdef USE_NATIVE_I2P
            if (fNativeI2P) {
                for (size_t i = 0; i < ARRAYLEN(pstrI2PSeed); i++)
                {
                    const int64_t nOneWeek = 7*24*60*60;
                    CAddress addr(CService((const std::string&)pstrI2PSeed[i]));
                    addr.nTime = GetTime()-GetRand(nOneWeek)-nOneWeek;
                    vAdd.push_back(addr);
                }
            }
#endif
            addrman.Add(vAdd, CNetAddr("127.0.0.1"));
        }

        //
        // Choose an address to connect to based on most recently seen
        //
        CAddress addrConnect;

        // Only connect out to one peer per network group (/16 for IPv4).
        // Do this here so we don't have to critsect vNodes inside mapAddresses critsect.
        int nOutbound = 0;
        set<vector<unsigned char> > setConnected;
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes) {
                if (!pnode->fInbound) {
                    setConnected.insert(pnode->addr.GetGroup());
                    nOutbound++;
                }
            }
        }

        int64_t nANow = GetAdjustedTime();

        int nTries = 0;
        while (true)
        {
            // use an nUnkBias between 10 (no outgoing connections) and 90 (8 outgoing connections)
            CAddress addr = addrman.Select(10 + min(nOutbound,8)*10);

            // if we selected an invalid address, restart
            if (!addr.IsValid() || setConnected.count(addr.GetGroup()) || IsLocal(addr))
                break;

            // If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
            // stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
            // already-connected network ranges, ...) before trying new addrman addresses.
            nTries++;
            if (nTries > 100)
                break;

            if (IsLimited(addr))
                continue;

            // only consider very recently tried nodes after 30 failed attempts
            if (nANow - addr.nLastTry < 600 && nTries < 30)
                continue;

            // do not allow non-default ports, unless after 50 invalid addresses selected already
#ifdef USE_NATIVE_I2P
            if (fNativeI2P) {
                if (!addr.IsNativeI2P() && addr.GetPort() != GetDefaultPort() && nTries < 50)
                    continue;
            } else {
                if (addr.GetPort() != GetDefaultPort() && nTries < 50)
                    continue;
            }
#else
            if (addr.GetPort() != GetDefaultPort() && nTries < 50)
                continue;
#endif

            addrConnect = addr;
            break;
        }

        if (addrConnect.IsValid())
            OpenNetworkConnection(addrConnect, &grant);
    }
}

void ThreadOpenAddedConnections(void* parg)
{
    // Make this thread recognisable as the connection opening thread
    RenameThread("denarius-opencon");

    try
    {
        vnThreadsRunning[THREAD_ADDEDCONNECTIONS]++;
        ThreadOpenAddedConnections2(parg);
        vnThreadsRunning[THREAD_ADDEDCONNECTIONS]--;
    }
    catch (std::exception& e) {
        vnThreadsRunning[THREAD_ADDEDCONNECTIONS]--;
        PrintException(&e, "ThreadOpenAddedConnections()");
    } catch (...) {
        vnThreadsRunning[THREAD_ADDEDCONNECTIONS]--;
        PrintException(NULL, "ThreadOpenAddedConnections()");
    }
    printf("ThreadOpenAddedConnections exited\n");
}

void ThreadOpenAddedConnections2(void* parg)
{
    {
        LOCK(cs_vAddedNodes);
        vAddedNodes = mapMultiArgs["-addnode"];
    }

    if (HaveNameProxy()) {
        while(true) {
            list<string> lAddresses(0);
            {
                LOCK(cs_vAddedNodes);
                BOOST_FOREACH(string& strAddNode, vAddedNodes)
                    lAddresses.push_back(strAddNode);
            }
            BOOST_FOREACH(string& strAddNode, lAddresses) {
                CAddress addr;
                CSemaphoreGrant grant(*semOutbound);
                OpenNetworkConnection(addr, &grant, strAddNode.c_str());
                MilliSleep(500);
            }
			if (fShutdown)
                return;
            vnThreadsRunning[THREAD_ADDEDCONNECTIONS]--;
            MilliSleep(120000); // Retry every 2 minutes
			vnThreadsRunning[THREAD_ADDEDCONNECTIONS]++;
            if (fShutdown)
                return;
        }
    }

    for (unsigned int i = 0; true; i++)
    {
        list<string> lAddresses(0);
        {
            LOCK(cs_vAddedNodes);
            BOOST_FOREACH(string& strAddNode, vAddedNodes)
                lAddresses.push_back(strAddNode);
        }

        list<vector<CService> > lservAddressesToAdd(0);
        BOOST_FOREACH(string& strAddNode, lAddresses)
        {
            vector<CService> vservNode(0);
#ifdef USE_NATIVE_I2P
            if(Lookup(strAddNode.c_str(), vservNode, isStringI2pDestination(strAddNode) ? 0 : GetDefaultPort(), fNameLookup, 0))
#else
            if(Lookup(strAddNode.c_str(), vservNode, GetDefaultPort(), fNameLookup, 0))
#endif
            {
                lservAddressesToAdd.push_back(vservNode);
                {
                    LOCK(cs_setservAddNodeAddresses);
                    BOOST_FOREACH(CService& serv, vservNode)
                        setservAddNodeAddresses.insert(serv);
                }
            }
        }
        // Attempt to connect to each IP for each addnode entry until at least one is successful per addnode entry
        // (keeping in mind that addnode entries can have many IPs if fNameLookup)
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
                for (list<vector<CService> >::iterator it = lservAddressesToAdd.begin(); it != lservAddressesToAdd.end(); it++)
                    BOOST_FOREACH(CService& addrNode, *(it))
                        if (pnode->addr == addrNode)
                        {
                            it = lservAddressesToAdd.erase(it);
                            it--;
                            break;
                        }
        }
        BOOST_FOREACH(vector<CService>& vserv, lservAddressesToAdd)
        {
            CSemaphoreGrant grant(*semOutbound);
            OpenNetworkConnection(CAddress(vserv[i % vserv.size()]), &grant);
            MilliSleep(500);
			if (fShutdown)
                return;
        }
		vnThreadsRunning[THREAD_ADDEDCONNECTIONS]--;
        MilliSleep(120000); // Retry every 2 minutes
        vnThreadsRunning[THREAD_ADDEDCONNECTIONS]++;
        if (fShutdown)
            return;
    }
}

// if successful, this moves the passed grant to the constructed node
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound, const char *strDest, bool fOneShot)
{
    //
    // Initiate outbound network connection
    //
    if (fShutdown)
        return false;
    if (!strDest)
        if (IsLocal(addrConnect) ||
            FindNode((CNetAddr)addrConnect) || CNode::IsBanned(addrConnect) ||
            FindNode(addrConnect.ToStringIPPort().c_str()))
            return false;
    if (strDest && FindNode(strDest))
        return false;

    vnThreadsRunning[THREAD_OPENCONNECTIONS]--;
    CNode* pnode = ConnectNode(addrConnect, strDest);
    vnThreadsRunning[THREAD_OPENCONNECTIONS]++;
    if (fShutdown)
        return false;
    if (!pnode)
        return false;
    if (grantOutbound)
        grantOutbound->MoveTo(pnode->grantOutbound);
    pnode->fNetworkNode = true;
    if (fOneShot)
        pnode->fOneShot = true;

    return true;
}



// for now, use a very simple selection metric: the node from which we received
// most recently
// patch: use pingtime, e.g. 200ms * -1 = -200, so 10ms *-1 = -10 would win.
static int64_t NodeSyncScore(const CNode *pnode) {
    if (pnode->nPingUsecTime > 0)
        return pnode->nPingUsecTime * -1;
    return pnode->nLastRecv;
}

void static StartSync(const vector<CNode*> &vNodes) {
    CNode *pnodeNewSync = NULL;
    int64_t nBestScore = 0;

    // fImporting and fReindex are accessed out of cs_main here, but only
    // as an optimization - they are checked again in SendMessages.
    if (fImporting || fReindex)
        return;

    // Iterate over all nodes
    BOOST_FOREACH(CNode* pnode, vNodes) {
        // check preconditions for allowing a sync
        if (!pnode->fClient && !pnode->fOneShot &&
            !pnode->fDisconnect && pnode->fSuccessfullyConnected &&
            (pnode->nStartingHeight > (nBestHeight - 144)) &&
            (pnode->nVersion < NOBLKS_VERSION_START || pnode->nVersion >= NOBLKS_VERSION_END)) {
            // if ok, compare node's score with the best so far
            int64_t nScore = NodeSyncScore(pnode);
            if (pnodeNewSync == NULL || nScore > nBestScore) {
                pnodeNewSync = pnode;
                nBestScore = nScore;
            }
        }
    }
    // if a new sync candidate was found, start sync!
    if (pnodeNewSync) {
        pnodeNewSync->fStartSync = true;
        pnodeSync = pnodeNewSync;
    }
}




void ThreadMessageHandler(void* parg)
{
    // Make this thread recognisable as the message handling thread
    RenameThread("denarius-msghand");

    try
    {
        vnThreadsRunning[THREAD_MESSAGEHANDLER]++;
        ThreadMessageHandler2(parg);
        vnThreadsRunning[THREAD_MESSAGEHANDLER]--;
    }
    catch (std::exception& e) {
        vnThreadsRunning[THREAD_MESSAGEHANDLER]--;
        PrintException(&e, "ThreadMessageHandler()");
    } catch (...) {
        vnThreadsRunning[THREAD_MESSAGEHANDLER]--;
        PrintException(NULL, "ThreadMessageHandler()");
    }
    printf("ThreadMessageHandler exited\n");
}

void ThreadMessageHandler2(void* parg)
{
    printf("ThreadMessageHandler started\n");
    SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
    while (!fShutdown)
    {
		bool fHaveSyncNode = false;

        vector<CNode*> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
			BOOST_FOREACH(CNode* pnode, vNodesCopy) {
                pnode->AddRef();
                if (pnode == pnodeSync && pnode->nLastRecv > GetTime() - 5) // only accept a node who has replied in last 5 secs, if they stop then swap nodes
                    fHaveSyncNode = true;
            }
        }

		if (!fHaveSyncNode)
            StartSync(vNodesCopy);

        bool fSleep = true;

        // Poll the connected nodes for messages
        CNode* pnodeTrickle = NULL;
        if (!vNodesCopy.empty())
            pnodeTrickle = vNodesCopy[GetRand(vNodesCopy.size())];
        BOOST_FOREACH(CNode* pnode, vNodesCopy)
        {
            if (pnode->fDisconnect)
                continue;

            // Receive messages
            {
                TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                if (lockRecv)
                {
                    if (!ProcessMessages(pnode))
                        pnode->CloseSocketDisconnect();

                      if (!pnode->vRecvGetData.empty() || (!pnode->vRecvMsg.empty() && pnode->vRecvMsg[0].complete()))
                      {
                          fSleep = false; // don't sleep, we have work to do!
                      }
                }
            }

            boost::this_thread::interruption_point();

            if (fShutdown)
                return;

            // Send messages
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)
                    SendMessages(pnode, pnode == pnodeTrickle);
            }
            if (fShutdown)
                return;
        }

        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->Release();
        }

        // Wait and allow messages to bunch up.
        // Reduce vnThreadsRunning so StopNode has permission to exit while
        // we're sleeping, but we must always check fShutdown after doing this.
        vnThreadsRunning[THREAD_MESSAGEHANDLER]--;
        if (fSleep)
            MilliSleep(100);
        if (fRequestShutdown)
            StartShutdown();
        vnThreadsRunning[THREAD_MESSAGEHANDLER]++;
        if (fShutdown)
            return;
    }
}



#ifdef USE_NATIVE_I2P
bool BindListenNativeI2P()
{
    SOCKET hNewI2PListenSocket = INVALID_SOCKET;
    if (!BindListenNativeI2P(hNewI2PListenSocket))
        return false;
    vhI2PListenSocket.push_back(hNewI2PListenSocket);
    return true;
}

bool BindListenNativeI2P(SOCKET& hSocket)
{
    if( !IsLimited( NET_I2P ) ) {
        hSocket = I2PSession::Instance().accept(false);
        if (SetSocketNonBlocking(hSocket, true)) { // Set to non-blocking
            string sDest = GetArg( "-i2p.mydestination.publickey", "" );
            CService addrBind( sDest, 0 );
            return AddLocal( addrBind, LOCAL_BIND );
        } else
            printf( "BindListenNativeI2P() ERROR - Unable to set I2P Socket options to non-blocking, after I2P accept was issued.\n" );
    }
    else
        printf( "BindListenNativeI2P() ERROR - Unexpected I2P BIND request. Ignored, network access is limited.\n" );

    return false;
}

bool IsI2POnly()
{
//    bool i2pOnly = false;
//    if (mapArgs.count("-onlynet"))
//    {
//        const std::vector<std::string>& onlyNets = mapMultiArgs["-onlynet"];
//        i2pOnly = (onlyNets.size() == 1 && onlyNets[0] == NATIVE_I2P_NET_STRING);
//    }
//    return i2pOnly;

    bool i2pOnly = NET_MAX > 0; // if NET_MAX == 0 we set i2pOnly to false and exit
    for (int n = 0; n < NET_MAX; n++)
    {
        Network net = (Network)n;
        if (net == NET_UNROUTABLE)
            continue;
        i2pOnly &= ((net == NET_NATIVE_I2P) != IsLimited(net)); // isI2P xor IsLimited
    }
    return i2pOnly;
}
#endif


bool BindListenPort(const CService &addrBind, string& strError)
{
    strError = "";
    int nOne = 1;

#ifdef WIN32
    // Initialize Windows Sockets
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (ret != NO_ERROR)
    {
        strError = strprintf("Error: TCP/IP socket library failed to start (WSAStartup returned error %d)", ret);
        printf("%s\n", strError.c_str());
        return false;
    }
#endif

    // Create socket for listening for incoming connections
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!addrBind.GetSockAddr((struct sockaddr*)&sockaddr, &len))
    {
        strError = strprintf("Error: bind address family for %s not supported", addrBind.ToString().c_str());
        printf("%s\n", strError.c_str());
        return false;
    }

    SOCKET hListenSocket = socket(((struct sockaddr*)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (hListenSocket == INVALID_SOCKET)
    {
        strError = strprintf("Error: Couldn't open socket for incoming connections (socket returned error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        return false;
    }

#ifdef SO_NOSIGPIPE
    // Different way of disabling SIGPIPE on BSD
    setsockopt(hListenSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&nOne, sizeof(int));
#endif

#ifndef WIN32
    // Allow binding if the port is still in TIME_WAIT state after
    // the program was closed and restarted.  Not an issue on windows.
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (void*)&nOne, sizeof(int));
#endif


#ifdef WIN32
    // Set to non-blocking, incoming connections will also inherit this
    if (ioctlsocket(hListenSocket, FIONBIO, (u_long*)&nOne) == SOCKET_ERROR)
#else
    if (fcntl(hListenSocket, F_SETFL, O_NONBLOCK) == SOCKET_ERROR)
#endif
    {
        strError = strprintf("Error: Couldn't set properties on socket for incoming connections (error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        return false;
    }

    // some systems don't have IPV6_V6ONLY but are always v6only; others do have the option
    // and enable it by default or not. Try to enable it, if possible.
    if (addrBind.IsIPv6()) {
#ifdef IPV6_V6ONLY
#ifdef WIN32
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&nOne, sizeof(int));
#else
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&nOne, sizeof(int));
#endif
#endif
#ifdef WIN32
        int nProtLevel = 10 /* PROTECTION_LEVEL_UNRESTRICTED */;
        int nParameterId = 23 /* IPV6_PROTECTION_LEVEl */;
        // this call is allowed to fail
        setsockopt(hListenSocket, IPPROTO_IPV6, nParameterId, (const char*)&nProtLevel, sizeof(int));
#endif
    }

    if (::bind(hListenSocket, (struct sockaddr*)&sockaddr, len) == SOCKET_ERROR)
    {
        int nErr = WSAGetLastError();
        if (nErr == WSAEADDRINUSE)
            strError = strprintf(_("Unable to bind to %s on this computer. Denarius is probably already running."), addrBind.ToString().c_str());
        else
            strError = strprintf(_("Unable to bind to %s on this computer (bind returned error %d, %s)"), addrBind.ToString().c_str(), nErr, strerror(nErr));
        printf("%s\n", strError.c_str());
        return false;
    }
    printf("Bound to %s\n", addrBind.ToString().c_str());

    // Listen for incoming connections
    if (listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        strError = strprintf("Error: Listening for incoming connections failed (listen returned error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        return false;
    }

    vhListenSocket.push_back(hListenSocket);

    if (addrBind.IsRoutable() && fDiscover)
        AddLocal(addrBind, LOCAL_BIND);

    return true;
}


void static Discover()
{
    if (!fDiscover || fNativeTor)
        return;

#ifdef WIN32
    // Get local host IP
    char pszHostName[1000] = "";
    if (gethostname(pszHostName, sizeof(pszHostName)) != SOCKET_ERROR)
    {
        vector<CNetAddr> vaddr;
        if (LookupHost(pszHostName, vaddr))
        {
            BOOST_FOREACH (const CNetAddr &addr, vaddr)
            {
                AddLocal(addr, LOCAL_IF);
            }
        }
    }
#else
    // Get local host ip
    struct ifaddrs* myaddrs;
    if (getifaddrs(&myaddrs) == 0)
    {
        for (struct ifaddrs* ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == NULL) continue;
            if ((ifa->ifa_flags & IFF_UP) == 0) continue;
            if (strcmp(ifa->ifa_name, "lo") == 0) continue;
            if (strcmp(ifa->ifa_name, "lo0") == 0) continue;
            if (ifa->ifa_addr->sa_family == AF_INET)
            {
                struct sockaddr_in* s4 = (struct sockaddr_in*)(ifa->ifa_addr);
                CNetAddr addr(s4->sin_addr);
                if (AddLocal(addr, LOCAL_IF))
                    printf("IPv4 %s: %s\n", ifa->ifa_name, addr.ToString().c_str());
            }
            else if (ifa->ifa_addr->sa_family == AF_INET6)
            {
                struct sockaddr_in6* s6 = (struct sockaddr_in6*)(ifa->ifa_addr);
                CNetAddr addr(s6->sin6_addr);
                if (AddLocal(addr, LOCAL_IF))
                    printf("IPv6 %s: %s\n", ifa->ifa_name, addr.ToString().c_str());
            }
        }
        freeifaddrs(myaddrs);
    }
#endif

    // Don't use external IPv4 discovery, when -onlynet="IPv6"
    if (!IsLimited(NET_IPV4))
        NewThread(ThreadGetMyExternalIP, NULL);
}

static char *convert_str(const std::string &s) {
    char *pc = new char[s.size()+1];
    std::strcpy(pc, s.c_str());
    return pc;
}

#ifdef USE_NATIVETOR
// Start Tor Threads
static void run_tor() {
  if(fNativeTor)
  {
      printf("Tor Onion Thread Started!\n");

      std::string logDecl = "notice file " + GetDataDir().string() + "/tor/tor.log";
      char *argvLogDecl = (char*) logDecl.c_str();
      std::string rc = GetDataDir().string() + "/tor/torrc";
      char *rc_c = (char*) rc.c_str();

      char * clientTransportPlugin = NULL;

      struct stat sb;

      if ((stat("obfs4proxy", &sb) == 0 && sb.st_mode & S_IXUSR) || !std::system("which obfs4proxy")) {
          clientTransportPlugin = "obfs4 exec /usr/bin/obfs4proxy";
      } else if (stat("obfs4proxy.exe", &sb) == 0 && sb.st_mode & S_IXUSR) {
          clientTransportPlugin = "obfs4 exec obfs4proxy.exe";
      }

      if (clientTransportPlugin != NULL) {
          printf("Using OBFS4.\n");
          char* argv[] = {
            "tor",
            "--Log", argvLogDecl,
            "--SocksPort", "9089",
            "--ClientTransportPlugin", clientTransportPlugin,
            "--UseBridges", "1",
            "--Bridge", "38.229.33.135:443 8CF38F8AC7CA1ACF6051CACE5C84F3E5B3832CD1",
            "--Bridge", "obfs4 94.242.249.2:44939 E53EEA7DE6E170328F0A2C4338EE4E4DC2398218 cert=VpistQqdnS5zgkARR3he8rt03OrKhk2oobUUhLmFWAWK27pYMvjrBi6zAn1ebIcPH2xbcQ iat-mode=0",
            "--Bridge", "178.63.238.40:456 8E9B0C1B87837FF6CA730CDFB2A59DAB2D85DF08",
            "--ignore-missing-torrc",
            "-f", rc_c,
          };
          tor_main(16, argv);
      }
      else {
          printf("No OBFS4 found, not using it.\n");
          char* argv[] = {
            "tor",
            "--Log", argvLogDecl,
            "--SocksPort", "9089",
            "--ignore-missing-torrc",
            "-f", rc_c,
          };
          tor_main(6, argv);
      }
  }
}

void StartTor(void* parg)
{
  if(fNativeTor)
  {
      // Make this thread recognisable as the onion thread
      RenameThread("onion");
      try
      {
        run_tor();
      }
      catch (std::exception& e) {
        PrintException(&e, "StartTor()");
      }
      printf("Onion thread exited.");
  }
}
#endif

void StartNode(void* parg)
{
    // Make this thread recognisable as the startup thread
    RenameThread("denarius-start");

    if (semOutbound == NULL) {
        // initialize semaphore
        int nMaxOutbound = min(MAX_OUTBOUND_CONNECTIONS, (int)GetArg("-maxconnections", 125));
        semOutbound = new CSemaphore(nMaxOutbound);
    }

    if (pnodeLocalHost == NULL)
        pnodeLocalHost = new CNode(INVALID_SOCKET, CAddress(CService("127.0.0.1", 0), nLocalServices));

    Discover();

    //
    // Start threads
    //

    if(!fNativeTor)
    {
        if (!GetBoolArg("-dnsseed", true))
            printf("DNS seeding disabled\n");
        else
            if (!NewThread(ThreadDNSAddressSeed, NULL))
                printf("Error: NewThread(ThreadDNSAddressSeed) failed\n");
    } else
    {
#ifdef USE_NATIVETOR
        // start the onion seeder
        if (!GetBoolArg("-onionseed", true))
            printf(".onion seeding disabled\n");
        else
            if (!NewThread(ThreadOnionSeed, NULL))
				printf("Error: could not start .onion seeding\n");
#endif
    };

    // Map ports with UPnP
    if (fUseUPnP)
        MapPort();

    // Send and receive from sockets, accept connections
    if (!NewThread(ThreadSocketHandler, NULL))
        printf("Error: NewThread(ThreadSocketHandler) failed\n");

    // Initiate outbound connections from -addnode
    if (!NewThread(ThreadOpenAddedConnections, NULL))
        printf("Error: NewThread(ThreadOpenAddedConnections) failed\n");

    // Initiate outbound connections
    if (!NewThread(ThreadOpenConnections, NULL))
        printf("Error: NewThread(ThreadOpenConnections) failed\n");

    // Process messages
    if (!NewThread(ThreadMessageHandler, NULL))
        printf("Error: NewThread(ThreadMessageHandler) failed\n");

    // Dump network addresses
    if (!NewThread(ThreadDumpAddress, NULL))
        printf("Error; NewThread(ThreadDumpAddress) failed\n");

    // Mine proof-of-stake blocks in the background
    if (!GetBoolArg("-staking", true))
        printf("Staking disabled\n");
    else
        if (!NewThread(ThreadStakeMiner, pwalletMain))
            printf("Error: NewThread(ThreadStakeMiner) failed\n");
}

bool StopNode()
{
    printf("StopNode()\n");
    fShutdown = true;
    //MapPort(false);
    mempool.AddTransactionsUpdated(1);
    int64_t nStart = GetTime();
    if (semOutbound)
        for (int i=0; i<MAX_OUTBOUND_CONNECTIONS; i++)
            semOutbound->post();
    do
    {
        int nThreadsRunning = 0;
        for (int n = 0; n < THREAD_MAX; n++)
            nThreadsRunning += vnThreadsRunning[n];
        if (nThreadsRunning == 0)
            break;
        if (GetTime() - nStart > 20)
            break;
        MilliSleep(20);
    } while(true);
    if (vnThreadsRunning[THREAD_SOCKETHANDLER] > 0) printf("ThreadSocketHandler still running\n");
    if (vnThreadsRunning[THREAD_OPENCONNECTIONS] > 0) printf("ThreadOpenConnections still running\n");
    if (vnThreadsRunning[THREAD_MESSAGEHANDLER] > 0) printf("ThreadMessageHandler still running\n");
    if (vnThreadsRunning[THREAD_RPCLISTENER] > 0) printf("ThreadRPCListener still running\n");
    if (vnThreadsRunning[THREAD_RPCHANDLER] > 0) printf("ThreadsRPCServer still running\n");
#ifdef USE_UPNP
    if (vnThreadsRunning[THREAD_UPNP] > 0) printf("ThreadMapPort still running\n");
#endif
    if (vnThreadsRunning[THREAD_DNSSEED] > 0) printf("ThreadDNSAddressSeed still running\n");
    if (vnThreadsRunning[THREAD_ADDEDCONNECTIONS] > 0) printf("ThreadOpenAddedConnections still running\n");
    if (vnThreadsRunning[THREAD_DUMPADDRESS] > 0) printf("ThreadDumpAddresses still running\n");
    if (vnThreadsRunning[THREAD_STAKE_MINER] > 0) printf("ThreadStakeMiner still running\n");
    while (vnThreadsRunning[THREAD_MESSAGEHANDLER] > 0 || vnThreadsRunning[THREAD_RPCHANDLER] > 0)
        MilliSleep(20);
    MilliSleep(50);
    DumpAddresses();
    return true;
}

class CNetCleanup
{
public:
    CNetCleanup()
    {
    }
    ~CNetCleanup()
    {
        // Close sockets
        BOOST_FOREACH(CNode* pnode, vNodes)
            if (pnode->hSocket != INVALID_SOCKET)
                closesocket(pnode->hSocket);
        BOOST_FOREACH(SOCKET hListenSocket, vhListenSocket)
            if (hListenSocket != INVALID_SOCKET)
                if (closesocket(hListenSocket) == SOCKET_ERROR)
                    printf("closesocket(hListenSocket) failed with error %d\n", WSAGetLastError());
#ifdef USE_NATIVE_I2P
        if (fNativeI2P) {
                BOOST_FOREACH(SOCKET& hI2PListenSocket, vhI2PListenSocket)
                    if (hI2PListenSocket != INVALID_SOCKET)
                        if (closesocket(hI2PListenSocket) == SOCKET_ERROR)
                            printf("closesocket(hI2PListenSocket) failed with error %d\n", WSAGetLastError());
        }
#endif
#ifdef WIN32
        // Shutdown Windows Sockets
        WSACleanup();
#endif
    }
}
instance_of_cnetcleanup;

void RelayTransaction(const CTransaction& tx, const uint256& hash)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(10000);
    ss << tx;
    RelayTransaction(tx, hash, ss);
}

void RelayTransaction(const CTransaction& tx, const uint256& hash, const CDataStream& ss)
{
    CInv inv(MSG_TX, hash);
    {
        LOCK(cs_mapRelay);
        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime())
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    }

    RelayInventory(inv);
}

void RelayTransactionLockReq(const CTransaction& tx, const uint256& hash, bool relayToAll)
{
    CInv inv(MSG_TXLOCK_REQUEST, tx.GetHash());

    //broadcast the new lock
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if(!relayToAll && !pnode->fRelayTxes)
            continue;

        pnode->PushMessage("txlreq", tx);
    }

}

void RelayForTunaFinalTransaction(const int sessionID, const CTransaction& txNew)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        pnode->PushMessage("dsf", sessionID, txNew);
    }
}

void RelayForTunaIn(const std::vector<CTxIn>& in, const int64_t& nAmount, const CTransaction& txCollateral, const std::vector<CTxOut>& out)
{
    LOCK(cs_vNodes);

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((CNetAddr)forTunaPool.submittedToFortunastake != (CNetAddr)pnode->addr) continue;
        printf("RelayForTunaIn - found master, relaying message - %s \n", pnode->addr.ToString().c_str());
        pnode->PushMessage("dsi", in, nAmount, txCollateral, out);
    }
}

void RelayForTunaStatus(const int sessionID, const int newState, const int newEntriesCount, const int newAccepted, const std::string error)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        pnode->PushMessage("dssu", sessionID, newState, newEntriesCount, newAccepted, error);
    }
}

void RelayForTunaElectionEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey, const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if(!pnode->fRelayTxes) continue;

        pnode->PushMessage("dsee", vin, addr, vchSig, nNow, pubkey, pubkey2, count, current, lastUpdated, protocolVersion);
    }
}

void SendForTunaElectionEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey, const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        pnode->PushMessage("dsee", vin, addr, vchSig, nNow, pubkey, pubkey2, count, current, lastUpdated, protocolVersion);
    }
}

void RelayForTunaElectionEntryPing(const CTxIn vin, const std::vector<unsigned char> vchSig, const int64_t nNow, const bool stop)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if(!pnode->fRelayTxes) continue;

        pnode->PushMessage("dseep", vin, vchSig, nNow, stop);
    }
}

void SendForTunaElectionEntryPing(const CTxIn vin, const std::vector<unsigned char> vchSig, const int64_t nNow, const bool stop)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        pnode->PushMessage("dseep", vin, vchSig, nNow, stop);
    }
}

void RelayForTunaCompletedTransaction(const int sessionID, const bool error, const std::string errorMessage)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        pnode->PushMessage("dsc", sessionID, error, errorMessage);
    }
}
