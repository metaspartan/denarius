// Copyright (c) 2019 The Denarius developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "state.h"

int nNodeMode = NT_FULL;
int nNodeState = NS_STARTUP;

int nMaxThinPeers = 8;
int nBloomFilterElements = 1536;
//int nMinStakeInterval = 60;         // in seconds, min time between successful stakes
int nThinStakeDelay = 48;           // in seconds
int nThinIndexWindow = 4096;        // no. of block headers to keep in memory
int nLastTryThinStake = 0;

uint32_t nMaxThinStakeCandidates = 8;

// -- services provided by local node, initialise to all on
uint64_t nLocalServices     = 0 | NODE_NETWORK | THIN_SUPPORT | THIN_STAKE | THIN_STEALTH | SMSG_RELAY;
uint32_t nLocalRequirements = 0 | NODE_NETWORK;

bool fThinFullIndex = false; // when in thin mode don't keep all headers in memory