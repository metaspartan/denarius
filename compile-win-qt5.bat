set PATH=%PATH%;C:\Qt\5.3.2\bin
qmake "USE_QRCODE=1" "USE_UPNP=1" denarius-qt.pro
mingw32-make -f Makefile.Release
pause