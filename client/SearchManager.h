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

#if !defined(SEARCH_MANAGER_H)
#define SEARCH_MANAGER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "SettingsManager.h"

#include "Socket.h"
#include "User.h"
#include "Thread.h"
#include "Client.h"
#include "Singleton.h"
#include "FastAlloc.h"
#include "MerkleTree.h"

#include "SearchManagerListener.h"
#include "TimerManager.h"
#include "AdcCommand.h"

class SearchManager;
class SocketException;

class SearchResult : public FastAlloc<SearchResult> {
public:

	enum Types {
		TYPE_FILE,
		TYPE_DIRECTORY
	};

	typedef SearchResult* Ptr;
	typedef vector<Ptr> List;
	typedef List::iterator Iter;

	SearchResult(Types aType, int64_t aSize, const string& name, const TTHValue& aTTH);

	SearchResult(const User::Ptr& aUser, Types aType, int aSlots, int aFreeSlots,
		int64_t aSize, const string& aFile, const string& aHubName,
		const string& aHubURL, const string& ip, TTHValue aTTH, const string& aToken) :
	file(aFile), hubName(aHubName), hubURL(aHubURL), user(aUser),
		size(aSize), type(aType), slots(aSlots), freeSlots(aFreeSlots), IP(ip),
		tth(aTTH), token(aToken), ref(1) { }

	string getFileName() const;
	string toSR(const Client& client) const;
	AdcCommand toRES(char type) const;

	User::Ptr& getUser() { return user; }
	string getSlotString() const { return Util::toString(getFreeSlots()) + '/' + Util::toString(getSlots()); }

	const string& getFile() const { return file; }
	const string& getHubURL() const { return hubURL; }
	const string& getHubName() const { return hubName; }
	int64_t getSize() const { return size; }
	Types getType() const { return type; }
	int getSlots() const { return slots; }
	int getFreeSlots() const { return freeSlots; }
	TTHValue getTTH() const { return tth; }
	const string& getIP() const { return IP; }
	const string& getToken() const { return token; }

	void incRef() { Thread::safeInc(ref); }
	void decRef() {
		if(Thread::safeDec(ref) == 0)
			delete this;
	}

private:
	friend class SearchManager;

	SearchResult();
	~SearchResult() { }

	SearchResult(const SearchResult& rhs);

	string file;
	string hubName;
	string hubURL;
	User::Ptr user;
	int64_t size;
	Types type;
	int slots;
	int freeSlots;
	string IP;
	TTHValue tth;
	string token;

	volatile long ref;
};

class SearchManager : public Speaker<SearchManagerListener>, public Singleton<SearchManager>, public Thread
{
public:
	enum SizeModes {
		SIZE_DONTCARE = 0x00,
		SIZE_ATLEAST = 0x01,
		SIZE_ATMOST = 0x02
	};

	enum TypeModes {
		TYPE_ANY = 0,
		TYPE_AUDIO,
		TYPE_COMPRESSED,
		TYPE_DOCUMENT,
		TYPE_EXECUTABLE,
		TYPE_PICTURE,
		TYPE_VIDEO,
		TYPE_DIRECTORY,
		TYPE_TTH
	};

	void search(const string& aName, int64_t aSize, TypeModes aTypeMode, SizeModes aSizeMode, const string& aToken);
	void search(const string& aName, const string& aSize, TypeModes aTypeMode, SizeModes aSizeMode, const string& aToken) {
		search(aName, Util::toInt64(aSize), aTypeMode, aSizeMode, aToken);
	}

	void search(StringList& who, const string& aName, int64_t aSize, TypeModes aTypeMode, SizeModes aSizeMode, const string& aToken);
	void search(StringList& who, const string& aName, const string& aSize, TypeModes aTypeMode, SizeModes aSizeMode, const string& aToken) {
		search(who, aName, Util::toInt64(aSize), aTypeMode, aSizeMode, aToken);
	}
	static string clean(const string& aSearchString);

	void respond(const AdcCommand& cmd, const CID& cid);

	uint16_t getPort()
	{
		return port;
	}

	void listen() throw(SocketException);
	void disconnect() throw();
	void onSearchResult(const string& aLine) {
		onData((const uint8_t*)aLine.data(), aLine.length(), Util::emptyString);
	}

	void onRES(const AdcCommand& cmd, const User::Ptr& from, const string& removeIp = Util::emptyString);

	int32_t timeToSearch() {
		return (int32_t)(((((int64_t)lastSearch) + 5000) - GET_TICK() ) / 1000);
	}

	bool okToSearch() {
		return timeToSearch() <= 0;
	}

private:

	Socket* socket;
	uint16_t port;
	bool stop;
	uint32_t lastSearch;
	friend class Singleton<SearchManager>;

	SearchManager() : socket(NULL), port(0), stop(false), lastSearch(0) { }

	virtual int run();

	virtual ~SearchManager() throw() {
		if(socket) {
			stop = true;
			socket->disconnect();
#ifdef _WIN32
			join();
#endif
			delete socket;
		}
	}

	void onData(const uint8_t* buf, size_t aLen, const string& address);
};

#endif // !defined(SEARCH_MANAGER_H)
