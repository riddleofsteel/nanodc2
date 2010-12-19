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

#if !defined(NMDC_HUB_H)
#define NMDC_HUB_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "TimerManager.h"
#include "SettingsManager.h"

#include "User.h"
#include "CriticalSection.h"
#include "Text.h"
#include "Client.h"

class ClientManager;

class NmdcHub : public Client, private Flags
{
public:
	using Client::send;

	virtual void connect();
	virtual void connect(const OnlineUser& aUser);

	virtual void hubMessage(const string& aMessage);
	virtual void privateMessage(const OnlineUser& aUser, const string& aMessage);
	virtual void sendUserCmd(const string& aUserCmd) throw() { send(toAcp(aUserCmd)); }
	virtual void search(int aSizeType, int64_t aSize, int aFileType, const string& aString, const string& aToken);
	virtual void password(const string& aPass) { send("$MyPass " + toAcp(aPass) + "|"); }
	virtual void info(bool force) { myInfo(force); }

	virtual size_t getUserCount() const { Lock l(cs); return users.size(); }
	virtual int64_t getAvailable() const;

	virtual string escape(string const& str) const { return validateMessage(str, false); }
	static string unescape(const string& str) { return validateMessage(str, true); }

	virtual void send(const AdcCommand&) { dcassert(0); }

	static string validateMessage(string tmp, bool reverse);

	GETSET(int, supportFlags, SupportFlags);
private:
	friend class ClientManager;
	enum SupportFlags {
		SUPPORTS_USERCOMMAND = 0x01,
		SUPPORTS_NOGETINFO = 0x02,
		SUPPORTS_USERIP2 = 0x04
	};

	enum States {
		STATE_CONNECT,
		STATE_LOCK,
		STATE_HELLO,
		STATE_CONNECTED
	} state;

	mutable CriticalSection cs;

	typedef HASH_MAP_X(string, OnlineUser*, noCaseStringHash, noCaseStringEq, noCaseStringLess) NickMap;
	typedef NickMap::iterator NickIter;

	NickMap users;

	uint32_t lastUpdate;
	string lastMyInfoA, lastMyInfoB;

	typedef list<pair<string, uint32_t> > FloodMap;
	typedef FloodMap::iterator FloodIter;
	FloodMap seekers;
	FloodMap flooders;

	NmdcHub(const string& aHubURL);
	virtual ~NmdcHub() throw();

	// Dummy
	NmdcHub(const NmdcHub&);
	NmdcHub& operator=(const NmdcHub&);

	void clearUsers();
	void onLine(const string& aLine) throw();

	OnlineUser& getUser(const string& aNick);
	OnlineUser* findUser(const string& aNick);
	void putUser(const string& aNick);

	string fromAcp(const string& str) const { return Text::acpToUtf8(str); }
	string toAcp(const string& str) const { return Text::utf8ToAcp(str); }

	void validateNick(const string& aNick) { send("$ValidateNick " + toAcp(aNick) + "|"); }
	void key(const string& aKey) { send("$Key " + aKey + "|"); }
	void version() { send("$Version 1,0091|"); }
	void getNickList() { send("$GetNickList|"); }
	void connectToMe(const OnlineUser& aUser);
	void revConnectToMe(const OnlineUser& aUser);
	void myInfo(bool alwaysSend);
	void supports(const StringList& feat);
	void clearFlooders(uint32_t tick);

	void updateFromTag(Identity& id, const string& tag);

	virtual string checkNick(const string& aNick);

	// TimerManagerListener
	virtual void on(Second, uint32_t aTick) throw();

	virtual void on(Line, const string& l) throw() { onLine(l); }
	virtual void on(Failed, const string&) throw();

};

#endif // !defined(NMDC_HUB_H)
