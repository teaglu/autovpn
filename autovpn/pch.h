/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#pragma once


#include "targetver.h"

#include <afx.h>
#include <stdio.h>
#include <tchar.h>
#include <assert.h>


#define _WINSOCKAPI_
#include <windows.h>
#include <winsock.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>
#include <wlanapi.h>
#include <winhttp.h>

#include <Pdh.h>

#include <string>
#include <sstream>
#include <list>
#include <deque>
#include <mutex>
#include <thread>
#include <memory>
#include <set>

#include <comdef.h>
#include <Wbemidl.h>
#include <atlstr.h>
#include <atlconv.h>

using namespace std;

// Removed because winlocation is not available in W10
// #include <locationapi.h>


