/*
 * Copyright (C) 2001-2006 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#if !defined(STDINC_H)
#define STDINC_H

#include "config.h"

#ifdef _WIN32

#define STRICT
#define WIN32_LEAN_AND_MEAN
#define _WTL_NO_CSTRING
#define _ATL_NO_OPENGL
#define _ATL_NO_MSIMG
#define _ATL_NO_COM
#define _ATL_NO_HOSTING
#define _ATL_NO_OLD_NAMES

#if _MSC_VER == 1400
//disable the deperecated warnings for the crt functions.
#define _CRT_SECURE_NO_DEPRECATE 1
#define _ATL_SECURE_NO_DEPRECATE 1
#define _CRT_NON_CONFORMING_SWPRINTFS 1
#endif

#include <Winsock2.h>

#include <windows.h>
#include <crtdbg.h>
#include <tchar.h>

#else
#include <unistd.h>
#include <stdint.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <memory.h>
#include <sys/types.h>
#include <time.h>
#include <locale.h>

#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <deque>
#include <list>
#include <utility>
#include <functional>

// Use maps if hash_maps aren't available
#ifdef HAVE_HASH
# ifdef _STLPORT_VERSION
#  define HASH_SET_X(key, hfunc, eq, order) hash_set<key, hfunc, eq >
#  define HASH_MAP_X(key, type, hfunc, eq, order) hash_map<key, type, hfunc, eq >
// STLPort 5.0.2 hash_multimap buggy
#  define HASH_MULTIMAP_X(key, type, hfunc, eq, order) multimap<key, type, order > 
# elif defined(__GLIBCPP__) || defined(__GLIBCXX__)  // Using GNU C++ library?
#  define HASH_SET_X(key, hfunc, eq, order) hash_set<key, hfunc, eq >
#  define HASH_MAP_X(key, type, hfunc, eq, order) hash_map<key, type, hfunc, eq >
#  define HASH_MULTIMAP_X(key, type, hfunc, eq, order) hash_multimap<key, type, hfunc, eq >
# elif defined(_MSC_VER)  // Assume the msvc 7.x stl
#  define HASH_SET_X(key, hfunc, eq, order) hash_set<key, hfunc >
#  define HASH_MAP_X(key, type, hfunc, eq, order) hash_map<key, type, hfunc >
#  define HASH_MULTIMAP_X(key, type, hfunc, eq, order) hash_multimap<key, type, hfunc >
# else
#  error Unknown STL, hashes need to be configured
# endif

# define HASH_SET hash_set
# define HASH_MAP hash_map
# define HASH_MULTIMAP multimap

#else // HAVE_HASH

# define HASH_SET_X(key, hfunc, eq, order)
# define HASH_SET set
# define HASH_MAP map
# define HASH_MAP_X(key, type, hfunc, eq, order) map<key, type, order >
# define HASH_MULTIMAP multimap
# define HASH_MULTIMAP_X(key, type, hfunc, eq, order) multimap<key, type, order >

#endif // HAVE_HASH

#ifdef _STLPORT_VERSION
using namespace std;
#include <hash_map>
#include <hash_set>

#elif defined(__GLIBCPP__) || defined(__GLIBCXX__)  // Using GNU C++ library?
#include <ext/hash_map>
#include <ext/hash_set>
#include <ext/functional>
using namespace std;
using namespace __gnu_cxx;

// GNU C++ library doesn't have hash(std::string) or hash(long long int)
namespace __gnu_cxx {
	template<> struct hash<std::string> {
		size_t operator()(const std::string& x) const
			{ return hash<const char*>()(x.c_str()); }
	};
	template<> struct hash<long long int> {
		size_t operator()(long long int x) const { return x; }
	};
}
#else // __GLIBCPP__

#include <hash_map>
#include <hash_set>

using namespace std;
using namespace stdext;

#endif // __GLIBCPP__

#endif // !defined(STDINC_H)
