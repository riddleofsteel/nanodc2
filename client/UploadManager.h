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

#if !defined(UPLOAD_MANAGER_H)
#define UPLOAD_MANAGER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "UserConnection.h"
#include "Singleton.h"

#include "ClientManagerListener.h"
#include "MerkleTree.h"

class InputStream;

class Upload : public Transfer, public Flags {
public:
	enum Flags {
		FLAG_USER_LIST = 0x01,
		FLAG_TTH_LEAVES = 0x02,
		FLAG_ZUPLOAD = 0x04,
		FLAG_PARTIAL_LIST = 0x08,
		FLAG_PENDING_KICK = 0x10
	};

	typedef Upload* Ptr;
	typedef vector<Ptr> List;
	typedef List::iterator Iter;

	Upload(UserConnection& conn);
	virtual ~Upload();

	virtual void getParams(const UserConnection& aSource, StringMap& params);

	GETSET(string, sourceFile, SourceFile);
	GETSET(InputStream*, stream, Stream);
};

class UploadManagerListener {
public:
	virtual ~UploadManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> Complete;
	typedef X<1> Failed;
	typedef X<2> Starting;
	typedef X<3> Tick;
	typedef X<4> WaitingAddFile;
	typedef X<5> WaitingRemoveUser;

	virtual void on(Starting, Upload*) throw() { }
	virtual void on(Tick, const Upload::List&) throw() { }
	virtual void on(Complete, Upload*) throw() { }
	virtual void on(Failed, Upload*, const string&) throw() { }
	virtual void on(WaitingAddFile, const User::Ptr, const string&) throw() { }
	virtual void on(WaitingRemoveUser, const User::Ptr) throw() { }

};

class UploadManager : private ClientManagerListener, private UserConnectionListener, public Speaker<UploadManagerListener>, private TimerManagerListener, public Singleton<UploadManager>
{
public:

	/** @return Number of uploads. */
	size_t getUploadCount() { Lock l(cs); return uploads.size(); }

	/**
	 * @remarks This is only used in the tray icons. Could be used in
	 * MainFrame too.
	 *
	 * @return Running average download speed in Bytes/s
	 */
	int64_t getRunningAverage();

	/** @return Number of free slots. */
	int getFreeSlots() { return max((SETTING(SLOTS) - running), 0); }

	/** @internal */
	int getFreeExtraSlots() { return max(3 - getExtra(), 0); }

	/** @param aUser Reserve an upload slot for this user and connect. */
	void reserveSlot(const User::Ptr& aUser);

	typedef set<string> FileSet;
	typedef HASH_MAP_X(User::Ptr, FileSet, User::HashFunction, equal_to<User::Ptr>, less<User::Ptr>) FilesMap;
	void clearUserFiles(const User::Ptr&);
	User::List getWaitingUsers();
	const FileSet& getWaitingUserFiles(const User::Ptr &);

	/** @internal */
	void addConnection(UserConnection::Ptr conn) {
		conn->addListener(this);
		conn->setState(UserConnection::STATE_GET);
	}

	GETSET(int, running, Running);
	GETSET(int, extra, Extra);
	GETSET(uint32_t, lastGrant, LastGrant);
private:
	Upload::List uploads;
	CriticalSection cs;

	typedef HASH_SET<User::Ptr, User::HashFunction> SlotSet;
	typedef SlotSet::iterator SlotIter;
	SlotSet reservedSlots;

	typedef pair<User::Ptr, uint32_t> WaitingUser;
	typedef list<WaitingUser> UserList;

	struct WaitingUserFresh {
		bool operator()(const WaitingUser& wu) { return wu.second > GET_TICK() - 5*60*1000; }
	};

	//functions for manipulating waitingFiles and waitingUsers
	UserList waitingUsers;		//this one merely lists the users waiting for slots
	FilesMap waitingFiles;		//set of files which this user has asked for
	void addFailedUpload(const UserConnection& source, string filename);

	friend class Singleton<UploadManager>;
	UploadManager() throw();
	virtual ~UploadManager() throw();

	bool getAutoSlot();
	void removeConnection(UserConnection* aConn);
	void removeUpload(Upload* aUpload);

	// ClientManagerListener
	virtual void on(ClientManagerListener::UserDisconnected, const User::Ptr& aUser) throw();

	// TimerManagerListener
	virtual void on(Second, uint32_t aTick) throw();
	virtual void on(Minute, uint32_t aTick) throw();

	// UserConnectionListener
	virtual void on(BytesSent, UserConnection*, size_t, size_t) throw();
	virtual void on(Failed, UserConnection*, const string&) throw();
	virtual void on(Get, UserConnection*, const string&, int64_t) throw();
	virtual void on(Send, UserConnection*) throw();
	virtual void on(GetListLength, UserConnection* conn) throw();
	virtual void on(TransmitDone, UserConnection*) throw();

	virtual void on(AdcCommand::GET, UserConnection*, const AdcCommand&) throw();
	virtual void on(AdcCommand::GFI, UserConnection*, const AdcCommand&) throw();

	bool prepareFile(UserConnection& aSource, const string& aType, const string& aFile, int64_t aResume, int64_t aBytes, bool listRecursive = false);
};

#endif // !defined(UPLOAD_MANAGER_H)
