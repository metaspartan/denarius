# Denarius [D]
Tribus Algo PoW/PoS Hybrid Cryptocurrency

![logo](http://i.imgur.com/gIe5vnw.png)

[![GitHub version](https://img.shields.io/github/release/carsenk/denarius.svg)](https://badge.fury.io/gh/carsenk%2Fdenarius)
[![License: GPL v3](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/carsenk/denarius/blob/master/COPYING)
[![Denarius downloads](https://img.shields.io/github/downloads/carsenk/denarius/total.svg?maxAge=2592000)](https://github.com/carsenk/denarius/releases)
[![Join the chat at https://discord.gg/AcThv2y](https://img.shields.io/badge/Discord-Chat-blue.svg?logo=discord)](https://discord.gg/AcThv2y)

[![HitCount](http://hits.dwyl.io/carsenk/denarius.svg)](http://hits.dwyl.io/carsenk/denarius)
<a href="https://discord.gg/UPpQy3n"><img src="https://discordapp.com/api/guilds/334361453320732673/embed.png" alt="Discord server" /></a>

![GitHub code size in bytes](https://img.shields.io/github/languages/code-size/carsenk/denarius.svg) ![GitHub repo size in bytes](https://img.shields.io/github/repo-size/carsenk/denarius.svg)

[![Snap Status](https://build.snapcraft.io/badge/carsenk/denarius.svg)](https://build.snapcraft.io/user/carsenk/denarius)

[![Build Status](https://travis-ci.org/carsenk/denarius.svg?branch=master)](https://travis-ci.org/carsenk/denarius)

[![Build history](https://buildstats.info/travisci/chart/carsenk/denarius?branch=master)](https://travis-ci.org/carsenk/denarius?branch=master)

Intro
==========================
Denarius is a true optional anonymous, untraceable, and secure hybrid cryptocurrency.

Ticker: D

Denarius [D] is an anonymous, untraceable, energy efficient, Proof-of-Work (New Tribus Algorithm) and Proof-of-Stake cryptocurrency.
10,000,000 D will be created in approx. about 3 years during the PoW phase. Denarius had a 10% Premine of 1,000,000 D for bounties, distribution, and marketing.

Specifications
==========================
* Total number of coins: 10,000,000 D
* Ideal block time: 30 seconds
* Stake interest: 6% annual static inflation
* Confirmations: 10 blocks
* Maturity: 30 blocks (15 minutes)
* Min stake age: 8 hours
* Cost of Hybrid Fortuna Stakes: 5,000 D
* Hybrid Fortuna Stake Reward: 33% of the current block reward
* P2P Port: 33369, Testnet Port: 33368
* RPC Port: 32369, Testnet RPC Port: 32368
* Fortuna Stake Port: 9999, Testnet Port: 19999

Technology
==========================
* Hybrid PoW/PoS Fortuna Stakes
* Stealth addresses
* Ring Signatures (16 Recommended)
* Native Optional Tor Onion Node (-nativetor=1)
* Native Optional I2P Node (-nativei2p=1)
* Encrypted Messaging
* Multi-Signature Addresses & TXs
* Atomic Swaps using UTXOs (BIP65 CLTV)
* BIP39 Support (Coin Type 114)
* Proof of Data (Image/Data Timestamping)
* Fast 30 Second Block Times
* New/First Tribus PoW Algorithm comprising of 3 NIST5 algorithms
* Tribus PoW/PoS Hybrid
* Full decentralization

LINKS
==========================
* Official Website(https://denarius.io/)
* Official Forums(https://denariustalk.org/)
* Denarius Twitter(https://twitter.com/denariuscoin)
* Denarius Discord Chat(https://discord.gg/C64HXbc)

ASCII CAST TUTORIALS
==========================
[![asciicast](https://asciinema.org/a/179356.png)](https://asciinema.org/a/179356)
[![asciicast](https://asciinema.org/a/179362.png)](https://asciinema.org/a/179362)
[![asciicast](https://asciinema.org/a/179355.png)](https://asciinema.org/a/179355)

denariusqtubuntu.sh by Buzzkillb
===========================
Compile the latest Denarius QT (Graphical Wallet) Ubuntu 16.04 or Ubuntu 18.04.1

Credits to Buzzkillb for the creation of this bash script, original repository: https://github.com/buzzkillb/denarius-qt/

Compiles Denarius QT Ubuntu 16.04 or 18.04, Grabs latest chaindata, and populates denarius.conf with addnodes or can update a previous compile to the latest master branch.  
```bash -c "$(wget -O - https://raw.githubusercontent.com/carsenk/denarius/master/denariusqtubuntu.sh)"```  

To turn on nativetor in denarius.conf  
```nativetor=1```  

![Denarius Installer Menu](https://raw.githubusercontent.com/buzzkillb/denarius-qt/master/compile-menu.PNG)  

Development process
===========================

Developers work in their own trees, then submit pull requests when
they think their feature or bug fix is ready.

The patch will be accepted if there is broad consensus that it is a
good thing.  Developers should expect to rework and resubmit patches
if they don't match the project's coding conventions (see coding.txt)
or are controversial.

The master branch is regularly built and tested, but is not guaranteed
to be completely stable. Tags are regularly created to indicate new
stable release versions of Denarius.

Feature branches are created when there are major new features being
worked on by several people.

From time to time a pull request will become outdated. If this occurs, and
the pull is no longer automatically mergeable; a comment on the pull will
be used to issue a warning of closure. The pull will be closed 15 days
after the warning if action is not taken by the author. Pull requests closed
in this manner will have their corresponding issue labeled 'stagnant'.

Issues with no commits will be given a similar warning, and closed after
15 days from their last activity. Issues closed in this manner will be
labeled 'stale'.
