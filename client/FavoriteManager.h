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

#if !defined(FAVORITE_MANAGER_H)
#define FAVORITE_MANAGER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "SettingsManager.h"

#include "CriticalSection.h"
#include "HttpConnection.h"
#include "User.h"
#include "UserCommand.h"
#include "FavoriteUser.h"
#include "Singleton.h"
#include "ClientManagerListener.h"
#include "ClientManager.h"

class HubEntry {
public:
	typedef vector<HubEntry> List;
	typedef List::iterator Iter;

	HubEntry(const string& aName, const string& aServer, const string& aDescription, const string& aUsers) throw() :
	name(aName), server(aServer), description(aDescription), country(Util::emptyString),
	rating(Util::emptyString), reliability(0.0), shared(0), minShare(0), users(Util::toInt(aUsers)), minSlots(0), maxHubs(0), maxUsers(0) { }

	HubEntry(const string& aName, const string& aServer, const string& aDescription, const string& aUsers, const string& aCountry,
		const string& aShared, const string& aMinShare, const string& aMinSlots, const string& aMaxHubs, const string& aMaxUsers,
		const string& aReliability, const string& aRating) : name(aName), server(aServer), description(aDescription), country(aCountry),
		rating(aRating), reliability((float)(Util::toFloat(aReliability) / 100.0)), shared(Util::toInt64(aShared)), minShare(Util::toInt64(aMinShare)),
		users(Util::toInt(aUsers)), minSlots(Util::toInt(aMinSlots)), maxHubs(Util::toInt(aMaxHubs)), maxUsers(Util::toInt(aMaxUsers))
	{

	}

	HubEntry() throw() { }
	HubEntry(const HubEntry& rhs) throw() : name(rhs.name), server(rhs.server), description(rhs.description), country(rhs.country),
		rating(rhs.rating), reliability(rhs.reliability), shared(rhs.shared), minShare(rhs.minShare), users(rhs.users), minSlots(rhs.minSlots),
		maxHubs(rhs.maxHubs), maxUsers(rhs.maxUsers) { }

	~HubEntry() throw() { }

	GETSET(string, name, Name);
	GETSET(string, server, Server);
	GETSET(string, description, Description);
	GETSET(string, country, Country);
	GETSET(string, rating, Rating);
	GETSET(float, reliability, Reliability);
	GETSET(int64_t, shared, Shared);
	GETSET(int64_t, minShare, MinShare);
	GETSET(int, users, Users);
	GETSET(int, minSlots, MinSlots);
	GETSET(int, maxHubs, MaxHubs)
	GETSET(int, maxUsers, MaxUsers);
};

class FavoriteHubEntry {
public:
	typedef FavoriteHubEntry* Ptr;
	typedef vector<Ptr> List;
	typedef List::iterator Iter;

	FavoriteHubEntry() throw() : connect(false), bottom(0), top(0), left(0), right(0){ }
	FavoriteHubEntry(const HubEntry& rhs) throw() : name(rhs.getName()), server(rhs.getServer()), description(rhs.getDescription()), connect(false), bottom(0), top(0), left(0), right(0){ }
	FavoriteHubEntry(const FavoriteHubEntry& rhs) throw() : userdescription(rhs.userdescription), name(rhs.getName()), server(rhs.getServer()), description(rhs.getDescription()),
		password(rhs.getPassword()), connect(rhs.getConnect()), bottom(rhs.getBottom()), top(rhs.getTop()), left(rhs.getLeft()), right(rhs.getRight()), nick(rhs.nick){ }
	~FavoriteHubEntry() throw() { }

	const string& getNick(bool useDefault = true) const {
		return (!nick.empty() || !useDefault) ? nick : SETTING(NICK);
	}

	void setNick(const string& aNick) { nick = aNick; }

	GETSET(string, userdescription, UserDescription);
	GETSET(string, name, Name);
	GETSET(string, server, Server);
	GETSET(string, description, Description);
	GETSET(string, password, Password);
	GETSET(bool, connect, Connect);

	GETSET(uint16_t, bottom, Bottom);
	GETSET(uint16_t, top, Top);
	GETSET(uint16_t, left, Left);
	GETSET(uint16_t, right, Right);


private:
	string nick;
};

class FavoriteManagerListener {
public:
	virtual ~FavoriteManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> DownloadStarting;
	typedef X<1> DownloadFailed;
	typedef X<2> DownloadFinished;
	typedef X<3> FavoriteAdded;
	typedef X<4> FavoriteRemoved;
	typedef X<5> UserAdded;
	typedef X<6> UserRemoved;
	typedef X<7> StatusChanged;

	virtual void on(DownloadStarting, const string&) throw() { }
	virtual void on(DownloadFailed, const string&) throw() { }
	virtual void on(DownloadFinished, const string&) throw() { }
	virtual void on(FavoriteAdded, const FavoriteHubEntry*) throw() { }
	virtual void on(FavoriteRemoved, const FavoriteHubEntry*) throw() { }
	virtual void on(UserAdded, const FavoriteUser&) throw() { }
	virtual void on(UserRemoved, const FavoriteUser&) throw() { }
	virtual void on(StatusChanged, const User::Ptr&) throw() { }
};

class SimpleXML;

/**
 * Public hub list, favorites (hub&user). Assumed to be called only by UI thread.
 */
class FavoriteManager : public Speaker<FavoriteManagerListener>, private HttpConnectionListener, public Singleton<FavoriteManager>,
	private SettingsManagerListener, private ClientManagerListener
{
public:
// Public Hubs
	enum HubTypes {
		TYPE_NORMAL,
		TYPE_BZIP2
	};
	StringList getHubLists();
	bool setHubList(int /*aHubList*/);
	int getSelectedHubList() { return lastServer; }
	void refresh();
	HubTypes getHubListType() { return listType; }
	HubEntry::List getPublicHubs() {
		Lock l(cs);
		return publicListMatrix[publicListServer];
	}
	bool isDownloading() { return running; }

// Favorite Users
	typedef HASH_MAP_X(CID, FavoriteUser, CID::Hash, equal_to<CID>, less<CID>) FavoriteMap;
	FavoriteMap getFavoriteUsers() { Lock l(cs); return users; }

	void addFavoriteUser(User::Ptr& aUser);
	bool isFavoriteUser(const User::Ptr& aUser) const { Lock l(cs); return users.find(aUser->getCID()) != users.end(); }
	void removeFavoriteUser(User::Ptr& aUser);

	bool hasSlot(const User::Ptr& aUser) const;
	void setUserDescription(const User::Ptr& aUser, const string& description);
	void setAutoGrant(const User::Ptr& aUser, bool grant);
	void userUpdated(const OnlineUser& info);
	time_t getLastSeen(const User::Ptr& aUser) const;
// Favorite Hubs
	FavoriteHubEntry::List& getFavoriteHubs() { return favoriteHubs; }

	void addFavorite(const FavoriteHubEntry& aEntry);
	void removeFavorite(FavoriteHubEntry* entry);
	bool checkFavHubExists(const FavoriteHubEntry& aEntry);
	FavoriteHubEntry* getFavoriteHubEntry(const string& aServer);

// Favorite Directories
	bool addFavoriteDir(const string& aDirectory, const string& aName);
	bool removeFavoriteDir(const string& aName);
	bool renameFavoriteDir(const string& aName, const string& anotherName);
	StringPairList getFavoriteDirs() { return favoriteDirs; }

// User Commands
	UserCommand addUserCommand(int type, int ctx, int flags, const string& name, const string& command, const string& hub);
	bool getUserCommand(int cid, UserCommand& uc);
	int findUserCommand(const string& aName);
	bool moveUserCommand(int cid, int pos);
	void updateUserCommand(const UserCommand& uc);
	void removeUserCommand(int cid);
	void removeUserCommand(const string& srv);
	void removeHubUserCommands(int ctx, const string& hub);

	UserCommand::List getUserCommands() { Lock l(cs); return userCommands; }
	UserCommand::List getUserCommands(int ctx, const StringList& hub);

	void load();
	void save();

private:
	FavoriteHubEntry::List favoriteHubs;
	StringPairList favoriteDirs;
	UserCommand::List userCommands;
	int lastId;

	FavoriteMap users;

	mutable CriticalSection cs;

	// Public Hubs
	typedef map<string, HubEntry::List> PubListMap;
	PubListMap publicListMatrix;
	string publicListServer;
	bool running;
	HttpConnection* c;
	int lastServer;
	HubTypes listType;
	string downloadBuf;

	/** Used during loading to prevent saving. */
	bool dontSave;

	friend class Singleton<FavoriteManager>;

	FavoriteManager() : lastId(0), running(false), c(NULL), lastServer(0), listType(TYPE_NORMAL), dontSave(false) {
		SettingsManager::getInstance()->addListener(this);
		ClientManager::getInstance()->addListener(this);
	}

	virtual ~FavoriteManager() throw() {
		ClientManager::getInstance()->removeListener(this);
		SettingsManager::getInstance()->removeListener(this);
		if(c) {
			c->removeListener(this);
			delete c;
			c = NULL;
		}

		for_each(favoriteHubs.begin(), favoriteHubs.end(), DeleteFunction());
	}

	FavoriteHubEntry::Iter getFavoriteHub(const string& aServer) {
		for(FavoriteHubEntry::Iter i = favoriteHubs.begin(); i != favoriteHubs.end(); ++i) {
			if(Util::stricmp((*i)->getServer(), aServer) == 0) {
				return i;
			}
		}
		return favoriteHubs.end();
	}

	void loadXmlList(const string& xml);

	// ClientManagerListener
	virtual void on(UserUpdated, const OnlineUser& user) throw();
	virtual void on(UserConnected, const User::Ptr& user) throw();
	virtual void on(UserDisconnected, const User::Ptr& user) throw();

	// HttpConnectionListener
	virtual void on(Data, HttpConnection*, const uint8_t*, size_t) throw();
	virtual void on(Failed, HttpConnection*, const string&) throw();
	virtual void on(Complete, HttpConnection*, const string&) throw();
	virtual void on(Redirected, HttpConnection*, const string&) throw();
	virtual void on(TypeNormal, HttpConnection*) throw();
	virtual void on(TypeBZ2, HttpConnection*) throw();

	void onHttpFinished() throw();

	// SettingsManagerListener
	virtual void on(SettingsManagerListener::Load, SimpleXML& xml) throw() {
		load(xml);
	}

	void load(SimpleXML& aXml);

	string getConfigFile() { return Util::getConfigPath() + "Favorites.xml"; }
};

#endif // !defined(FAVORITE_MANAGER_H)
