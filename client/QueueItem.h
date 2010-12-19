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

#if !defined(QUEUE_ITEM_H)
#define QUEUE_ITEM_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class QueueManager;
class Download;

#include "User.h"
#include "FastAlloc.h"
#include "MerkleTree.h"

class QueueItem : public Flags, public FastAlloc<QueueItem> {
public:
	typedef QueueItem* Ptr;
	// Strange, the vc7 optimizer won't take a deque here...
	typedef vector<Ptr> List;
	typedef List::iterator Iter;
	typedef map<string*, Ptr, noCaseStringLess> StringMap;
	//	typedef HASH_MAP<string, Ptr, noCaseStringHash, noCaseStringEq> StringMap;
	typedef StringMap::iterator StringIter;
	typedef HASH_MAP_X(User::Ptr, Ptr, User::HashFunction, equal_to<User::Ptr>, less<User::Ptr>) UserMap;
	typedef UserMap::iterator UserIter;
	typedef HASH_MAP_X(User::Ptr, List, User::HashFunction, equal_to<User::Ptr>, less<User::Ptr>) UserListMap;
	typedef UserListMap::iterator UserListIter;

	enum Status {
		/** The queue item is waiting to be downloaded and can be found in userQueue */
		STATUS_WAITING,
		/** This item is being downloaded and can be found in running */
		STATUS_RUNNING
	};

	enum Priority {
		DEFAULT = -1,
		PAUSED = 0,
		LOWEST,
		LOW,
		NORMAL,
		HIGH,
		HIGHEST,
		LAST
	};

	enum FileFlags {
		/** Normal download, no flags set */
		FLAG_NORMAL = 0x00,
		/** This download should be resumed if possible */
		FLAG_RESUME = 0x01,
		/** This is a user file listing download */
		FLAG_USER_LIST = 0x02,
		/** The file list is downloaded to use for directory download (used with USER_LIST) */
		FLAG_DIRECTORY_DOWNLOAD = 0x04,
		/** The file is downloaded to be viewed in the gui */
		FLAG_CLIENT_VIEW = 0x08,
		/** Flag to indicate that file should be viewed as a text file */
		FLAG_TEXT = 0x20,
		/** This file exists on the hard disk and should be prioritised */
		FLAG_EXISTS = 0x40,
		/** Match the queue against this list */
		FLAG_MATCH_QUEUE = 0x80,
		/** The file list downloaded was actually an .xml.bz2 list */
		FLAG_XML_BZLIST = 0x200
	};

	class Source : public Flags {
	public:
		enum {
			FLAG_NONE = 0x00,
			FLAG_FILE_NOT_AVAILABLE = 0x01,
			FLAG_ROLLBACK_INCONSISTENCY = 0x02,
			FLAG_PASSIVE = 0x04,
			FLAG_REMOVED = 0x08,
			FLAG_CRC_FAILED = 0x10,
			FLAG_CRC_WARN = 0x20,
			FLAG_NO_TTHF = 0x40,
			FLAG_BAD_TREE = 0x80,
			FLAG_NO_TREE = 0x100,
			FLAG_SLOW_SOURCE = 0x200,
			FLAG_MASK = FLAG_FILE_NOT_AVAILABLE | FLAG_ROLLBACK_INCONSISTENCY
				| FLAG_PASSIVE | FLAG_REMOVED | FLAG_CRC_FAILED | FLAG_CRC_WARN 
				| FLAG_BAD_TREE | FLAG_NO_TREE | FLAG_SLOW_SOURCE
		};

		Source(const User::Ptr& aUser) : user(aUser) { }
		Source(const Source& aSource) : Flags(aSource), user(aSource.user) { }

		bool operator==(const User::Ptr& aUser) const { return user == aUser; }
		User::Ptr& getUser() { return user; }
		GETSET(User::Ptr, user, User);
	};
	
	typedef vector<Source> SourceList;
	typedef SourceList::iterator SourceIter;
	typedef SourceList::const_iterator SourceConstIter;

	QueueItem(const string& aTarget, int64_t aSize,
		Priority aPriority, int aFlag, int64_t aDownloadedBytes, uint32_t aAdded, const TTHValue& tth) :
	Flags(aFlag), target(aTarget),
		size(aSize), downloadedBytes(aDownloadedBytes), status(STATUS_WAITING),
		priority(aPriority), currentDownload(NULL), added(aAdded),
		tthRoot(tth)
	{ }

	QueueItem(const QueueItem& rhs) :
	Flags(rhs), target(rhs.target), tempTarget(rhs.tempTarget),
		size(rhs.size), downloadedBytes(rhs.downloadedBytes), status(rhs.status), priority(rhs.priority),
		current(rhs.current), currentDownload(rhs.currentDownload), added(rhs.added), tthRoot(rhs.tthRoot),
		sources(rhs.sources), badSources(rhs.badSources)
	{
	}

	virtual ~QueueItem() {
	}

	int countOnlineUsers() const {
		int n = 0;
		SourceConstIter i = sources.begin();
		for(; i != sources.end(); ++i) {
			if(i->getUser()->isOnline())
				n++;
		}
		return n;
	}
	bool hasOnlineUsers() const { return countOnlineUsers() > 0; }

	SourceList& getSources() { return sources; }
	const SourceList& getSources() const { return sources; }
	SourceList& getBadSources() { return badSources; }
	const SourceList& getBadSources() const { return badSources; }

	void getOnlineUsers(User::List& l) const {
		for(SourceConstIter i = sources.begin(); i != sources.end(); ++i)
			if(i->getUser()->isOnline())
				l.push_back(i->getUser());
	}

	string getTargetFileName() const { return Util::getFileName(getTarget()); }

	SourceIter getSource(const User::Ptr& aUser) { return find(sources.begin(), sources.end(), aUser); }
	SourceIter getBadSource(const User::Ptr& aUser) { return find(badSources.begin(), badSources.end(), aUser); }
	SourceConstIter getSource(const User::Ptr& aUser) const { return find(sources.begin(), sources.end(), aUser); }
	SourceConstIter getBadSource(const User::Ptr& aUser) const { return find(badSources.begin(), badSources.end(), aUser); }

	bool isSource(const User::Ptr& aUser) const { return getSource(aUser) != sources.end(); }
	bool isBadSource(const User::Ptr& aUser) const { return getBadSource(aUser) != badSources.end(); }
	bool isBadSourceExcept(const User::Ptr& aUser, Flags::MaskType exceptions) const {
		SourceConstIter i = getBadSource(aUser);
		if(i != badSources.end())
			return i->isAnySet(exceptions^Source::FLAG_MASK);
		return false;
	}

	string getListName() const {
		dcassert(isSet(QueueItem::FLAG_USER_LIST));
		if(isSet(QueueItem::FLAG_XML_BZLIST)) {
			return getTarget() + ".xml.bz2";
		} else {
			return getTarget() + ".xml";
		}
	}

	const string& getTempTarget();
	void setTempTarget(const string& aTempTarget) {
		tempTarget = aTempTarget;
	}
	GETSET(string, target, Target);
	string tempTarget;
	GETSET(int64_t, size, Size);
	GETSET(int64_t, downloadedBytes, DownloadedBytes);
	GETSET(Status, status, Status);
	GETSET(Priority, priority, Priority);
	GETSET(User::Ptr, current, Current);
	GETSET(Download*, currentDownload, CurrentDownload);
	GETSET(uint32_t, added, Added);
	GETSET(TTHValue, tthRoot, TTH);
private:
	QueueItem& operator=(const QueueItem&);

	friend class QueueManager;
	SourceList sources;
	SourceList badSources;

	void addSource(const User::Ptr& aUser);
	void removeSource(const User::Ptr& aUser, int reason);
};

#endif // !defined(QUEUE_ITEM_H)
