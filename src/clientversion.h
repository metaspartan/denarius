#ifndef CLIENTVERSION_H
#define CLIENTVERSION_H

//
// client versioning
// Codename C A R S E N  W A S  H E R E
//

// These need to be macros, as version.cpp's and bitcoin-qt.rc's voodoo requires it
#define CLIENT_VERSION_MAJOR       3
#define CLIENT_VERSION_MINOR       3
#define CLIENT_VERSION_REVISION    9
#define CLIENT_VERSION_BUILD       3

#ifdef USE_NATIVE_I2P
#define I2P_NATIVE_VERSION_MAJOR       0
#define I2P_NATIVE_VERSION_MINOR       2
#endif

// Converts the parameter X to a string after macro replacement on X has been performed.
// Don't merge these into one macro!
#define STRINGIZE(X) DO_STRINGIZE(X)
#define DO_STRINGIZE(X) #X

#endif // CLIENTVERSION_H
