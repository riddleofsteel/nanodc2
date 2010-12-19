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

#if !defined(TEXT_H)
#define TEXT_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

/**
 * Text handling routines for DC++. DC++ internally uses UTF-8 for
 * (almost) all string:s, hence all foreign text must be converted
 * appropriately...
 * acp - ANSI code page used by the system
 * wide - wide unicode string
 * utf8 - UTF-8 representation of the string
 * t - current GUI text format
 * string - UTF-8 string (most of the time)
 * wstring - Wide string
 * tstring - GUI type string (acp string or wide string depending on build type)
 */
class Text {
public:
	static void initialize();

	static string& acpToUtf8(const string& str, string& tmp) throw() {
		tmp = acpToUtf8(str);
		return tmp;
	}
	static string acpToUtf8(const string& str) throw();

	static wstring& acpToWide(const string& str, wstring& tmp) throw();
	static wstring acpToWide(const string& str) throw() {
		wstring tmp;
		return acpToWide(str, tmp);
	}

	static string& utf8ToAcp(const string& str, string& tmp) throw() {
		tmp = utf8ToAcp(str);
		return tmp;
	}
	static string utf8ToAcp(const string& str) throw();

	static wstring& utf8ToWide(const string& str, wstring& tmp) throw();
	static wstring utf8ToWide(const string& str) throw() {
		wstring tmp;
		return utf8ToWide(str, tmp);
	}

	static string& wideToAcp(const wstring& str, string& tmp) throw();
	static string wideToAcp(const wstring& str) throw() {
		string tmp;
		return wideToAcp(str, tmp);
	}
	static string& wideToUtf8(const wstring& str, string& tmp) throw();
	static string wideToUtf8(const wstring& str) throw() {
		string tmp;
		return wideToUtf8(str, tmp);
	}

	static int utf8ToWc(const char* str, wchar_t& c);
	static void wcToUtf8(wchar_t c, string& str);

#ifdef UNICODE
	static tstring toT(const string& str) throw() { return utf8ToWide(str); }
	static tstring& toT(const string& str, tstring& tmp) throw() { return utf8ToWide(str, tmp); }

	static string fromT(const tstring& str) throw() { return wideToUtf8(str); }
	static string fromT(const tstring& str, string& tmp) throw() { return wideToUtf8(str, tmp); }
#else
	static tstring toT(const string& str) throw() { return utf8ToAcp(str); }
	static tstring& toT(const string& str, tstring& tmp) throw() { return utf8ToAcp(str, tmp); }

	static string fromT(const tstring& str) throw() { return acpToUtf8(str); }
	static string& fromT(const tstring& str, string& tmp) throw() { return acpToUtf8(str, tmp); }
#endif

	static bool isAscii(const string& str) {
		return isAscii(str.c_str());
	}
	static bool isAscii(const char* str) {
		for(const uint8_t* p = (const uint8_t*)str; *p; ++p) {
			if(*p & 0x80)
				return false;
		}
		return true;
	}

	static bool validateUtf8(const string& str) throw();

	static char asciiToLower(char c) { dcassert((((uint8_t)c) & 0x80) == 0); return asciiLower[(uint8_t)c]; }

	static wchar_t toLower(wchar_t c) { return lower[(uint16_t)c]; }
	static wstring toLower(const wstring& str) throw() {
		wstring tmp;
		return toLower(str, tmp);
	}
	static wstring& toLower(const wstring& str, wstring& tmp) throw();
	static string toLower(const string& str) throw();
	static string& toLower(const string& str, string& tmp) throw() {
		tmp = toLower(str);
		return tmp;
	}
private:
	static char asciiLower[128];
	static wchar_t lower[65536];

};

#endif // !defined(TEXT_H)
