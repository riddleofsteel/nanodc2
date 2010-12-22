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

#include "UploadManager.h"

#include "ConnectionManager.h"
#include "LogManager.h"
#include "ShareManager.h"
#include "ClientManager.h"
#include "FilteredFile.h"
#include "ZUtils.h"
#include "ResourceManager.h"
#include "HashManager.h"
#include "AdcCommand.h"
#include "FavoriteManager.h"
#include "CryptoManager.h"

#include <functional>
template <class _Pair> struct select1st : public std::_Select1st<_Pair> {};

static const string UPLOAD_AREA = "Uploads";

Upload::Upload(UserConnection& conn) : Transfer(conn), stream(0) { 
	conn.setUpload(this);
}

Upload::~Upload() { 
	getUserConnection().setUpload(0);
	delete stream; 
}

void Upload::getParams(const UserConnection& aSource, StringMap& params) {
	Transfer::getParams(aSource, params);
	params["source"] = getSourceFile();
}

UploadManager::UploadManager() throw() : running(0), extra(0), lastGrant(0) {
	ClientManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
}

UploadManager::~UploadManager() throw() {
	TimerManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);
	while(true) {
		{
			Lock l(cs);
			if(uploads.empty())
				break;
		}
		Thread::sleep(100);
	}
}

bool UploadManager::prepareFile(UserConnection& aSource, const string& aType, const string& aFile, int64_t aStartPos, int64_t aBytes, bool listRecursive) {
	if(aFile.empty() || aStartPos < 0 || aBytes < -1 || aBytes == 0) {
		aSource.fileNotAvail("Invalid request");
		return false;
	}

	InputStream* is = 0;
	int64_t start = 0;
	int64_t bytesLeft = 0;
	int64_t size = 0;

	bool userlist = (aFile == Transfer::USER_LIST_NAME_BZ || aFile == Transfer::USER_LIST_NAME);
	bool free = userlist;
	bool leaves = false;
	bool partList = false;

	string sourceFile;
	try {
		if(aType == Transfer::TYPE_FILE) {
			sourceFile = ShareManager::getInstance()->toReal(aFile);

			if(aFile == Transfer::USER_LIST_NAME) {
				// Unpack before sending...
				string bz2 = File(sourceFile, File::READ, File::OPEN).read();
				string xml;
				CryptoManager::getInstance()->decodeBZ2(reinterpret_cast<const uint8_t*>(bz2.data()), bz2.size(), xml);
				// Clear to save some memory...
				string().swap(bz2);
				is = new MemoryInputStream(xml);
				start = 0;
				bytesLeft = size = xml.size();
			} else {
				File* f = new File(sourceFile, File::READ, File::OPEN);

				start = aStartPos;
				size = f->getSize();
				bytesLeft = (aBytes == -1) ? size : aBytes;

				if(size < (start + bytesLeft)) {
					aSource.fileNotAvail();
					delete f;
					return false;
				}

				free = free || (size <= (int64_t)(SETTING(SET_MINISLOT_SIZE) * 1024) );

				f->setPos(start);

				is = f;
				if((start + bytesLeft) < size) {
					is = new LimitedInputStream<true>(is, aBytes);
				}
			}
		} else if(aType == Transfer::TYPE_TTHL) {
			sourceFile = ShareManager::getInstance()->toReal(aFile);
			MemoryInputStream* mis = ShareManager::getInstance()->getTree(aFile);
			if(!mis) {
				aSource.fileNotAvail();
				return false;
			}

			start = 0;
			bytesLeft = size = mis->getSize();
			is = mis;
			leaves = true;
			free = true;
		} else if(aType == Transfer::TYPE_LIST) {
			// Partial file list
			MemoryInputStream* mis = ShareManager::getInstance()->generatePartialList(aFile, listRecursive);
			if(mis == NULL) {
				aSource.fileNotAvail();
				return false;
			}
			// Some old dc++ clients err here...
			aBytes = -1;
			start = 0;
			bytesLeft = size = mis->getSize();
	
			is = mis;
			free = true;
			partList = true;
		} else {
			aSource.fileNotAvail("Unknown file type");
			return false;
		}
	} catch(const ShareException& e) {
		aSource.fileNotAvail(e.getError());
		return false;
	} catch(const Exception& e) {
		LogManager::getInstance()->message(STRING(UNABLE_TO_SEND_FILE) + sourceFile + ": " + e.getError());
		aSource.fileNotAvail();
		return false;
	}

	Lock l(cs);

	bool extraSlot = false;

	if(!aSource.isSet(UserConnection::FLAG_HASSLOT)) {
		bool hasReserved = (reservedSlots.find(aSource.getUser()) != reservedSlots.end());
		bool isFavorite = FavoriteManager::getInstance()->hasSlot(aSource.getUser());

		if(!(hasReserved || isFavorite || getFreeSlots() > 0 || getAutoSlot())) {
			bool supportsFree = aSource.isSet(UserConnection::FLAG_SUPPORTS_MINISLOTS);
			bool allowedFree = aSource.isSet(UserConnection::FLAG_HASEXTRASLOT) || aSource.isSet(UserConnection::FLAG_OP) || getFreeExtraSlots() > 0;
			if(free && supportsFree && allowedFree) {
				extraSlot = true;
			} else {
				delete is;
				aSource.maxedOut();

				// Check for tth root identifier
				string tFile = aFile;
				if (tFile.compare(0, 4, "TTH/") == 0)
					tFile = ShareManager::getInstance()->toVirtual(TTHValue(aFile.substr(4)));

				addFailedUpload(aSource, tFile +
					" (" + Util::toString((aStartPos*1000/(size+10))/10.0)+"% of " + Util::formatBytes(size) + " done)");
				aSource.disconnect();
				return false;
			}
		} else {
			clearUserFiles(aSource.getUser());	// this user is using a full slot, nix them.
		}

		setLastGrant(GET_TICK());
	}

	Upload* u = new Upload(aSource);
	u->setStream(is);
	if(aBytes == -1)
		u->setSize(size);
	else
		u->setSize(start + bytesLeft);

	u->setStartPos(start);
	u->setSourceFile(sourceFile);

	if(userlist)
		u->setFlag(Upload::FLAG_USER_LIST);
	if(leaves)
		u->setFlag(Upload::FLAG_TTH_LEAVES);
	if(partList)
		u->setFlag(Upload::FLAG_PARTIAL_LIST);

	uploads.push_back(u);

	if(!aSource.isSet(UserConnection::FLAG_HASSLOT)) {
		if(extraSlot) {
			if(!aSource.isSet(UserConnection::FLAG_HASEXTRASLOT)) {
				aSource.setFlag(UserConnection::FLAG_HASEXTRASLOT);
				extra++;
			}
		} else {
			if(aSource.isSet(UserConnection::FLAG_HASEXTRASLOT)) {
				aSource.unsetFlag(UserConnection::FLAG_HASEXTRASLOT);
				extra--;
			}
			aSource.setFlag(UserConnection::FLAG_HASSLOT);
			running++;
		}

		reservedSlots.erase(aSource.getUser());
	}

	return true;
}

int64_t UploadManager::getRunningAverage() {
	Lock l(cs);
	int64_t avg = 0;
	for(Upload::Iter i = uploads.begin(); i != uploads.end(); ++i) {
		Upload* u = *i;
		avg += (int)u->getRunningAverage();
	}
	return avg;
}

bool UploadManager::getAutoSlot() {
	/** A 0 in settings means disable */
	if(SETTING(MIN_UPLOAD_SPEED) == 0)
		return false;
	/** Only grant one slot per 30 sec */
	if(GET_TICK() < getLastGrant() + 30*1000)
		return false;
	/** Grant if upload speed is less than the threshold speed */
	return getRunningAverage() < (SETTING(MIN_UPLOAD_SPEED)*1024);
}

void UploadManager::removeUpload(Upload* aUpload) {
	Lock l(cs);
	dcassert(find(uploads.begin(), uploads.end(), aUpload) != uploads.end());
	uploads.erase(remove(uploads.begin(), uploads.end(), aUpload), uploads.end());
	delete aUpload;
}

void UploadManager::reserveSlot(const User::Ptr& aUser) {
	{
		Lock l(cs);
		reservedSlots.insert(aUser);
	}
	if(aUser->isOnline())
		ClientManager::getInstance()->connect(aUser);
}

void UploadManager::on(UserConnectionListener::Get, UserConnection* aSource, const string& aFile, int64_t aResume) throw() {
	if(aSource->getState() != UserConnection::STATE_GET) {
		dcdebug("UM::onGet Bad state, ignoring\n");
		return;
	}

	if(prepareFile(*aSource, Transfer::TYPE_FILE, Util::toAdcFile(aFile), aResume, -1)) {
		aSource->setState(UserConnection::STATE_SEND);
		aSource->fileLength(Util::toString(aSource->getUpload()->getSize()));
	}
}

void UploadManager::on(UserConnectionListener::Send, UserConnection* aSource) throw() {
	if(aSource->getState() != UserConnection::STATE_SEND) {
		dcdebug("UM::onSend Bad state, ignoring\n");
		return;
	}

	Upload* u = aSource->getUpload();
	dcassert(u != NULL);

	u->setStart(GET_TICK());
	aSource->setState(UserConnection::STATE_RUNNING);
	aSource->transmitFile(u->getStream());
	fire(UploadManagerListener::Starting(), u);
}

void UploadManager::on(AdcCommand::GET, UserConnection* aSource, const AdcCommand& c) throw() {
	int64_t aBytes = Util::toInt64(c.getParam(3));
	int64_t aStartPos = Util::toInt64(c.getParam(2));
	const string& fname = c.getParam(1);
	const string& type = c.getParam(0);

	if(prepareFile(*aSource, type, fname, aStartPos, aBytes, c.hasFlag("RE", 4))) {
		Upload* u = aSource->getUpload();
		dcassert(u != NULL);

		AdcCommand cmd(AdcCommand::CMD_SND);
		cmd.addParam(type).addParam(fname)
			.addParam(Util::toString(u->getPos()))
			.addParam(Util::toString(u->getSize() - u->getPos()));

		if(c.hasFlag("ZL", 4)) {
			u->setStream(new FilteredInputStream<ZFilter, true>(u->getStream()));
			u->setFlag(Upload::FLAG_ZUPLOAD);
			cmd.addParam("ZL1");
		}

		aSource->send(cmd);

		u->setStart(GET_TICK());
		aSource->setState(UserConnection::STATE_RUNNING);
		aSource->transmitFile(u->getStream());
		fire(UploadManagerListener::Starting(), u);
	}
}

void UploadManager::on(UserConnectionListener::BytesSent, UserConnection* aSource, size_t aBytes, size_t aActual) throw() {
	dcassert(aSource->getState() == UserConnection::STATE_RUNNING);
	Upload* u = aSource->getUpload();
	dcassert(u != NULL);
	u->addPos(aBytes, aActual);
}

void UploadManager::on(UserConnectionListener::Failed, UserConnection* aSource, const string& aError) throw() {
	Upload* u = aSource->getUpload();

	if(u) {
		fire(UploadManagerListener::Failed(), u, aError);

		dcdebug("UM::onFailed: Removing upload\n");
		removeUpload(u);
	}

	removeConnection(aSource);
}

void UploadManager::on(UserConnectionListener::TransmitDone, UserConnection* aSource) throw() {
	dcassert(aSource->getState() == UserConnection::STATE_RUNNING);
	Upload* u = aSource->getUpload();
	dcassert(u != NULL);

	aSource->setState(UserConnection::STATE_GET);

	if(BOOLSETTING(LOG_UPLOADS) && !u->isSet(Upload::FLAG_TTH_LEAVES) && (BOOLSETTING(LOG_FILELIST_TRANSFERS) || !u->isSet(Upload::FLAG_USER_LIST))) {
		StringMap params;
		u->getParams(*aSource, params);
		LOG(LogManager::UPLOAD, params);
	}

	fire(UploadManagerListener::Complete(), u);
	removeUpload(u);
}

void UploadManager::addFailedUpload(const UserConnection& source, string filename) {
	{
		Lock l(cs);
		if (!count_if(waitingUsers.begin(), waitingUsers.end(), CompareFirst<User::Ptr, uint32_t>(source.getUser())))
			waitingUsers.push_back(WaitingUser(source.getUser(), GET_TICK()));
		waitingFiles[source.getUser()].insert(filename);		//files for which user's asked
	}

	fire(UploadManagerListener::WaitingAddFile(), source.getUser(), filename);
}

void UploadManager::clearUserFiles(const User::Ptr& source) {
	Lock l(cs);
	//run this when a user's got a slot or goes offline.
	UserList::iterator sit = find_if(waitingUsers.begin(), waitingUsers.end(), CompareFirst<User::Ptr, uint32_t>(source));
	if (sit == waitingUsers.end()) return;

	FilesMap::iterator fit = waitingFiles.find(sit->first);
	if (fit != waitingFiles.end()) waitingFiles.erase(fit);
	fire(UploadManagerListener::WaitingRemoveUser(), sit->first);

	waitingUsers.erase(sit);
}

User::List UploadManager::getWaitingUsers() {
	Lock l(cs);
	User::List u;
	transform(waitingUsers.begin(), waitingUsers.end(), back_inserter(u), select1st<WaitingUser>());
	return u;
}

const UploadManager::FileSet& UploadManager::getWaitingUserFiles(const User::Ptr &u) {
	Lock l(cs);
	return waitingFiles.find(u)->second;
}

void UploadManager::removeConnection(UserConnection* aSource) {
	dcassert(aSource->getUpload() == NULL);
	aSource->removeListener(this);
	if(aSource->isSet(UserConnection::FLAG_HASSLOT)) {
		running--;
		aSource->unsetFlag(UserConnection::FLAG_HASSLOT);
	}
	if(aSource->isSet(UserConnection::FLAG_HASEXTRASLOT)) {
		extra--;
		aSource->unsetFlag(UserConnection::FLAG_HASEXTRASLOT);
	}
}

void UploadManager::on(TimerManagerListener::Minute, uint32_t /* aTick */) throw() {
	User::List disconnects;
	{
		Lock l(cs);

		UserList::iterator i = stable_partition(waitingUsers.begin(), waitingUsers.end(), WaitingUserFresh());
		for (UserList::iterator j = i; j != waitingUsers.end(); ++j) {
			FilesMap::iterator fit = waitingFiles.find(j->first);
			if (fit != waitingFiles.end()) waitingFiles.erase(fit);
			fire(UploadManagerListener::WaitingRemoveUser(), j->first);
		}

		waitingUsers.erase(i, waitingUsers.end());

		if( BOOLSETTING(AUTO_KICK) ) {
			for(Upload::Iter i = uploads.begin(); i != uploads.end(); ++i) {
				Upload* u = *i;
				if(u->getUser()->isOnline()) {
					u->unsetFlag(Upload::FLAG_PENDING_KICK);
					continue;
				}

				if(u->isSet(Upload::FLAG_PENDING_KICK)) {
					disconnects.push_back(u->getUser());
					continue;
				}

				if(BOOLSETTING(AUTO_KICK_NO_FAVS) && FavoriteManager::getInstance()->isFavoriteUser(u->getUser())) {
					continue;
				}

				u->setFlag(Upload::FLAG_PENDING_KICK);
			}
		}
	}

	for(User::Iter i = disconnects.begin(); i != disconnects.end(); ++i) {
		LogManager::getInstance()->message(STRING(DISCONNECTED_USER) + Util::toString(ClientManager::getInstance()->getNicks((*i)->getCID())));
		ConnectionManager::getInstance()->disconnect(*i, false);
	}
}

void UploadManager::on(GetListLength, UserConnection* conn) throw() {
	conn->listLen("42");
}

void UploadManager::on(AdcCommand::GFI, UserConnection* aSource, const AdcCommand& c) throw() {
	if(c.getParameters().size() < 2) {
		aSource->send(AdcCommand(AdcCommand::SEV_RECOVERABLE, AdcCommand::ERROR_PROTOCOL_GENERIC, "Missing parameters"));
		return;
	}

	const string& type = c.getParam(0);
	const string& ident = c.getParam(1);

	if(type == Transfer::TYPE_FILE) {
		try {
			aSource->send(ShareManager::getInstance()->getFileInfo(ident));
		} catch(const ShareException&) {
			aSource->fileNotAvail();
		}
	} else {
		aSource->fileNotAvail();
	}
}

// TimerManagerListener
void UploadManager::on(TimerManagerListener::Second, uint32_t) throw() {
	Lock l(cs);
	Upload::List ticks;

	for(Upload::Iter i = uploads.begin(); i != uploads.end(); ++i) {
		ticks.push_back(*i);
	}

	if(ticks.size() > 0)
		fire(UploadManagerListener::Tick(), ticks);
}

void UploadManager::on(ClientManagerListener::UserDisconnected, const User::Ptr& aUser) throw() {
	if(!aUser->isOnline()) {
		clearUserFiles(aUser);
	}
}
