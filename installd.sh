#!/bin/bash
TEMP=/tmp/answer$$
whiptail --title "Denarius [D]"  --menu  "Ubuntu 16.04/18.04 Daemon Node :" 20 0 0 1 "Compile denariusd Ubuntu 16.04" 2 "Update denariusd 16.04 to latest" 3 "Compile denariusd Ubuntu 18.04" 4 "Update denariusd 18.04 to latest" 2>$TEMP
choice=`cat $TEMP`
case $choice in
1) echo 1 "Compiling denariusd Ubuntu 16.04"

echo "Updating linux packages"
sudo apt-get update -y && sudo apt-get upgrade -y

sudo apt-get --assume-yes install git unzip build-essential libgmp-dev libsecp256k1-dev libssl-dev libdb++-dev libboost-all-dev libqrencode-dev libminiupnpc-dev libevent-dev obfs4proxy libcurl4-openssl-dev

echo "Installing Denarius Wallet"
git clone https://github.com/carsenk/denarius
cd denarius || exit
git checkout master
git pull

cd src
make -f makefile.unix

sudo yes | cp -rf denariusd /usr/bin/

echo "Copied to /usr/bin for ease of use"

echo "Populate denarius.conf"
mkdir ~/.denarius
echo -e "daemon=1\listen=1\rpcuser=user\rpcpassword=changethispassword\nativetor=0\naddnode=denarius.host\naddnode=denarius.win\naddnode=denarius.pro\naddnode=triforce.black" > ~/.denarius/denarius.conf

echo "Get Chaindata"
cd ~/.denarius || exit
rm -rf database txleveldb smsgDB
wget https://gitlab.com/denarius/chain/raw/master/chaindata2290877.zip
unzip chaindata2290877.zip
rm -rf chaindata2290877.zip
echo "Back to Compiled denariusd Binary Folder"
cd ~/denarius/src
                ;;
2) echo 2 "Update denariusd"
echo "Updating Denarius Wallet"
cd ~/denarius || exit
git checkout master
git pull

cd src
make -f makefile.unix

sudo yes | cp -rf denariusd /usr/bin/

echo "Copied to /usr/bin for ease of use"

echo "Back to Compiled denariusd Binary Folder"
cd ~/denarius/src
                ;;
3) echo 3 "Compile denariusd Ubuntu 18.04"
echo "Updating linux packages"
sudo apt-get update -y && sudo apt-get upgrade -y

sudo apt-get --assume-yes install git unzip build-essential libgmp-dev libsecp256k1-dev libdb++-dev libboost-all-dev libqrencode-dev libminiupnpc-dev libevent-dev obfs4proxy libssl-dev libcurl4-openssl-dev

echo "Downgrade libssl-dev"
sudo apt-get install make
wget https://www.openssl.org/source/openssl-1.0.1j.tar.gz
tar -xzvf openssl-1.0.1j.tar.gz
cd openssl-1.0.1j
./config
make depend
sudo make install
sudo ln -sf /usr/local/ssl/bin/openssl `which openssl`
cd ~
openssl version -v

echo "Installing Denarius Wallet"
git clone https://github.com/carsenk/denarius
cd denarius
git checkout master
git pull

cd src
make OPENSSL_INCLUDE_PATH=/usr/local/ssl/include OPENSSL_LIB_PATH=/usr/local/ssl/lib -f makefile.unix

sudo yes | cp -rf denariusd /usr/bin/

echo "Copied to /usr/bin for ease of use"

echo "Populate denarius.conf"
mkdir ~/.denarius
echo -e "daemon=1\listen=1\rpcuser=user\rpcpassword=changethispassword\nativetor=0\naddnode=denarius.host\naddnode=denarius.win\naddnode=denarius.pro\naddnode=triforce.black" > ~/.denarius/denarius.conf

echo "Get Chaindata"
cd ~/.denarius
rm -rf database txleveldb smsgDB
wget https://gitlab.com/denarius/chain/raw/master/chaindata2290877.zip
unzip chaindata2290877.zip
rm -rf chaindata2290877.zip
echo "Back to Compiled denariusd Binary Folder"
cd ~/denarius/src
                ;;
4) echo 4 "Update denariusd 18.04"
echo "Updating Denarius Wallet"
cd ~/denarius || exit
git checkout master
git pull

cd src
make OPENSSL_INCLUDE_PATH=/usr/local/ssl/include OPENSSL_LIB_PATH=/usr/local/ssl/lib -f makefile.unix

sudo yes | cp -rf denariusd /usr/bin/

echo "Copied to /usr/bin for ease of use"

echo "Back to Compiled denariusd Binary Folder"
cd ~/denarius/src
                ;;
esac
echo Selected $choice
