#pragma once

// clang-format off
#pragma warning(disable : 26426) // Warning C26426 Global initializer calls a non-constexpr (i.22)
#pragma warning(disable : 26446) // Warning C26446 Prefer to use gsl::at() instead of unchecked subscript operator (bounds.4).
#pragma warning(disable : 26481) // Warning C26481 Don't use pointer arithmetic. Use span instead (bounds.1).
#pragma warning(disable : 26485) // Warning C26485 Expression '': No array to pointer decay (bounds.3).
#pragma warning(disable : 26487) // Warning C26487 Don't return a pointer '' that may be invalid (lifetime.4).
// clang-format on

#define VC_EXTRALEAN // Exclude rarely-used stuff from Windows headers

#define _WINSOCKAPI_ // stops windows.h including winsock.h
#include <Windows.h>
#include <tchar.h>

// Common CRT headers
#include <list>
#include <algorithm>
#include <locale>
#include <sstream>
#include <iterator>
#include <functional>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cassert>
#include <stack>
#include <deque>

// All the MAPI headers
#include <MAPIX.h>
#include <MAPIUtil.h>
#include <MAPIForm.h>
#include <MAPIWz.h>
#include <MAPIHook.h>
#include <MSPST.h>

#include <EdkMdb.h>
#include <ExchForm.h>
#include <EMSABTAG.H>
#include <IMessage.h>
#include <EdkGuid.h>
#include <TNEF.h>
#include <MAPIAux.h>

#include <core/res/Resource.h> // main symbols
#include <UnitTest/res/resource.h> // unittest symbols