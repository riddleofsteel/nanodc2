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

#include "FinishedManager.h"
#include "ClientManager.h"

FinishedManager::~FinishedManager() throw() {
	DownloadManager::getInstance()->removeListener(this);
	UploadManager::getInstance()->removeListener(this);

	Lock l(cs);
	for_each(downloads.begin(), downloads.end(), DeleteFunction());
	for_each(uploads.begin(), uploads.end(), DeleteFunction());
}

void FinishedManager::remove(FinishedItem *item, bool upload /* = false */) {
	{
		Lock l(cs);
		FinishedItem::List *listptr = upload ? &uploads : &downloads;
		FinishedItem::Iter it = find(listptr->begin(), listptr->end(), item);

		if(it != listptr->end())
			listptr->erase(it);
		else
			return;
	}
	if (!upload)
		fire(FinishedManagerListener::RemovedDl(), item);
	else
		fire(FinishedManagerListener::RemovedUl(), item);
	delete item;
}

void FinishedManager::removeAll(bool upload /* = false */) {
	{
		Lock l(cs);
		FinishedItem::List *listptr = upload ? &uploads : &downloads;
		for_each(listptr->begin(), listptr->end(), DeleteFunction());
		listptr->clear();
	}
	if (!upload)
		fire(FinishedManagerListener::RemovedAllDl());
	else
		fire(FinishedManagerListener::RemovedAllUl());
}

FinishedItem* FinishedManager::getFinishedItem(string aTarget, bool upload)
{
	FinishedItem *item = NULL;
	{
		Lock l(cs);
		FinishedItem::List *listptr = upload ? &uploads : &downloads;
		for(FinishedItem::Iter i = listptr->begin(); i != listptr->end(); ++i) {
			if(Util::stricmp((*i)->getTarget(), aTarget) == 0) {
				item = *i;
				break;
			}
		}
	}
	return item;
}

void FinishedManager::on(DownloadManagerListener::Complete, Download* d) throw()
{
	if(!d->isSet(Download::FLAG_TREE_DOWNLOAD) && (!d->isSet(Download::FLAG_USER_LIST) || BOOLSETTING(LOG_FILELIST_TRANSFERS))) {
		FinishedItem *item = new FinishedItem(
			d->getTarget(), Util::toString(ClientManager::getInstance()->getNicks(d->getUser()->getCID())),
			Util::toString(ClientManager::getInstance()->getHubNames(d->getUser()->getCID())),
			d->getSize(), d->getTotal(), (GET_TICK() - d->getStart()), GET_TIME(), d->isSet(Download::FLAG_CRC32_OK));
		{
			Lock l(cs);
			downloads.push_back(item);
		}

		fire(FinishedManagerListener::AddedDl(), item);
	}
}

void FinishedManager::on(UploadManagerListener::Complete, Upload* u) throw()
{
	if(!u->isSet(Upload::FLAG_TTH_LEAVES) && (!u->isSet(Upload::FLAG_USER_LIST) || BOOLSETTING(LOG_FILELIST_TRANSFERS))) {
		FinishedItem *item = new FinishedItem(
			u->getSourceFile(), Util::toString(ClientManager::getInstance()->getNicks(u->getUser()->getCID())),
			Util::toString(ClientManager::getInstance()->getHubNames(u->getUser()->getCID())),
			u->getSize(), u->getTotal(), (GET_TICK() - u->getStart()), GET_TIME());
		{
			Lock l(cs);
			uploads.push_back(item);
		}

		fire(FinishedManagerListener::AddedUl(), item);
	}
}
