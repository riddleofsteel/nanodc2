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

#include "ClientManager.h"

#include "ShareManager.h"
#include "SearchManager.h"
#include "CryptoManager.h"
#include "FavoriteManager.h"
#include "SimpleXML.h"
#include "UserCommand.h"
#include "ResourceManager.h"
#include "LogManager.h"

#include "AdcHub.h"
#include "NmdcHub.h"

Client* ClientManager::getClient(const string& aHubURL) {
	Client* c;
	if(Util::strnicmp("adc://", aHubURL.c_str(), 6) == 0) {
		c = new AdcHub(aHubURL, false);
	} else if(Util::strnicmp("adcs://", aHubURL.c_str(), 7) == 0) {
		c = new AdcHub(aHubURL, true);
	} else {
		c = new NmdcHub(aHubURL);
	}

	{
		Lock l(cs);
		clients.push_back(c);
	}

	c->addListener(this);

	return c;
}

void ClientManager::putClient(Client* aClient) {
	aClient->shutdown();

	fire(ClientManagerListener::ClientDisconnected(), aClient);
	aClient->removeListeners();

	{
		Lock l(cs);
		clients.remove(aClient);
	}
	delete aClient;
}

size_t ClientManager::getUserCount() const {
	Lock l(cs);
	return onlineUsers.size();
}

StringList ClientManager::getHubs(const CID& cid) const {
	Lock l(cs);
	StringList lst;
	OnlinePairC op = onlineUsers.equal_range(cid);
	for(OnlineIterC i = op.first; i != op.second; ++i) {
		lst.push_back(i->second->getClient().getHubUrl());
	}
	return lst;
}

StringList ClientManager::getHubNames(const CID& cid) const {
	Lock l(cs);
	StringList lst;
	OnlinePairC op = onlineUsers.equal_range(cid);
	for(OnlineIterC i = op.first; i != op.second; ++i) {
		lst.push_back(i->second->getClient().getHubName());
	}
	return lst;
}

StringList ClientManager::getNicks(const CID& cid) const {
	Lock l(cs);
	StringSet nicks;
	OnlinePairC op = onlineUsers.equal_range(cid);
	for(OnlineIterC i = op.first; i != op.second; ++i) {
		nicks.insert(i->second->getIdentity().getNick());
	}
	if(nicks.empty()) {
		// Offline perhaps?
		UserMap::const_iterator i = users.find(cid);
		if(i != users.end() && !i->second->getFirstNick().empty()) {
			nicks.insert(i->second->getFirstNick());
		} else {
			nicks.insert('{' + cid.toBase32() + '}');
		}
	}
	return StringList(nicks.begin(), nicks.end());
}

string ClientManager::getConnection(const CID& cid) const {
	Lock l(cs);
	OnlineIterC i = onlineUsers.find(cid);
	if(i != onlineUsers.end()) {
		return i->second->getIdentity().getConnection();
	}
	return STRING(OFFLINE);
}

int64_t ClientManager::getAvailable() const {
	Lock l(cs);
	int64_t bytes = 0;
	for(OnlineIterC i = onlineUsers.begin(); i != onlineUsers.end(); ++i) {
		bytes += i->second->getIdentity().getBytesShared();
	}

	return bytes;
}

bool ClientManager::isConnected(const string& aUrl) const {
	Lock l(cs);

	for(Client::List::const_iterator i = clients.begin(); i != clients.end(); ++i) {
		if((*i)->getHubUrl() == aUrl) {
			return true;
		}
	}
	return false;
}

string ClientManager::findHub(const string& ipPort) const {
	Lock l(cs);

	string ip;
	uint16_t port = 411;
	string::size_type i = ipPort.find(':');
	if(i == string::npos) {
		ip = ipPort;
	} else {
		ip = ipPort.substr(0, i);
		port = static_cast<uint16_t>(Util::toInt(ipPort.substr(i+1)));
	}

	string url;
	for(Client::List::const_iterator i = clients.begin(); i != clients.end(); ++i) {
		const Client* c = *i;
		if(c->getIp() == ip) {
			// If exact match is found, return it
			if(c->getPort() == port)
				return c->getHubUrl();

			// Port is not always correct, so use this as a best guess...
			url = c->getHubUrl();
		}
	}

	return url;
}

User::Ptr ClientManager::findLegacyUser(const string& aNick) const throw() {
	Lock l(cs);
	dcassert(aNick.size() > 0);

	for(OnlineMap::const_iterator i = onlineUsers.begin(); i != onlineUsers.end(); ++i) {
		const OnlineUser* ou = i->second;
		if(ou->getUser()->isSet(User::NMDC) && Util::stricmp(ou->getIdentity().getNick(), aNick) == 0)
			return ou->getUser();
	}
	return User::Ptr();
}

User::Ptr ClientManager::getUser(const string& aNick, const string& aHubUrl) throw() {
	CID cid = makeCid(aNick, aHubUrl);
	Lock l(cs);

	UserIter ui = users.find(cid);
	if(ui != users.end()) {
		if(ui->second->getFirstNick().empty())
			ui->second->setFirstNick(aNick);
		ui->second->setFlag(User::NMDC);
		return ui->second;
	}

	User::Ptr p(new User(cid));
	p->setFirstNick(aNick);
	p->setFlag(User::NMDC);
	users.insert(make_pair(cid, p));

	return p;
}

User::Ptr ClientManager::getUser(const CID& cid) throw() {
	Lock l(cs);
	UserIter ui = users.find(cid);
	if(ui != users.end()) {
		return ui->second;
	}

	User::Ptr p(new User(cid));
	users.insert(make_pair(cid, p));
	return p;
}

User::Ptr ClientManager::findUser(const CID& cid) const throw() {
	Lock l(cs);
	UserMap::const_iterator ui = users.find(cid);
	if(ui != users.end()) {
		return ui->second;
	}
	return 0;
}

bool ClientManager::isOp(const User::Ptr& user, const string& aHubUrl) const {
	Lock l(cs);
	OnlinePairC p = onlineUsers.equal_range(user->getCID());
	for(OnlineIterC i = p.first; i != p.second; ++i) {
		if(i->second->getClient().getHubUrl() == aHubUrl) {
			return i->second->getIdentity().isOp();
		}
	}
	return false;
}

CID ClientManager::makeCid(const string& aNick, const string& aHubUrl) const throw() {
	string n = Text::toLower(aNick);
	TigerHash th;
	th.update(n.c_str(), n.length());
	th.update(Text::toLower(aHubUrl).c_str(), aHubUrl.length());
	// Construct hybrid CID from the bits of the tiger hash - should be
	// fairly random, and hopefully low-collision
	return CID(th.finalize());
}

void ClientManager::putOnline(OnlineUser* ou) throw() {
	{
		Lock l(cs);
		onlineUsers.insert(make_pair(ou->getUser()->getCID(), ou));
	}

	if(!ou->getUser()->isOnline()) {
		ou->getUser()->setFlag(User::ONLINE);
		fire(ClientManagerListener::UserConnected(), ou->getUser());
	}
}

void ClientManager::putOffline(OnlineUser* ou) throw() {
	bool lastUser = false;
	{
		Lock l(cs);
		OnlinePair op = onlineUsers.equal_range(ou->getUser()->getCID());
		dcassert(op.first != op.second);
		for(OnlineIter i = op.first; i != op.second; ++i) {
			OnlineUser* ou2 = i->second;
			if(ou == ou2) {
				lastUser = (distance(op.first, op.second) == 1);
				onlineUsers.erase(i);
				break;
			}
		}
	}

	if(lastUser) {
		ou->getUser()->unsetFlag(User::ONLINE);
		fire(ClientManagerListener::UserDisconnected(), ou->getUser());
	}
}

void ClientManager::connect(const User::Ptr& p) {
	Lock l(cs);
	OnlineIter i = onlineUsers.find(p->getCID());
	if(i != onlineUsers.end()) {
		OnlineUser* u = i->second;
		u->getClient().connect(*u);
	}
}

void ClientManager::privateMessage(const User::Ptr& p, const string& msg) {
	Lock l(cs);
	OnlineIter i = onlineUsers.find(p->getCID());
	if(i != onlineUsers.end()) {
		OnlineUser* u = i->second;
		u->getClient().privateMessage(*u, msg);
	}
}

void ClientManager::send(AdcCommand& cmd, const CID& cid) {
	Lock l(cs);
	OnlineIter i = onlineUsers.find(cid);
	if(i != onlineUsers.end()) {
		OnlineUser& u = *i->second;
		if(cmd.getType() == AdcCommand::TYPE_UDP && !u.getIdentity().isUdpActive()) {
			cmd.setType(AdcCommand::TYPE_DIRECT);
			cmd.setTo(u.getIdentity().getSID());
			u.getClient().send(cmd);
		} else {
			try {
				udp.writeTo(u.getIdentity().getIp(), static_cast<uint16_t>(Util::toInt(u.getIdentity().getUdpPort())), cmd.toString(getMe()->getCID()));
			} catch(const SocketException&) {
				dcdebug("Socket exception sending ADC UDP command\n");
			}
		}
	}
}

void ClientManager::infoUpdated() {
	Lock l(cs);
	for(Client::Iter i = clients.begin(); i != clients.end(); ++i) {
		if((*i)->isConnected()) {
			(*i)->info(false);
		}
	}
}

void ClientManager::on(NmdcSearch, Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize,
									int aFileType, const string& aString) throw()
{
	Speaker<ClientManagerListener>::fire(ClientManagerListener::IncomingSearch(), aString);

	bool isPassive = (aSeeker.compare(0, 4, "Hub:") == 0);

	// We don't wan't to answer passive searches if we're in passive mode...
	if(isPassive && !ClientManager::getInstance()->isActive()) {
		return;
	}

	SearchResult::List l;
	ShareManager::getInstance()->search(l, aString, aSearchType, aSize, aFileType, aClient, isPassive ? 5 : 10);
//		dcdebug("Found %d items (%s)\n", l.size(), aString.c_str());
	if(l.size() > 0) {
		if(isPassive) {
			string name = aSeeker.substr(4);
			// Good, we have a passive seeker, those are easier...
			string str;
			for(SearchResult::Iter i = l.begin(); i != l.end(); ++i) {
				SearchResult* sr = *i;
				str += sr->toSR(*aClient);
				str[str.length()-1] = 5;
				str += name;
				str += '|';

				sr->decRef();
			}

			if(str.size() > 0)
				aClient->send(str);

		} else {
			try {
				string ip, file;
				uint16_t port = 0;
				Util::decodeUrl(aSeeker, ip, port, file);
				ip = Socket::resolve(ip);

				// Temporary fix to avoid spamming hublist.org and dcpp.net
				if(ip == "70.85.55.252" || ip == "207.44.220.108") {
					LogManager::getInstance()->message("Someone is trying to use your client to spam " + ip + ", please urge hub owner to fix this");
					return;
				}

				if(port == 0)
					port = 412;
				for(SearchResult::Iter i = l.begin(); i != l.end(); ++i) {
					SearchResult* sr = *i;
					udp.writeTo(ip, port, sr->toSR(*aClient));
					sr->decRef();
				}
			} catch(const SocketException& /* e */) {
				dcdebug("Search caught error\n");
			}
		}
	}
}

void ClientManager::userCommand(const User::Ptr& p, const ::UserCommand& uc, StringMap& params, bool compatibility) {
	Lock l(cs);
	OnlineIter i = onlineUsers.find(p->getCID());
	if(i == onlineUsers.end())
		return;

	OnlineUser& ou = *i->second;
	ou.getIdentity().getParams(params, "user", compatibility);
	ou.getClient().getHubIdentity().getParams(params, "hub", false);
	ou.getClient().getMyIdentity().getParams(params, "my", compatibility);
	ou.getClient().escapeParams(params);
	ou.getClient().sendUserCmd(Util::formatParams(uc.getCommand(), params, false));
}

void ClientManager::on(AdcSearch, Client*, const AdcCommand& adc, const CID& from) throw() {
	SearchManager::getInstance()->respond(adc, from);
}

void ClientManager::search(int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken) {
	Lock l(cs);

	updateCachedIp(); // no point in doing a resolve for every single hub we're searching on

	for(Client::Iter i = clients.begin(); i != clients.end(); ++i) {
		if((*i)->isConnected()) {
			(*i)->search(aSizeMode, aSize, aFileType, aString, aToken);
		}
	}
}

void ClientManager::search(StringList& who, int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken) {
	Lock l(cs);

	updateCachedIp(); // no point in doing a resolve for every single hub we're searching on

	for(StringIter it = who.begin(); it != who.end(); ++it) {
		string& client = *it;
		for(Client::Iter j = clients.begin(); j != clients.end(); ++j) {
			Client* c = *j;
			if(c->isConnected() && c->getHubUrl() == client) {
				c->search(aSizeMode, aSize, aFileType, aString, aToken);
			}
		}
	}
}

void ClientManager::on(Load, SimpleXML&) throw() {
	users.insert(make_pair(getMe()->getCID(), getMe()));
}

void ClientManager::on(TimerManagerListener::Minute, uint32_t /* aTick */) throw() {
	Lock l(cs);

	// Collect some garbage...
	UserIter i = users.begin();
	while(i != users.end()) {
		if(i->second->unique()) {
			users.erase(i++);
		} else {
			++i;
		}
	}

	for(Client::Iter j = clients.begin(); j != clients.end(); ++j) {
		(*j)->info(false);
	}
}

User::Ptr& ClientManager::getMe() {
	if(!me) {
		Lock l(cs);
		if(!me) {
			me = new User(getMyCID());
			me->setFirstNick(SETTING(NICK));
		}
	}
	return me;
}

const CID& ClientManager::getMyPID() {
	if(pid.isZero())
		pid = CID(SETTING(PRIVATE_ID));
	return pid;
}

CID ClientManager::getMyCID() {
	TigerHash tiger;
	tiger.update(getMyPID().data(), CID::SIZE);
	return CID(tiger.finalize());
}

void ClientManager::on(Failed, Client* client, const string&) throw() {
	FavoriteManager::getInstance()->removeUserCommand(client->getHubUrl());
	fire(ClientManagerListener::ClientDisconnected(), client);
}

void ClientManager::on(UserCommand, Client* client, int aType, int ctx, const string& name, const string& command) throw() {
	if(BOOLSETTING(HUB_USER_COMMANDS)) {
 		if(aType == ::UserCommand::TYPE_CLEAR) {
 			FavoriteManager::getInstance()->removeHubUserCommands(ctx, client->getHubUrl());
 		} else {
 			FavoriteManager::getInstance()->addUserCommand(aType, ctx, ::UserCommand::FLAG_NOSAVE, name, command, client->getHubUrl());
 		}
	}
}

void ClientManager::updateCachedIp() {
	// Best case - the server detected it
	if((!BOOLSETTING(NO_IP_OVERRIDE) || SETTING(EXTERNAL_IP).empty())) {
		for(Client::Iter i = clients.begin(); i != clients.end(); ++i) {
			if(!(*i)->getMyIdentity().getIp().empty()) {
				cachedIp = (*i)->getMyIdentity().getIp();
				return;
			}
		}
	}

	if(!SETTING(EXTERNAL_IP).empty()) {
		cachedIp = Socket::resolve(SETTING(EXTERNAL_IP));
		return;
	}

	//if we've come this far just use the first client to get the ip.
	if(clients.size() > 0)
		cachedIp = (*clients.begin())->getLocalIp();
}
