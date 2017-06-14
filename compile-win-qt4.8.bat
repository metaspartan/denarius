set PATH=%PATH%;C:\Qt\4.8.6\bin
qmake "USE_QRCODE=1" "USE_UPNP=-" denarius-qt.pro
mingw32-make -f Makefile.Release
pause