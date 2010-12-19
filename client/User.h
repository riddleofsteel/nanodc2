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

#if !defined(USER_H)
#define USER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Util.h"
#include "Pointer.h"
#include "CID.h"
#include "FastAlloc.h"
#include "CriticalSection.h"

/** A user connected to one or more hubs. */
class User : public FastAlloc<User>, public PointerBase, public Flags
{
public:
	enum Bits {
		ONLINE_BIT,
		DCPLUSPLUS_BIT,
		PASSIVE_BIT,
		NMDC_BIT,
		BOT_BIT,
		TTH_GET_BIT,
		TLS_BIT,
		OLD_CLIENT_BIT
	};

	/** Each flag is set if it's true in at least one hub */
	enum UserFlags {
		ONLINE = 1<<ONLINE_BIT,
		DCPLUSPLUS = 1<<DCPLUSPLUS_BIT,
		PASSIVE = 1<<PASSIVE_BIT,
		NMDC = 1<<NMDC_BIT,
		BOT = 1<<BOT_BIT,
		TTH_GET = 1<<TTH_GET_BIT,		//< User supports getting files by tth -> don't have path in queue...
		TLS = 1<<TLS_BIT,				//< Client supports TLS
		OLD_CLIENT = 1<<OLD_CLIENT_BIT  //< Can't download - old client
	};

	typedef Pointer<User> Ptr;
	typedef vector<Ptr> List;
	typedef List::iterator Iter;

	struct HashFunction {
		static const size_t bucket_size = 4;
		static const size_t min_buckets = 8;
		size_t operator()(const Ptr& x) const { return ((size_t)(&(*x)))/sizeof(User); }
		bool operator()(const Ptr& a, const Ptr& b) const { return (&(*a)) < (&(*b)); }
	};

	User(const CID& aCID) : cid(aCID) { }

	virtual ~User() throw() { }

	const CID& getCID() const { return cid; }
	operator const CID&() const { return cid; }

	bool isOnline() const { return isSet(ONLINE); }
	bool isNMDC() const { return isSet(NMDC); }

	GETSET(string, firstNick, FirstNick);
private:
	User(const User&);
	User& operator=(const User&);

	CID cid;
};

/** One of possibly many identities of a user, mainly for UI purposes */
class Identity : public Flags {
public:
	enum {
		GOT_INF_BIT,
		NMDC_PASSIVE_BIT
	};
	enum IdentityFlags {
		GOT_INF = 1 << GOT_INF_BIT,
		NMDC_PASSIVE = 1 << NMDC_PASSIVE_BIT
	};

	Identity() : sid(0) { }
	Identity(const User::Ptr& ptr, uint32_t aSID) : user(ptr), sid(aSID) { }
	Identity(const Identity& rhs) : Flags(rhs), user(rhs.user), sid(rhs.sid), info(rhs.info) { }
	Identity& operator=(const Identity& rhs) { Lock l(cs); *static_cast<Flags*>(this) = rhs; user = rhs.user; sid = rhs.sid; info = rhs.info; return *this; }

#define GS(n, x) string get##n() const { return get(x); } void set##n(const string& v) { set(x, v); }
	GS(Nick, "NI")
	GS(Description, "DE")
	GS(Ip, "I4")
	GS(UdpPort, "U4")
	GS(Email, "EM")
	GS(Connection, "CO")

	void setBytesShared(const string& bs) { set("SS", bs); }
	int64_t getBytesShared() const { return Util::toInt64(get("SS")); }

	void setOp(bool op) { set("OP", op ? "1" : Util::emptyString); }
	void setHub(bool hub) { set("HU", hub ? "1" : Util::emptyString); }
	void setBot(bool bot) { set("BO", bot ? "1" : Util::emptyString); }
	void setHidden(bool hidden) { set("HI", hidden ? "1" : Util::emptyString); }
	string getTag() const;
	bool supports(const string& name) const;
	bool isHub() const { return !get("HU").empty(); }
	bool isOp() const { return !get("OP").empty(); }
	bool isRegistered() const { return !get("RG").empty(); }
	bool isHidden() const { return !get("HI").empty(); }
	bool isBot() const { return !get("BO").empty(); }
	bool isAway() const { return !get("AW").empty(); }
	bool isTcpActive() const { return !getIp().empty() || (user->isSet(User::NMDC) && !user->isSet(User::PASSIVE)); }
	bool isUdpActive() const { return !getIp().empty() && !getUdpPort().empty(); }
	string get(const char* name) const;
	void set(const char* name, const string& val);
	string getSIDString() const { return string((const char*)&sid, 4); }

	void getParams(StringMap& map, const string& prefix, bool compatibility) const;
	User::Ptr& getUser() { return user; }
	GETSET(User::Ptr, user, User);
	GETSET(uint32_t, sid, SID);
private:
	typedef map<short, string> InfMap;
	typedef InfMap::iterator InfIter;
	InfMap info;
	/** @todo there are probably more threading issues here ...*/
	mutable CriticalSection cs;
};

class Client;
class NmdcHub;

class OnlineUser : public FastAlloc<OnlineUser> {
public:
	typedef vector<OnlineUser*> List;
	typedef List::iterator Iter;

	OnlineUser(const User::Ptr& ptr, Client& client_, uint32_t sid_);

	operator User::Ptr&() { return getUser(); }
	operator const User::Ptr&() const { return getUser(); }

	User::Ptr& getUser() { return getIdentity().getUser(); }
	const User::Ptr& getUser() const { return getIdentity().getUser(); }
	Identity& getIdentity() { return identity; }
	Client& getClient() { return client; }
	const Client& getClient() const { return client; }

	GETSET(Identity, identity, Identity);
private:
	friend class NmdcHub;

	OnlineUser(const OnlineUser&);
	OnlineUser& operator=(const OnlineUser&);

	Client& client;
};

#endif // !defined(USER_H)
