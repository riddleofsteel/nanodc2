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

#if !defined(STRING_TOKENIZER_H)
#define STRING_TOKENIZER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

template<class T>
class StringTokenizer
{
private:
	vector<T> tokens;
public:
	StringTokenizer(const T& aString, typename T::value_type aToken) {
		string::size_type i = 0;
		string::size_type j = 0;
		while( (i=aString.find(aToken, j)) != string::npos ) {
			tokens.push_back(aString.substr(j, i-j));
			j = i + 1;
		}
		if(j < aString.size())
			tokens.push_back(aString.substr(j, aString.size()-j));
	}

	StringTokenizer(const T& aString, typename T::value_type* aToken) {
		string::size_type i = 0;
		string::size_type j = 0;
		size_t l = strlen(aToken);
		while( (i=aString.find(aToken, j)) != string::npos ) {
			tokens.push_back(aString.substr(j, i-j));
			j = i + l;
		}
		if(j < aString.size())
			tokens.push_back(aString.substr(j, aString.size()-j));
	}

	vector<T>& getTokens() { return tokens; }

	~StringTokenizer() { }
};

#endif // !defined(STRING_TOKENIZER_H)
