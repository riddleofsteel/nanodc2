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

#include "Client.h"

#include "BufferedSocket.h"

#include "FavoriteManager.h"
#include "TimerManager.h"
#include "ResourceManager.h"

Client::Counts Client::counts;

Client::Client(const string& hubURL, char separator_, bool secure_) :
	myIdentity(ClientManager::getInstance()->getMe(), 0),
	reconnDelay(120), lastActivity(GET_TICK()), registered(false), autoReconnect(true), reconnecting(false), socket(0),
	hubUrl(hubURL), port(0), separator(separator_),
	secure(secure_), countType(COUNT_UNCOUNTED)
{
	string file;
	Util::decodeUrl(hubURL, address, port, file);

	TimerManager::getInstance()->addListener(this);
}

Client::~Client() throw() {
	dcassert(!socket);
	TimerManager::getInstance()->removeListener(this);
	updateCounts(true);
}

void Client::reconnect() {
	disconnect(true);
	setAutoReconnect(true);
	setReconnecting(true);
}

void Client::shutdown() {

	if(socket) {
		BufferedSocket::putSocket(socket);
		socket = 0;
	}
}

void Client::reloadSettings(bool updateNick) {
	FavoriteHubEntry* hub = FavoriteManager::getInstance()->getFavoriteHubEntry(getHubUrl());
	if(hub) {
		if(updateNick) {
			setCurrentNick(checkNick(hub->getNick(true)));
		}

		if(!hub->getUserDescription().empty()) {
			setCurrentDescription(hub->getUserDescription());
		} else {
			setCurrentDescription(SETTING(DESCRIPTION));
		}
		if(!hub->getPassword().empty())
			setPassword(hub->getPassword());
	} else {
		if(updateNick) {
			setCurrentNick(checkNick(SETTING(NICK)));
		}
		setCurrentDescription(SETTING(DESCRIPTION));
	}
}

void Client::connect() {
	if(socket)
		BufferedSocket::putSocket(socket);

	setAutoReconnect(true);
	setReconnecting(false);
	setReconnDelay(120 + Util::rand(0, 60));
	reloadSettings(true);
	setRegistered(false);
	setMyIdentity(Identity(ClientManager::getInstance()->getMe(), 0));
	setHubIdentity(Identity());

	try {
		socket = BufferedSocket::getSocket(separator);
		socket->addListener(this);
		socket->connect(address, port, secure, BOOLSETTING(ALLOW_UNTRUSTED_HUBS), true);
	} catch(const Exception& e) {
		if(socket) {
			BufferedSocket::putSocket(socket);
			socket = 0;
		}
		fire(ClientListener::Failed(), this, e.getError());
	}
	updateActivity();
}

void Client::on(Connected) throw() {
	updateActivity();
	ip = socket->getIp();
	fire(ClientListener::Connected(), this);
}

void Client::disconnect(bool graceLess) {
	if(!socket)
		return;
	socket->disconnect(graceLess);
}

void Client::updateCounts(bool aRemove) {
	// We always remove the count and then add the correct one if requested...
	if(countType == COUNT_NORMAL) {
		Thread::safeDec(counts.normal);
	} else if(countType == COUNT_REGISTERED) {
		Thread::safeDec(counts.registered);
	} else if(countType == COUNT_OP) {
		Thread::safeDec(counts.op);
	}

	countType = COUNT_UNCOUNTED;

	if(!aRemove) {
		if(getMyIdentity().isOp()) {
			Thread::safeInc(counts.op);
			countType = COUNT_OP;
		} else if(getMyIdentity().isRegistered()) {
			Thread::safeInc(counts.registered);
			countType = COUNT_REGISTERED;
		} else {
			Thread::safeInc(counts.normal);
			countType = COUNT_NORMAL;
		}
	}
}

string Client::getLocalIp() const {
	// Best case - the server detected it
	if((!BOOLSETTING(NO_IP_OVERRIDE) || SETTING(EXTERNAL_IP).empty()) && !getMyIdentity().getIp().empty()) {
		return getMyIdentity().getIp();
	}

	if(!SETTING(EXTERNAL_IP).empty()) {
		return Socket::resolve(SETTING(EXTERNAL_IP));
	}

	string lip;
	if(socket)
		lip = socket->getLocalIp();

	if(lip.empty())
		return Util::getLocalIp();
	return lip;
}

void Client::on(Second, uint32_t) throw() {
}
