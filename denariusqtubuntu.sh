#!/bin/bash
TEMP=/tmp/answer$$
whiptail --title "Denarius [D]"  --menu  "Ubuntu 16.04/18.04 QT Wallet :" 20 0 0 1 "Compile Denarius QT Ubuntu 16.04" 2 "Update Denarius QT 16.04 to v3.4 latest" 3 "Compile Denarius QT Ubuntu 18.04" 4 "Update Denarius QT 18.04 to v3.4 latest" 2>$TEMP
choice=`cat $TEMP`
case $choice in
1) echo 1 "Compiling Denarius QT Ubuntu 16.04"

echo "Updating linux packages"
sudo apt-get update -y && sudo apt-get upgrade -y

sudo apt-get install -y git unzip build-essential libssl-dev libdb++-dev libboost-all-dev libqrencode-dev libminiupnpc-dev libevent-dev autogen automake  libtool libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools qt5-default libcurl4-openssl-dev

echo "Installing Denarius Wallet"
git clone https://github.com/carsenk/denarius
cd denarius || exit
git checkout master
git pull

#echo "Change line in denarius-qt.pro from stdlib=c99 to stdlib=gnu99"
#sed -i 's/c99/gnu99/' ~/denarius/denarius-qt.pro

qmake "USE_QRCODE=1" "USE_UPNP=1" denarius-qt.pro
make

echo "Populate denarius.conf"
mkdir ~/.denarius
echo -e "nativetor=0\naddnode=denarius.host\naddnode=denarius.win\naddnode=denarius.pro\naddnode=triforce.black" > ~/.denarius/denarius.conf

echo "Get Chaindata"
cd ~/.denarius || exit
rm -rf database txleveldb smsgDB
#wget http://d.hashbag.cc/chaindata.zip
#unzip chaindata.zip
wget https://chaindata.pos.watch/chaindata.zip
unzip chaindata.zip
rm -rf chaindata.zip
Echo "Back to Compiled QT Binary Folder"
cd ~/denarius/src
                ;;
2) echo 2 "Update Denarius QT"
echo "Updating Denarius Wallet"
cd ~/denarius || exit
git checkout master
git pull

#echo "Change line in denarius-qt.pro from stdlib=c99 to stdlib=gnu99"
#sed -i 's/c99/gnu99/' ~/denarius/denarius-qt.pro

qmake "USE_QRCODE=1" "USE_UPNP=1" denarius-qt.pro
make
echo "Back to Compiled QT Binary Folder"
cd ~/denarius
                ;;
3) echo 3 "Compile Denarius QT Ubuntu 18.04"
echo "Updating linux packages"
sudo apt-get update -y && sudo apt-get upgrade -y

sudo apt-get install -y git unzip build-essential libdb++-dev libboost-all-dev libqrencode-dev libminiupnpc-dev libevent-dev autogen automake libtool libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools qt5-default libcurl4-openssl-dev

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

#echo "Change line in denarius-qt.pro from stdlib=c99 to stdlib=gnu99"
#sed -i 's/c99/gnu99/' ~/denarius/denarius-qt.pro

qmake "USE_UPNP=1" "USE_QRCODE=1" OPENSSL_INCLUDE_PATH=/usr/local/ssl/include OPENSSL_LIB_PATH=/usr/local/ssl/lib denarius-qt.pro
make

echo "Populate denarius.conf"
mkdir ~/.denarius
echo -e "nativetor=0\naddnode=denarius.host\naddnode=denarius.win\naddnode=denarius.pro\naddnode=triforce.black" > ~/.denarius/denarius.conf

echo "Get Chaindata"
cd ~/.denarius
rm -rf database txleveldb smsgDB
#wget http://d.hashbag.cc/chaindata.zip
#unzip chaindata.zip
wget https://chaindata.pos.watch/chaindata.zip
unzip chaindata.zip
rm -rf chaindata.zip
Echo "Back to Compiled QT Binary Folder"
cd ~/denarius/src
                ;;
4) echo 4 "Update Denarius QT 18.04"
echo "Updating Denarius Wallet"
cd ~/denarius || exit
git checkout master
git pull

#echo "Change line in denarius-qt.pro from stdlib=c99 to stdlib=gnu99"
#sed -i 's/c99/gnu99/' ~/denarius/denarius-qt.pro

qmake "USE_UPNP=1" "USE_QRCODE=1" OPENSSL_INCLUDE_PATH=/usr/local/ssl/include OPENSSL_LIB_PATH=/usr/local/ssl/lib denarius-qt.pro
make
echo "Back to Compiled QT Binary Folder"
cd ~/denarius
                ;;
esac
echo Selected $choice
