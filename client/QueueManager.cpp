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

#include "stdinc.h"
#include "DCPlusPlus.h"

#include "QueueManager.h"

#include "ConnectionManager.h"
#include "SearchManager.h"
#include "ClientManager.h"
#include "DownloadManager.h"
#include "ShareManager.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "version.h"

#include "UserConnection.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "DirectoryListing.h"

#include <limits>

#ifdef ff
#undef ff
#endif

#ifndef _WIN32
#include <sys/types.h>
#include <dirent.h>
#include <fnmatch.h>
#endif

namespace {
	const string TEMP_EXTENSION = ".dctmp";

	string getTempName(const string& aFileName, const TTHValue& aRoot) {
		string tmp(aFileName);
		tmp += "." + aRoot.toBase32();
		tmp += TEMP_EXTENSION;
		return tmp;
	}
}

const string& QueueItem::getTempTarget() {
	if(!isSet(QueueItem::FLAG_USER_LIST) && tempTarget.empty()) {
		if(!SETTING(TEMP_DOWNLOAD_DIRECTORY).empty() && (File::getSize(getTarget()) == -1)) {
#ifdef _WIN32
			::StringMap sm;
			if(target.length() >= 3 && target[1] == ':' && target[2] == '\\')
				sm["targetdrive"] = target.substr(0, 3);
			else
				sm["targetdrive"] = Util::getConfigPath().substr(0, 3);
			setTempTarget(Util::formatParams(SETTING(TEMP_DOWNLOAD_DIRECTORY), sm, false) + getTempName(getTargetFileName(), getTTH()));
#else //_WIN32
			setTempTarget(SETTING(TEMP_DOWNLOAD_DIRECTORY) + getTempName(getTargetFileName(), getTTH()));
#endif //_WIN32
		}
	}
	return tempTarget;
}

QueueItem* QueueManager::FileQueue::add(const string& aTarget, int64_t aSize,
						  int aFlags, QueueItem::Priority p, const string& aTempTarget,
						  int64_t aDownloadedBytes, uint32_t aAdded, const TTHValue& root) throw(QueueException, FileException)
{
	if(p == QueueItem::DEFAULT) {
		p = QueueItem::NORMAL;
		if(aSize <= SETTING(PRIO_HIGHEST_SIZE)*1024) {
			p = QueueItem::HIGHEST;
		} else if(aSize <= SETTING(PRIO_HIGH_SIZE)*1024) {
			p = QueueItem::HIGH;
		} else if(aSize <= SETTING(PRIO_NORMAL_SIZE)*1024) {
			p = QueueItem::NORMAL;
		} else if(aSize <= SETTING(PRIO_LOW_SIZE)*1024) {
			p = QueueItem::LOW;
		} else if(SETTING(PRIO_LOWEST)) {
			p = QueueItem::LOWEST;
		}
	}

	QueueItem* qi = new QueueItem(aTarget, aSize, p, aFlags, aDownloadedBytes, aAdded, root);

	if(!qi->isSet(QueueItem::FLAG_USER_LIST)) {
		if(!aTempTarget.empty()) {
			qi->setTempTarget(aTempTarget);
		}
	} else {
		qi->setPriority(QueueItem::HIGHEST);
	}

	if((qi->getDownloadedBytes() > 0))
		qi->setFlag(QueueItem::FLAG_EXISTS);

	dcassert(find(aTarget) == NULL);
	add(qi);
	return qi;
}

void QueueManager::FileQueue::add(QueueItem* qi) {
	if(lastInsert == queue.end())
		lastInsert = queue.insert(make_pair(const_cast<string*>(&qi->getTarget()), qi)).first;
	else
		lastInsert = queue.insert(lastInsert, make_pair(const_cast<string*>(&qi->getTarget()), qi));
}

QueueItem* QueueManager::FileQueue::find(const string& target) {
	QueueItem::StringIter i = queue.find(const_cast<string*>(&target));
	return (i == queue.end()) ? NULL : i->second;
}

void QueueManager::FileQueue::find(QueueItem::List& sl, int64_t aSize, const string& suffix) {
	for(QueueItem::StringIter i = queue.begin(); i != queue.end(); ++i) {
		if(i->second->getSize() == aSize) {
			const string& t = i->second->getTarget();
			if(suffix.empty() || (suffix.length() < t.length() &&
				Util::stricmp(suffix.c_str(), t.c_str() + (t.length() - suffix.length())) == 0) )
				sl.push_back(i->second);
		}
	}
}

void QueueManager::FileQueue::find(QueueItem::List& ql, const TTHValue& tth) {
	for(QueueItem::StringIter i = queue.begin(); i != queue.end(); ++i) {
		QueueItem* qi = i->second;
		if(qi->getTTH() == tth) {
			ql.push_back(qi);
		}
	}
}

static QueueItem* findCandidate(QueueItem::StringIter start, QueueItem::StringIter end, StringList& recent) {
	QueueItem* cand = NULL;
	for(QueueItem::StringIter i = start; i != end; ++i) {
		QueueItem* q = i->second;

		// We prefer to search for things that are not running...
		if((cand != NULL) && (q->getStatus() == QueueItem::STATUS_RUNNING))
			continue;
		// No user lists
		if(q->isSet(QueueItem::FLAG_USER_LIST))
			continue;
		// No paused downloads
		if(q->getPriority() == QueueItem::PAUSED)
			continue;
		// No files that already have more than AUTO_SEARCH_LIMIT online sources
		if(q->countOnlineUsers() >= SETTING(AUTO_SEARCH_LIMIT))
			continue;
		// Did we search for it recently?
		if(find(recent.begin(), recent.end(), q->getTarget()) != recent.end())
			continue;

		cand = q;

		if(cand->getStatus() != QueueItem::STATUS_RUNNING)
			break;
	}
	return cand;
}

QueueItem* QueueManager::FileQueue::findAutoSearch(StringList& recent) {
	// We pick a start position at random, hoping that we will find something to search for...
	QueueItem::StringMap::size_type start = (QueueItem::StringMap::size_type)Util::rand((uint32_t)queue.size());

	QueueItem::StringIter i = queue.begin();
	advance(i, start);

	QueueItem* cand = findCandidate(i, queue.end(), recent);
	if(cand == NULL) {
		cand = findCandidate(queue.begin(), i, recent);
	} else if(cand->getStatus() == QueueItem::STATUS_RUNNING) {
		QueueItem* cand2 = findCandidate(queue.begin(), i, recent);
		if(cand2 != NULL && (cand2->getStatus() != QueueItem::STATUS_RUNNING)) {
			cand = cand2;
		}
	}
	return cand;
}

void QueueManager::FileQueue::move(QueueItem* qi, const string& aTarget) {
	if(lastInsert != queue.end() && Util::stricmp(*lastInsert->first, qi->getTarget()) == 0)
		lastInsert = queue.end();
	queue.erase(const_cast<string*>(&qi->getTarget()));
	qi->setTarget(aTarget);
	add(qi);
}

void QueueManager::UserQueue::add(QueueItem* qi) {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		add(qi, i->getUser());
	}
}

void QueueManager::UserQueue::add(QueueItem* qi, const User::Ptr& aUser) {
	if(qi->getStatus() == QueueItem::STATUS_RUNNING) {
		return;
	}

	QueueItem::List& l = userQueue[qi->getPriority()][aUser];
	if(qi->isSet(QueueItem::FLAG_EXISTS)) {
		l.insert(l.begin(), qi);
	} else {
		l.push_back(qi);
	}
}

QueueItem* QueueManager::UserQueue::getNext(const User::Ptr& aUser, QueueItem::Priority minPrio) {
	int p = QueueItem::LAST - 1;

	do {
		QueueItem::UserListIter i = userQueue[p].find(aUser);
		if(i != userQueue[p].end()) {
			dcassert(!i->second.empty());
			return i->second.front();
		}
		p--;
	} while(p >= minPrio);

	return NULL;
}

void QueueManager::UserQueue::setRunning(QueueItem* qi, const User::Ptr& aUser) {
	dcassert(qi->getCurrentDownload() == 0);
	dcassert(qi->getStatus() == QueueItem::STATUS_WAITING);

	// Remove the download from the userQueue...
	remove(qi);

	// Set the flag to running...
	qi->setStatus(QueueItem::STATUS_RUNNING);
	qi->setCurrent(aUser);

	// Move the download to the running list...
	dcassert(running.find(aUser) == running.end());
	running[aUser] = qi;
}

void QueueManager::UserQueue::setWaiting(QueueItem* qi) {
	dcassert(qi->getStatus() == QueueItem::STATUS_RUNNING);
	dcassert(qi->getCurrentDownload() != 0);

	dcassert(running.find(qi->getCurrent()) != running.end());
	// Remove the download from running
	running.erase(qi->getCurrent());

	// Set flag to waiting
	qi->setStatus(QueueItem::STATUS_WAITING);
	qi->setCurrent(User::Ptr());
	qi->setCurrentDownload(0);

	// Add to the userQueue
	add(qi);
}

QueueItem* QueueManager::UserQueue::getRunning(const User::Ptr& aUser) {
	QueueItem::UserIter i = running.find(aUser);
	return (i == running.end()) ? 0 : i->second;
}

void QueueManager::UserQueue::remove(QueueItem* qi) {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		remove(qi, i->getUser());
	}
}

void QueueManager::UserQueue::remove(QueueItem* qi, const User::Ptr& aUser) {
	if(qi->getStatus() == QueueItem::STATUS_RUNNING) {
		if(qi->getCurrent() == aUser) {
			// Remove from running...
			dcassert(running.find(aUser) != running.end());
			running.erase(aUser);
		}
	} else {
		dcassert(qi->isSource(aUser));
		QueueItem::UserListMap& ulm = userQueue[qi->getPriority()];
		QueueItem::UserListIter j = ulm.find(aUser);
		dcassert(j != ulm.end());
		QueueItem::List& l = j->second;
		dcassert(find(l.begin(), l.end(), qi) != l.end());
		l.erase(std::remove(l.begin(), l.end(), qi), l.end());

		if(l.empty()) {
			ulm.erase(j);
		}
	}
}

QueueManager::QueueManager() : lastSave(0), queueFile(Util::getConfigPath() + "Queue.xml"), dirty(true), nextSearch(0) {
	TimerManager::getInstance()->addListener(this);
	SearchManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);

	File::ensureDirectory(Util::getListPath());
}

QueueManager::~QueueManager() throw() {
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);

	saveQueue();

	if(!BOOLSETTING(KEEP_LISTS)) {
		string path = Util::getListPath();

#ifdef _WIN32
		WIN32_FIND_DATA data;
		HANDLE hFind;

		hFind = FindFirstFile(Text::toT(path + "\\*.xml.bz2").c_str(), &data);
		if(hFind != INVALID_HANDLE_VALUE) {
			do {
				File::deleteFile(path + Text::fromT(data.cFileName));
			} while(FindNextFile(hFind, &data));

			FindClose(hFind);
		}

		hFind = FindFirstFile(Text::toT(path + "\\*.DcLst").c_str(), &data);
		if(hFind != INVALID_HANDLE_VALUE) {
			do {
				File::deleteFile(path + Text::fromT(data.cFileName));
			} while(FindNextFile(hFind, &data));

			FindClose(hFind);
		}

#else
		DIR* dir = opendir(path.c_str());
		if (dir) {
			while (struct dirent* ent = readdir(dir)) {
				if (fnmatch("*.xml.bz2", ent->d_name, 0) == 0 ||
					fnmatch("*.DcLst", ent->d_name, 0) == 0) {
					File::deleteFile(path + ent->d_name);
				}
			}
			closedir(dir);
		}
#endif
	}
}

bool QueueManager::getTTH(const string& name, TTHValue& tth) throw() {
	Lock l(cs);
	QueueItem* qi = fileQueue.find(name);
	if(qi) {
		tth = qi->getTTH();
		return true;
	}
	return false;
}

void QueueManager::on(TimerManagerListener::Minute, uint32_t aTick) throw() {
	string fn;
	string searchString;
	bool online = false;

	{
		Lock l(cs);
		QueueItem::UserMap& um = userQueue.getRunning();

		for(QueueItem::UserIter j = um.begin(); j != um.end(); ++j) {
			QueueItem* q = j->second;
			dcassert(q->getCurrentDownload() != NULL);
			q->setDownloadedBytes(q->getCurrentDownload()->getPos());
		}
		if(!um.empty())
			setDirty();

		if(BOOLSETTING(AUTO_SEARCH) && (aTick >= nextSearch) && (fileQueue.getSize() > 0)) {
			// We keep 30 recent searches to avoid duplicate searches
			while((recent.size() >= fileQueue.getSize()) || (recent.size() > 30)) {
				recent.erase(recent.begin());
			}

			QueueItem* qi = fileQueue.findAutoSearch(recent);
			if(qi != NULL) {
				searchString = qi->getTTH().toBase32();
				online = qi->hasOnlineUsers();
				recent.push_back(qi->getTarget());
				nextSearch = aTick + (online ? 120000 : 300000);
			}
		}
	}

	if(!searchString.empty()) {
		SearchManager::getInstance()->search(searchString, 0, SearchManager::TYPE_TTH, SearchManager::SIZE_DONTCARE, "auto");
	}
}

void QueueManager::addList(const User::Ptr& aUser, int aFlags) throw(QueueException, FileException) {
	string target = Util::getListPath() + Util::validateFileName(aUser->getFirstNick()) + "." + aUser->getCID().toBase32();

	add(target, -1, TTHValue(), aUser, QueueItem::FLAG_USER_LIST | aFlags);
}

void QueueManager::addPfs(const User::Ptr& aUser, const string& aDir) throw(QueueException) {
	if(aUser == ClientManager::getInstance()->getMe()) {
		throw QueueException(STRING(NO_DOWNLOADS_FROM_SELF));
	}

	if(!aUser->isOnline())
		return;

	{
		Lock l(cs);
		pair<PfsIter, PfsIter> range = pfsQueue.equal_range(aUser->getCID());
		if(find_if(range.first, range.second, CompareSecond<CID, string>(aDir)) == range.second) {
			pfsQueue.insert(make_pair(aUser->getCID(), aDir));
		}
	}

	ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

void QueueManager::add(const string& aTarget, int64_t aSize, const TTHValue& root, User::Ptr aUser, 
					   int aFlags /* = QueueItem::FLAG_RESUME */, bool addBad /* = true */) throw(QueueException, FileException)
{
	bool wantConnection = true;

	// Check that we're not downloading from ourselves...
	if(aUser == ClientManager::getInstance()->getMe()) {
		throw QueueException(STRING(NO_DOWNLOADS_FROM_SELF));
	}

	// Check if we're not downloading something already in our share
	if(BOOLSETTING(DONT_DL_ALREADY_SHARED)){
		if (ShareManager::getInstance()->isTTHShared(root)){
			throw QueueException(STRING(TTH_ALREADY_SHARED));
		}
	}

	string target = checkTarget(aTarget, aSize, aFlags);

	// Check if it's a zero-byte file, if so, create and return...
	if(aSize == 0) {
		if(!BOOLSETTING(SKIP_ZERO_BYTE)) {
			File::ensureDirectory(target);
			File f(target, File::WRITE, File::CREATE);
		}
		return;
	}

	if(aUser->isSet(User::PASSIVE) && !ClientManager::getInstance()->isActive()) {
		throw QueueException(STRING(NO_DOWNLOADS_FROM_PASSIVE));
	}

	{
		Lock l(cs);

		QueueItem* q = fileQueue.find(target);
		if(q == NULL) {
			q = fileQueue.add(target, aSize, aFlags, QueueItem::DEFAULT, Util::emptyString, 0, GET_TIME(), root);
			fire(QueueManagerListener::Added(), q);
		} else {
			if(q->getSize() != aSize) {
				throw QueueException(STRING(FILE_WITH_DIFFERENT_SIZE));
			}
			if(!(root == q->getTTH())) {
				throw QueueException(STRING(FILE_WITH_DIFFERENT_TTH));
			} else if(BOOLSETTING(DONT_DL_ALREADY_QUEUED)) {
				throw QueueException(STRING(FILE_IS_ALREADY_QUEUED));
			}
			q->setFlag(aFlags);

			// We don't add any more sources to user list downloads...
			if(q->isSet(QueueItem::FLAG_USER_LIST))
				return;
		}

		wantConnection = addSource(q, aUser, addBad ? QueueItem::Source::FLAG_MASK : 0);
	}

	if(wantConnection && aUser->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

void QueueManager::readd(const string& target, User::Ptr& aUser) throw(QueueException) {
	bool wantConnection = false;
	{
		Lock l(cs);
		QueueItem* q = fileQueue.find(target);
		if(q != NULL && q->isBadSource(aUser)) {
			wantConnection = addSource(q, aUser, QueueItem::Source::FLAG_MASK);
		}
	}
	if(wantConnection && aUser->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

string QueueManager::checkTarget(const string& aTarget, int64_t aSize, int& flags) throw(QueueException, FileException) {
#ifdef _WIN32
	if(aTarget.length() > MAX_PATH) {
		throw QueueException(STRING(TARGET_FILENAME_TOO_LONG));
	}
	// Check that target starts with a drive or is an UNC path
	if( (aTarget[1] != ':' || aTarget[2] != '\\') &&
		(aTarget[0] != '\\' && aTarget[1] != '\\') ) {
		throw QueueException(STRING(INVALID_TARGET_FILE));
	}
#else
	if(aTarget.length() > PATH_MAX) {
		throw QueueException(STRING(TARGET_FILENAME_TOO_LONG));
	}
	// Check that target contains at least one directory...we don't want headless files...
	if(aTarget[0] != '/') {
		throw QueueException(STRING(INVALID_TARGET_FILE));
	}
#endif

	string target = Util::validateFileName(aTarget);

	// Check that the file doesn't already exist...
	int64_t sz = File::getSize(target);
	if( (aSize != -1) && (aSize <= sz) ) {
		throw FileException(STRING(LARGER_TARGET_FILE_EXISTS));
	}
	if(sz > 0)
		flags |= QueueItem::FLAG_EXISTS;

	return target;
}

/** Add a source to an existing queue item */
bool QueueManager::addSource(QueueItem* qi, User::Ptr aUser, Flags::MaskType addBad) throw(QueueException, FileException) {
	bool wantConnection = (qi->getPriority() != QueueItem::PAUSED) && (qi->getStatus() != QueueItem::STATUS_RUNNING);

	if(qi->isSource(aUser)) {
		throw QueueException(STRING(DUPLICATE_SOURCE));
	}

	if(qi->isBadSourceExcept(aUser, addBad)) {
		throw QueueException(STRING(DUPLICATE_SOURCE));
	}

	qi->addSource(aUser);

	if(aUser->isSet(User::PASSIVE) && !ClientManager::getInstance()->isActive() ) {
		qi->removeSource(aUser, QueueItem::Source::FLAG_PASSIVE);
		wantConnection = false;
	} else {
		userQueue.add(qi, aUser);
	}

	fire(QueueManagerListener::SourcesUpdated(), qi);
	setDirty();

	return wantConnection;
}

void QueueManager::addDirectory(const string& aDir, const User::Ptr& aUser, const string& aTarget, QueueItem::Priority p /* = QueueItem::DEFAULT */) throw() {
	bool needList;
	{
		Lock l(cs);

		DirectoryItem::DirectoryPair dp = directories.equal_range(aUser);

		for(DirectoryItem::DirectoryIter i = dp.first; i != dp.second; ++i) {
			if(Util::stricmp(aTarget.c_str(), i->second->getName().c_str()) == 0)
				return;
		}

		// Unique directory, fine...
		directories.insert(make_pair(aUser, new DirectoryItem(aUser, aDir, aTarget, p)));
		needList = (dp.first == dp.second);
		setDirty();
	}

	if(needList) {
		try {
			addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD);
		} catch(const Exception&) {
			// Ignore, we don't really care...
		}
	}
}

QueueItem::Priority QueueManager::hasDownload(const User::Ptr& aUser) throw() {
	Lock l(cs);
	if(pfsQueue.find(aUser->getCID()) != pfsQueue.end()) {
		return QueueItem::HIGHEST;
	}
	QueueItem* qi = userQueue.getNext(aUser, QueueItem::LOWEST);
	if(!qi) {
		return QueueItem::PAUSED;
	}
	return qi->getPriority();
}
namespace {
typedef HASH_MAP_X(TTHValue, const DirectoryListing::File*, TTHValue::Hash, equal_to<TTHValue>, less<TTHValue>) TTHMap;

// *** WARNING ***
// Lock(cs) makes sure that there's only one thread accessing this
static TTHMap tthMap;

void buildMap(const DirectoryListing::Directory* dir) throw() {
	for(DirectoryListing::Directory::List::const_iterator j = dir->directories.begin(); j != dir->directories.end(); ++j) {
		if(!(*j)->getAdls())
			buildMap(*j);
	}

	for(DirectoryListing::File::List::const_iterator i = dir->files.begin(); i != dir->files.end(); ++i) {
		const DirectoryListing::File* df = *i;
		tthMap.insert(make_pair(df->getTTH(), df));
	}
}
}
int QueueManager::matchListing(const DirectoryListing& dl) throw() {
	int matches = 0;
	{
		Lock l(cs);
		tthMap.clear();
		buildMap(dl.getRoot());

		for(QueueItem::StringMap::const_iterator i = fileQueue.getQueue().begin(); i != fileQueue.getQueue().end(); ++i) {
			QueueItem* qi = i->second;
			if(qi->isSet(QueueItem::FLAG_USER_LIST))
				continue;
			TTHMap::iterator j = tthMap.find(qi->getTTH());
			if(j != tthMap.end() && i->second->getSize() == qi->getSize()) {
				try {
					addSource(qi, dl.getUser(), QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
				} catch(...) {
					// Ignore...
				}
				matches++;
			}
		}
	}
	if(matches > 0)
		ConnectionManager::getInstance()->getDownloadConnection(dl.getUser());
	return matches;
}

void QueueManager::move(const string& aSource, const string& aTarget) throw() {
	string target = Util::validateFileName(aTarget);
	if(aSource == target)
		return;

	bool delSource = false;

	Lock l(cs);
	QueueItem* qs = fileQueue.find(aSource);
	if(qs != NULL) {
		// Don't move running downloads
		if(qs->getStatus() == QueueItem::STATUS_RUNNING) {
			return;
		}
		// Don't move file lists
		if(qs->isSet(QueueItem::FLAG_USER_LIST))
			return;

		// Let's see if the target exists...then things get complicated...
		QueueItem* qt = fileQueue.find(target);
		if(qt == NULL || Util::stricmp(aSource, target) == 0) {
			// Good, update the target and move in the queue...
			fileQueue.move(qs, target);
			fire(QueueManagerListener::Moved(), qs, aSource);
			setDirty();
		} else {
			// Don't move to target of different size
			if(qs->getSize() != qt->getSize())
				return;

			try {
				for(QueueItem::SourceConstIter i = qs->getSources().begin(); i != qs->getSources().end(); ++i) {
					addSource(qt, i->getUser(), QueueItem::Source::FLAG_MASK);
				}
			} catch(const Exception&) {
			}
			delSource = true;
		}
	}

	if(delSource) {
		remove(aSource);
	}
}

void QueueManager::getTargets(const TTHValue& tth, StringList& sl) {
	Lock l(cs);
	QueueItem::List ql;
	fileQueue.find(ql, tth);
	for(QueueItem::Iter i = ql.begin(); i != ql.end(); ++i) {
		sl.push_back((*i)->getTarget());
	}
}

Download* QueueManager::getDownload(UserConnection& aSource, bool supportsTrees) throw() {
	Lock l(cs);

	User::Ptr& aUser = aSource.getUser();
	// First check PFS's...
	PfsIter pi = pfsQueue.find(aUser->getCID());
	if(pi != pfsQueue.end()) {
		Download* d = new Download(aSource);
		d->setFlag(Download::FLAG_PARTIAL_LIST);
		d->setSource(pi->second);
		return d;
	}

	QueueItem* q = userQueue.getNext(aUser);

	if(!q)
		return 0;

	userQueue.setRunning(q, aUser);

	Download* d = new Download(aSource, *q);

	q->setCurrentDownload(d);

	if(d->getSize() != -1) {
		if(HashManager::getInstance()->getTree(d->getTTH(), d->getTigerTree())) {
			d->setTreeValid(true);
		} else if(supportsTrees && !q->getCurrent()->isSet(QueueItem::Source::FLAG_NO_TREE) && d->getSize() > HashManager::MIN_BLOCK_SIZE) {
			// Get the tree unless the file is small (for small files, we'd probably only get the root anyway)
			d->setFlag(Download::FLAG_TREE_DOWNLOAD);
			d->getTigerTree().setFileSize(d->getSize());
			d->setPos(0);
			d->setSize(-1);
			d->unsetFlag(Download::FLAG_RESUME);
		} else {
			// Use the root as tree to get some sort of validation at least...
			d->getTigerTree() = TigerTree(d->getSize(), d->getSize(), d->getTTH());
			d->setTreeValid(true);
		}
	}

	if(!d->isSet(Download::FLAG_TREE_DOWNLOAD) && BOOLSETTING(ANTI_FRAG) ) {
		d->setStartPos(q->getDownloadedBytes());
	}

	fire(QueueManagerListener::StatusUpdated(), q);
	return d;
}


void QueueManager::putDownload(Download* aDownload, bool finished) throw() {
	User::List getConn;
 	string fname;
	User::Ptr up;
	int flag = 0;

	{
		Lock l(cs);

		if(aDownload->isSet(Download::FLAG_PARTIAL_LIST)) {
			pair<PfsIter, PfsIter> range = pfsQueue.equal_range(aDownload->getUser()->getCID());
			PfsIter i = find_if(range.first, range.second, CompareSecond<CID, string>(aDownload->getSource()));
			if(i != range.second) {
				pfsQueue.erase(i);
				fire(QueueManagerListener::PartialList(), aDownload->getUser(), aDownload->getPFS());
			}
		} else {
			QueueItem* q = fileQueue.find(aDownload->getTarget());

			if(q) {
				if(aDownload->isSet(Download::FLAG_USER_LIST)) {
					if(aDownload->getSource() == Transfer::USER_LIST_NAME_BZ) {
						q->setFlag(QueueItem::FLAG_XML_BZLIST);
					} else {
						q->unsetFlag(QueueItem::FLAG_XML_BZLIST);
					}
				}

				if(finished) {
					if(aDownload->isSet(Download::FLAG_TREE_DOWNLOAD)) {
						// Got a full tree, now add it to the HashManager
						dcassert(aDownload->getTreeValid());
						HashManager::getInstance()->addTree(aDownload->getTigerTree());

						if(q->getStatus() == QueueItem::STATUS_RUNNING) {
							userQueue.setWaiting(q);
							fire(QueueManagerListener::StatusUpdated(), q);
						}
					} else {
						// Now, let's see if this was a directory download filelist...
						if( (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) && directories.find(q->getCurrent()) != directories.end()) ||
							(q->isSet(QueueItem::FLAG_MATCH_QUEUE)) )
						{
							fname = q->getListName();
							up = q->getCurrent();
							flag = (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) ? QueueItem::FLAG_DIRECTORY_DOWNLOAD : 0)
								| (q->isSet(QueueItem::FLAG_MATCH_QUEUE) ? QueueItem::FLAG_MATCH_QUEUE : 0);
						}

						fire(QueueManagerListener::Finished(), q, aDownload->getAverageSpeed());
						fire(QueueManagerListener::Removed(), q);

						userQueue.remove(q);
						fileQueue.remove(q);
						setDirty();
					}
				} else {
					if(!aDownload->isSet(Download::FLAG_TREE_DOWNLOAD)) {
						q->setDownloadedBytes(aDownload->getPos());

						if(q->getDownloadedBytes() > 0) {
							q->setFlag(QueueItem::FLAG_EXISTS);
						} else {
							q->setTempTarget(Util::emptyString);
						}
						if(q->isSet(QueueItem::FLAG_USER_LIST)) {
							// Blah...no use keeping an unfinished file list...
							File::deleteFile(q->getListName());
						}
					}

					if(q->getPriority() != QueueItem::PAUSED) {
						q->getOnlineUsers(getConn);
					}

					// This might have been set to wait elsewhere already...
					if(q->getStatus() == QueueItem::STATUS_RUNNING) {
						userQueue.setWaiting(q);
						fire(QueueManagerListener::StatusUpdated(), q);
					}
				}
			} else if(!aDownload->isSet(Download::FLAG_TREE_DOWNLOAD)) {
				if(!aDownload->getTempTarget().empty() && (aDownload->isSet(Download::FLAG_USER_LIST) || aDownload->getTempTarget() != aDownload->getTarget())) {
					File::deleteFile(aDownload->getTempTarget() + Download::ANTI_FRAG_EXT);
					File::deleteFile(aDownload->getTempTarget());
				}
			}
		}
		delete aDownload;
	}

	for(User::Iter i = getConn.begin(); i != getConn.end(); ++i) {
		ConnectionManager::getInstance()->getDownloadConnection(*i);
	}

	if(!fname.empty()) {
		processList(fname, up, flag);
	}
}

void QueueManager::processList(const string& name, User::Ptr& user, int flags) {
	DirectoryListing dirList(user);
	try {
		dirList.loadFile(name);
	} catch(const Exception&) {
		LogManager::getInstance()->message(STRING(UNABLE_TO_OPEN_FILELIST) + name);
		return;
	}

	if(flags & QueueItem::FLAG_DIRECTORY_DOWNLOAD) {
		DirectoryItem::List dl;
		{
			Lock l(cs);
			DirectoryItem::DirectoryPair dp = directories.equal_range(user);
			for(DirectoryItem::DirectoryIter i = dp.first; i != dp.second; ++i) {
				dl.push_back(i->second);
			}
			directories.erase(user);
		}

		for(DirectoryItem::Iter i = dl.begin(); i != dl.end(); ++i) {
			DirectoryItem* di = *i;
			dirList.download(di->getName(), di->getTarget(), false);
			delete di;
		}
	}
	if(flags & QueueItem::FLAG_MATCH_QUEUE) {
		const size_t BUF_SIZE = STRING(MATCHED_FILES).size() + 16;
		AutoArray<char> tmp(BUF_SIZE);
		snprintf(tmp, BUF_SIZE, CSTRING(MATCHED_FILES), matchListing(dirList));
		LogManager::getInstance()->message(Util::toString(ClientManager::getInstance()->getNicks(user->getCID())) + ": " + string(tmp));
	}
}

void QueueManager::remove(const string& aTarget) throw() {
	User::Ptr x;

	{
		Lock l(cs);

		QueueItem* q = fileQueue.find(aTarget);
		if(!q)
			return;

		if(q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD)) {
			dcassert(q->getSources().size() == 1);
			DirectoryItem::DirectoryPair dp = directories.equal_range(q->getSources()[0].getUser());
			for(DirectoryItem::DirectoryIter i = dp.first; i != dp.second; ++i) {
				delete i->second;
			}
			directories.erase(q->getSources()[0].getUser());
		}

		if(q->getStatus() == QueueItem::STATUS_RUNNING) {
			x = q->getCurrent();
		} else if(!q->getTempTarget().empty() && q->getTempTarget() != q->getTarget()) {
			File::deleteFile(q->getTempTarget() + Download::ANTI_FRAG_EXT);
			File::deleteFile(q->getTempTarget());
		}

		fire(QueueManagerListener::Removed(), q);

		userQueue.remove(q);
		fileQueue.remove(q);

		setDirty();
	}

	if(x) {
		ConnectionManager::getInstance()->disconnect(x, true);
	}
}

void QueueManager::removeSource(const string& aTarget, User::Ptr& aUser, int reason, bool removeConn /* = true */) throw() {
	bool isRunning = false;
	bool removeCompletely = false;
	{
		Lock l(cs);
		QueueItem* q = fileQueue.find(aTarget);
		if(!q)
			return;

		if(!q->isSource(aUser))
			return;

		if(q->isSet(QueueItem::FLAG_USER_LIST)) {
			removeCompletely = true;
			goto endCheck;
		}

		if(reason == QueueItem::Source::FLAG_NO_TREE) {
			q->getSource(aUser)->setFlag(reason);
			return;
		}

		if(reason == QueueItem::Source::FLAG_CRC_WARN) {
			// Already flagged?
			QueueItem::SourceIter s = q->getSource(aUser);
			if(s->isSet(QueueItem::Source::FLAG_CRC_WARN)) {
				reason = QueueItem::Source::FLAG_CRC_FAILED;
			} else {
				s->setFlag(reason);
				return;
			}
		}

		if((q->getStatus() == QueueItem::STATUS_RUNNING) && q->getCurrent() == aUser) {
			isRunning = true;
			userQueue.setWaiting(q);
			fire(QueueManagerListener::StatusUpdated(), q);
		}

		userQueue.remove(q, aUser);
		q->removeSource(aUser, reason);

		fire(QueueManagerListener::SourcesUpdated(), q);
		setDirty();
	}
endCheck:
	if(isRunning && removeConn) {
		ConnectionManager::getInstance()->disconnect(aUser, true);
	}
	if(removeCompletely) {
		remove(aTarget);
	}
}

void QueueManager::removeSource(User::Ptr& aUser, int reason) throw() {
	bool isRunning = false;
	string removeRunning;
	{
		Lock l(cs);
		QueueItem* qi = NULL;
		while( (qi = userQueue.getNext(aUser, QueueItem::PAUSED)) != NULL) {
			if(qi->isSet(QueueItem::FLAG_USER_LIST)) {
				remove(qi->getTarget());
			} else {
				userQueue.remove(qi, aUser);
				qi->removeSource(aUser, reason);
				fire(QueueManagerListener::SourcesUpdated(), qi);
				setDirty();
			}
		}

		qi = userQueue.getRunning(aUser);
		if(qi) {
			if(qi->isSet(QueueItem::FLAG_USER_LIST)) {
				removeRunning = qi->getTarget();
			} else {
				userQueue.setWaiting(qi);
				userQueue.remove(qi, aUser);
				isRunning = true;
				qi->removeSource(aUser, reason);
				fire(QueueManagerListener::StatusUpdated(), qi);
				fire(QueueManagerListener::SourcesUpdated(), qi);
				setDirty();
			}
		}
	}

	if(isRunning) {
		ConnectionManager::getInstance()->disconnect(aUser, true);
	}
	if(!removeRunning.empty()) {
		remove(removeRunning);
	}
}

void QueueManager::setPriority(const string& aTarget, QueueItem::Priority p) throw() {
	User::List ul;

	{
		Lock l(cs);

		QueueItem* q = fileQueue.find(aTarget);
		if( (q != NULL) && (q->getPriority() != p) ) {
			if( q->getStatus() == QueueItem::STATUS_WAITING ) {
				if(q->getPriority() == QueueItem::PAUSED || p == QueueItem::HIGHEST) {
					// Problem, we have to request connections to all these users...
					q->getOnlineUsers(ul);
				}

				userQueue.remove(q);
				q->setPriority(p);
				userQueue.add(q);
			} else {
				q->setPriority(p);
			}
			setDirty();
			fire(QueueManagerListener::StatusUpdated(), q);
		}
	}

	for(User::Iter i = ul.begin(); i != ul.end(); ++i) {
		ConnectionManager::getInstance()->getDownloadConnection(*i);
	}
}

void QueueManager::saveQueue() throw() {
	if(!dirty)
		return;

	Lock l(cs);

	try {

		File ff(getQueueFile() + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE);
		BufferedOutputStream<false> f(&ff);

		f.write(SimpleXML::utf8Header);
		f.write(LIT("<Downloads Version=\"" VERSIONSTRING "\">\r\n"));
		string tmp;
		string b32tmp;
		for(QueueItem::StringIter i = fileQueue.getQueue().begin(); i != fileQueue.getQueue().end(); ++i) {
			QueueItem* qi = i->second;
			if(!qi->isSet(QueueItem::FLAG_USER_LIST)) {
				f.write(LIT("\t<Download Target=\""));
				f.write(SimpleXML::escape(qi->getTarget(), tmp, true));
				f.write(LIT("\" Size=\""));
				f.write(Util::toString(qi->getSize()));
				f.write(LIT("\" Priority=\""));
				f.write(Util::toString((int)qi->getPriority()));
				f.write(LIT("\" Added=\""));
				f.write(Util::toString(qi->getAdded()));
				b32tmp.clear();
				f.write(LIT("\" TTH=\""));
				f.write(qi->getTTH().toBase32(b32tmp));
				if(qi->getDownloadedBytes() > 0) {
					f.write(LIT("\" TempTarget=\""));
					f.write(SimpleXML::escape(qi->getTempTarget(), tmp, true));
					f.write(LIT("\" Downloaded=\""));
					f.write(Util::toString(qi->getDownloadedBytes()));
				}
				f.write(LIT("\">\r\n"));

				for(QueueItem::SourceConstIter j = qi->sources.begin(); j != qi->sources.end(); ++j) {
					f.write(LIT("\t\t<Source CID=\""));
					f.write(j->getUser()->getCID().toBase32());
					f.write(LIT("\"/>\r\n"));
				}

				f.write(LIT("\t</Download>\r\n"));
			}
		}

		f.write("</Downloads>\r\n");
		f.flush();
		ff.close();
		File::deleteFile(getQueueFile());
		File::renameFile(getQueueFile() + ".tmp", getQueueFile());

		dirty = false;
	} catch(const FileException&) {
		// ...
	}
	// Put this here to avoid very many saves tries when disk is full...
	lastSave = GET_TICK();
}

class QueueLoader : public SimpleXMLReader::CallBack {
public:
	QueueLoader() : cur(NULL), inDownloads(false) { }
	virtual ~QueueLoader() { }
	virtual void startTag(const string& name, StringPairList& attribs, bool simple);
	virtual void endTag(const string& name, const string& data);
private:
	string target;

	QueueItem* cur;
	bool inDownloads;
};

void QueueManager::loadQueue() throw() {
	try {
		QueueLoader l;
		SimpleXMLReader(&l).fromXML(File(getQueueFile(), File::READ, File::OPEN).read());
		dirty = false;
	} catch(const Exception&) {
		// ...
	}
}

int QueueManager::countOnlineSources(const string& aTarget) {
	Lock l(cs);

	QueueItem* qi = fileQueue.find(aTarget);
	if(!qi)
		return 0;
	int onlineSources = 0;
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		if(i->getUser()->isOnline())
			onlineSources++;
	}
	return onlineSources;
}

static const string sDownload = "Download";
static const string sTempTarget = "TempTarget";
static const string sTarget = "Target";
static const string sSize = "Size";
static const string sDownloaded = "Downloaded";
static const string sPriority = "Priority";
static const string sSource = "Source";
static const string sNick = "Nick";
static const string sDirectory = "Directory";
static const string sAdded = "Added";
static const string sTTH = "TTH";
static const string sCID = "CID";

void QueueLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	QueueManager* qm = QueueManager::getInstance();
	if(!inDownloads && name == "Downloads") {
		inDownloads = true;
	} else if(inDownloads) {
		if(cur == NULL && name == sDownload) {
			int flags = QueueItem::FLAG_RESUME;
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			if(size == 0)
				return;
			try {
				const string& tgt = getAttrib(attribs, sTarget, 0);
				target = QueueManager::checkTarget(tgt, size, flags);
				if(target.empty())
					return;
			} catch(const Exception&) {
				return;
			}
			QueueItem::Priority p = (QueueItem::Priority)Util::toInt(getAttrib(attribs, sPriority, 3));
			uint32_t added = (uint32_t)Util::toInt(getAttrib(attribs, sAdded, 4));
			const string& tthRoot = getAttrib(attribs, sTTH, 5);
			if(tthRoot.empty())
				return;

			string tempTarget = getAttrib(attribs, sTempTarget, 5);
			int64_t downloaded = Util::toInt64(getAttrib(attribs, sDownloaded, 5));
			if (downloaded > size || downloaded < 0)
				downloaded = 0;

			if(added == 0)
				added = GET_TIME();

			QueueItem* qi = qm->fileQueue.find(target);

			if(qi == NULL) {
				qi = qm->fileQueue.add(target, size, flags, p, tempTarget, downloaded, added, TTHValue(tthRoot));
				qm->fire(QueueManagerListener::Added(), qi);
			}
			if(!simple)
				cur = qi;
		} else if(cur != NULL && name == sSource) {
			const string& cid = getAttrib(attribs, sCID, 0);
			if(cid.length() != 39) {
				// Skip loading this source - sorry old users
				return;
			}
			User::Ptr user = ClientManager::getInstance()->getUser(CID(cid));

			try {
				if(qm->addSource(cur, user, 0) && user->isOnline())
					ConnectionManager::getInstance()->getDownloadConnection(user);
			} catch(const Exception&) {
				return;
			}
		}
	}
}

void QueueLoader::endTag(const string& name, const string&) {
	if(inDownloads) {
		if(name == sDownload)
			cur = NULL;
		else if(name == "Downloads")
			inDownloads = false;
	}
}

// SearchManagerListener
void QueueManager::on(SearchManagerListener::SR, SearchResult* sr) throw() {
	bool added = false;
	bool wantConnection = false;

	if(BOOLSETTING(AUTO_SEARCH)) {
		Lock l(cs);
		QueueItem::List matches;

		fileQueue.find(matches, sr->getTTH());

		for(QueueItem::Iter i = matches.begin(); i != matches.end(); ++i) {
			QueueItem* qi = *i;

			// Size compare to avoid popular spoof
			if(qi->getSize() == sr->getSize() && !qi->isSource(sr->getUser())) {
				try {
					if(!BOOLSETTING(AUTO_SEARCH_AUTO_MATCH))
						wantConnection = addSource(qi, sr->getUser(), 0);
					added = true;
				} catch(const Exception&) {
					// ...
				}
				break;
			}
		}
	}

	if(added && BOOLSETTING(AUTO_SEARCH_AUTO_MATCH)) {
		try {
			addList(sr->getUser(), QueueItem::FLAG_MATCH_QUEUE);
		} catch(const Exception&) {
			// ...
		}
	}
	if(added && sr->getUser()->isOnline() && wantConnection)
		ConnectionManager::getInstance()->getDownloadConnection(sr->getUser());

}

// ClientManagerListener
void QueueManager::on(ClientManagerListener::UserConnected, const User::Ptr& aUser) throw() {
	bool hasDown = false;
	{
		Lock l(cs);
		for(int i = 0; i < QueueItem::LAST; ++i) {
			QueueItem::UserListIter j = userQueue.getList(i).find(aUser);
			if(j != userQueue.getList(i).end()) {
				for(QueueItem::Iter m = j->second.begin(); m != j->second.end(); ++m)
					fire(QueueManagerListener::StatusUpdated(), *m);
				if(i != QueueItem::PAUSED)
					hasDown = true;
			}
		}

		if(pfsQueue.find(aUser->getCID()) != pfsQueue.end()) {
			hasDown = true;
		}
	}

	if(hasDown)
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

void QueueManager::on(TimerManagerListener::Second, uint32_t aTick) throw() {
	if(dirty && ((lastSave + 10000) < aTick)) {
		saveQueue();
	}
}

void QueueItem::addSource(const User::Ptr& aUser) {
	dcassert(!isSource(aUser));
	SourceIter i = getBadSource(aUser);
	if(i != badSources.end()) {
		sources.push_back(*i);
		badSources.erase(i);
	} else {
		sources.push_back(Source(aUser));
	}
}

void QueueItem::removeSource(const User::Ptr& aUser, int reason) {
	SourceIter i = getSource(aUser);
	dcassert(i != sources.end());
	i->setFlag(reason);
	badSources.push_back(*i);
	sources.erase(i);
}
