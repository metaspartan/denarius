#ifndef GENERIC_ORCONFIG_H_
#define GENERIC_ORCONFIG_H_

#if _WIN32 || _WIN64
#if _WIN64
#include "orconfig_win64.h"
#else
#include "orconfig_win32.h"
#endif
#elif defined(__darwin__) || defined(__APPLE__)
#include "orconfig_apple.h"
#else
#include "orconfig_linux.h"
#endif

#endif  // GENERIC_ORCONFIG_H_
