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

#if !defined(FINISHED_MANAGER_H)
#define FINISHED_MANAGER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "DownloadManager.h"
#include "UploadManager.h"

#include "CriticalSection.h"
#include "Singleton.h"

class FinishedItem
{
public:
	typedef FinishedItem* Ptr;
	typedef vector<Ptr> List;
	typedef List::iterator Iter;

	FinishedItem(string const& aTarget, string const& aUser, string const& aHub,
		int64_t aSize, int64_t aChunkSize, int64_t aMSeconds, uint32_t aTime,
		bool aCrc32 = false) :
		target(aTarget), user(aUser), hub(aHub), size(aSize), chunkSize(aChunkSize),
		milliSeconds(aMSeconds), time(aTime), crc32Checked(aCrc32)
	{
	}

	int64_t getAvgSpeed() { return milliSeconds > 0 ? (chunkSize * ((int64_t)1000) / milliSeconds) : 0; }

	GETSET(string, target, Target);
	GETSET(string, user, User);
	GETSET(string, hub, Hub);
	GETSET(int64_t, size, Size);
	GETSET(int64_t, chunkSize, ChunkSize);
	GETSET(int64_t, milliSeconds, MilliSeconds);
	GETSET(uint32_t, time, Time);
	GETSET(bool, crc32Checked, Crc32Checked)
private:
	friend class FinishedManager;

};

class FinishedManagerListener {
public:
	virtual ~FinishedManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> AddedUl;
	typedef X<1> AddedDl;
	typedef X<2> RemovedUl;
	typedef X<3> RemovedDl;
	typedef X<4> RemovedAllUl;
	typedef X<5> RemovedAllDl;

	virtual void on(AddedDl, FinishedItem*) throw() { }
	virtual void on(RemovedDl, FinishedItem*) throw() { }
	virtual void on(RemovedAllDl) throw() { }
	virtual void on(AddedUl, FinishedItem*) throw() { }
	virtual void on(RemovedUl, FinishedItem*) throw() { }
	virtual void on(RemovedAllUl) throw() { }

};

class FinishedManager : public Singleton<FinishedManager>,
	public Speaker<FinishedManagerListener>, private DownloadManagerListener, private UploadManagerListener
{
public:
	FinishedItem::List& lockList(bool upload = false) { cs.enter(); return upload ? uploads : downloads; }
	void unlockList() { cs.leave(); }

	void remove(FinishedItem *item, bool upload = false);
	void removeAll(bool upload = false);
	FinishedItem* getFinishedItem(string aTarget, bool upload = false);
private:
	friend class Singleton<FinishedManager>;

	FinishedManager() {
		DownloadManager::getInstance()->addListener(this);
		UploadManager::getInstance()->addListener(this);
	}
	virtual ~FinishedManager() throw();

	virtual void on(DownloadManagerListener::Complete, Download* d) throw();
	virtual void on(UploadManagerListener::Complete, Upload*) throw();

	CriticalSection cs;
	FinishedItem::List downloads, uploads;
};

#endif // !defined(FINISHED_MANAGER_H)
