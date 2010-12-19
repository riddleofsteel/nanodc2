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

#if !defined(USER_CONNECTION_H)
#define USER_CONNECTION_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "TimerManager.h"

#include "BufferedSocket.h"
#include "CriticalSection.h"
#include "File.h"
#include "User.h"
#include "AdcCommand.h"
#include "MerkleTree.h"

class UserConnection;

class UserConnectionListener {
public:
	virtual ~UserConnectionListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> BytesSent;
	typedef X<1> Connected;
	typedef X<2> Data;
	typedef X<3> Failed;
	typedef X<4> CLock;
	typedef X<5> Key;
	typedef X<6> Direction;
	typedef X<7> Get;
	typedef X<8> Error;
	typedef X<10> Sending;
	typedef X<11> FileLength;
	typedef X<12> Send;
	typedef X<13> GetListLength;
	typedef X<14> MaxedOut;
	typedef X<15> ModeChange;
	typedef X<16> MyNick;
	typedef X<17> TransmitDone;
	typedef X<18> Supports;
	typedef X<19> FileNotAvailable;
	typedef X<20> ADCGet;
	typedef X<21> ADCSnd;
	typedef X<22> ADCSta;

	virtual void on(BytesSent, UserConnection*, size_t, size_t) throw() { }
	virtual void on(Connected, UserConnection*) throw() { }
	virtual void on(Data, UserConnection*, const uint8_t*, size_t) throw() { }
	virtual void on(Error, UserConnection*, const string&) throw() { }
	virtual void on(Failed, UserConnection*, const string&) throw() { }
	virtual void on(CLock, UserConnection*, const string&, const string&) throw() { }
	virtual void on(Key, UserConnection*, const string&) throw() { }
	virtual void on(Direction, UserConnection*, const string&, const string&) throw() { }
	virtual void on(Get, UserConnection*, const string&, int64_t) throw() { }
	virtual void on(Sending, UserConnection*, int64_t) throw() { }
	virtual void on(FileLength, UserConnection*, int64_t) throw() { }
	virtual void on(Send, UserConnection*) throw() { }
	virtual void on(GetListLength, UserConnection*) throw() { }
	virtual void on(MaxedOut, UserConnection*) throw() { }
	virtual void on(ModeChange, UserConnection*) throw() { }
	virtual void on(MyNick, UserConnection*, const string&) throw() { }
	virtual void on(TransmitDone, UserConnection*) throw() { }
	virtual void on(Supports, UserConnection*, const StringList&) throw() { }
	virtual void on(FileNotAvailable, UserConnection*) throw() { }

	virtual void on(AdcCommand::SUP, UserConnection*, const AdcCommand&) throw() { }
	virtual void on(AdcCommand::INF, UserConnection*, const AdcCommand&) throw() { }
	virtual void on(AdcCommand::GET, UserConnection*, const AdcCommand&) throw() { }
	virtual void on(AdcCommand::SND, UserConnection*, const AdcCommand&) throw() { }
	virtual void on(AdcCommand::STA, UserConnection*, const AdcCommand&) throw() { }
	virtual void on(AdcCommand::RES, UserConnection*, const AdcCommand&) throw() { }
	virtual void on(AdcCommand::GFI, UserConnection*, const AdcCommand&) throw() { }
};

class ConnectionQueueItem;

class Transfer {
public:
	static const string TYPE_FILE;		///< File transfer
	static const string TYPE_LIST;		///< Partial file list
	static const string TYPE_TTHL;		///< TTH Leaves

	static const string USER_LIST_NAME;
	static const string USER_LIST_NAME_BZ;

	Transfer(UserConnection& conn);
	virtual ~Transfer() { };

	int64_t getPos() const { return pos; }
	void setPos(int64_t aPos) { pos = aPos; }

	void resetPos() { pos = getStartPos(); }
	void setStartPos(int64_t aPos) { startPos = aPos; pos = aPos; }
	int64_t getStartPos() const { return startPos; }

	void addPos(int64_t aBytes, int64_t aActual) { pos += aBytes; actual+= aActual; }

	enum { AVG_PERIOD = 30000 };
	void updateRunningAverage();

	int64_t getTotal() const { return getPos() - getStartPos(); }
	int64_t getActual() const { return actual; }

	int64_t getSize() const { return size; }
	void setSize(int64_t aSize) { size = aSize; }

	int64_t getAverageSpeed() const {
		int64_t diff = (int64_t)(GET_TICK() - getStart());
		return (diff > 0) ? (getTotal() * (int64_t)1000 / diff) : 0;
	}

	int64_t getSecondsLeft() {
		updateRunningAverage();
		int64_t avg = getRunningAverage();
		return (avg > 0) ? ((getSize() - getPos()) / avg) : 0;
	}

	int64_t getBytesLeft() const {
		return getSize() - getPos();
	}

	virtual void getParams(const UserConnection& aSource, StringMap& params);

	User::Ptr getUser();

	UserConnection& getUserConnection() { return userConnection; }
	const UserConnection& getUserConnection() const { return userConnection; }

	GETSET(uint32_t, start, Start);
	GETSET(uint32_t, lastTick, LastTick);
	GETSET(int64_t, runningAverage, RunningAverage);
	GETSET(TTHValue, tth, TTH);
private:
	Transfer(const Transfer&);
	Transfer& operator=(const Transfer&);

	/** Bytes on last avg update */
	int64_t last;
	/** Total actual bytes transfered this session (compression?) */
	int64_t actual;
	/** Write position in file */
	int64_t pos;
	/** Starting position */
	int64_t startPos;
	/** Target size of this transfer */
	int64_t size;

	UserConnection& userConnection;
};

class ServerSocket;
class Upload;
class Download;

class UserConnection : public Speaker<UserConnectionListener>,
	private BufferedSocketListener, public Flags, private CommandHandler<UserConnection>
{
public:
	friend class ConnectionManager;

	typedef UserConnection* Ptr;
	typedef vector<Ptr> List;
	typedef List::iterator Iter;

	static const string FEATURE_GET_ZBLOCK;
	static const string FEATURE_MINISLOTS;
	static const string FEATURE_XML_BZLIST;
	static const string FEATURE_ADCGET;
	static const string FEATURE_ZLIB_GET;
	static const string FEATURE_TTHL;
	static const string FEATURE_TTHF;
	static const string FEATURE_ADC_BASE;
	static const string FEATURE_ADC_BZIP;

	static const string FILE_NOT_AVAILABLE;

	enum Modes {
		MODE_COMMAND = BufferedSocket::MODE_LINE,
		MODE_DATA = BufferedSocket::MODE_DATA
	};

	enum Flags {
		FLAG_NMDC = 0x01,
		FLAG_OP = FLAG_NMDC << 1,
		FLAG_UPLOAD = FLAG_OP << 1,
		FLAG_DOWNLOAD = FLAG_UPLOAD << 1,
		FLAG_INCOMING = FLAG_DOWNLOAD << 1,
		FLAG_ASSOCIATED = FLAG_INCOMING << 1,
		FLAG_HASSLOT = FLAG_ASSOCIATED << 1,
		FLAG_HASEXTRASLOT = FLAG_HASSLOT << 1,
		FLAG_INVALIDKEY = FLAG_HASEXTRASLOT << 1,
		FLAG_SUPPORTS_GETZBLOCK = FLAG_INVALIDKEY << 1,
		FLAG_SUPPORTS_MINISLOTS = FLAG_SUPPORTS_GETZBLOCK << 1,
		FLAG_SUPPORTS_XML_BZLIST = FLAG_SUPPORTS_MINISLOTS << 1,
		FLAG_SUPPORTS_ADCGET = FLAG_SUPPORTS_XML_BZLIST << 1,
		FLAG_SUPPORTS_ZLIB_GET = FLAG_SUPPORTS_ADCGET << 1,
		FLAG_SUPPORTS_TTHL = FLAG_SUPPORTS_ZLIB_GET << 1,
		FLAG_SUPPORTS_TTHF = FLAG_SUPPORTS_TTHL << 1
	};

	enum States {
		// ConnectionManager
		STATE_UNCONNECTED,
		STATE_CONNECT,

		// Handshake
		STATE_SUPNICK,		// ADC: SUP, Nmdc: $Nick
		STATE_INF,
		STATE_LOCK,
		STATE_DIRECTION,
		STATE_KEY,

		// UploadManager
		STATE_GET,			// Waiting for GET
		STATE_SEND,			// Waiting for $Send
		STATE_RUNNING,		// Transmitting data

		// DownloadManager
		STATE_FILELENGTH,
		STATE_TREE

	};

	uint16_t getNumber() { return static_cast<uint16_t>(((((size_t)this)>>2) & 0x7fff)); }

	// NMDC stuff
	void myNick(const string& aNick) { send("$MyNick " + Text::utf8ToAcp(aNick) + '|'); }
	void lock(const string& aLock, const string& aPk) { send ("$Lock " + aLock + " Pk=" + aPk + '|'); }
	void key(const string& aKey) { send("$Key " + aKey + '|'); }
	void direction(const string& aDirection, int aNumber) { send("$Direction " + aDirection + " " + Util::toString(aNumber) + '|'); }
	void get(const string& aFile, int64_t aResume) { send("$Get " + aFile + "$" + Util::toString(aResume + 1) + '|'); } 	// No acp - utf conversion here...
	void fileLength(const string& aLength) { send("$FileLength " + aLength + '|'); }
	void startSend() { send("$Send|"); }
	void sending(int64_t bytes) { send(bytes == -1 ? string("$Sending|") : "$Sending " + Util::toString(bytes) + "|"); }
	void error(const string& aError) { send("$Error " + aError + '|'); }
	void listLen(const string& aLength) { send("$ListLen " + aLength + '|'); }
	void maxedOut() { isSet(FLAG_NMDC) ? send("$MaxedOut|") : send(AdcCommand(AdcCommand::SEV_RECOVERABLE, AdcCommand::ERROR_SLOTS_FULL, "Slots full")); }
	void fileNotAvail(const std::string& msg = FILE_NOT_AVAILABLE) { isSet(FLAG_NMDC) ? send("$Error " + msg + "|") : send(AdcCommand(AdcCommand::SEV_RECOVERABLE, AdcCommand::ERROR_FILE_NOT_AVAILABLE, msg)); }

	// ADC Stuff
	void sup(const StringList& features) {
		AdcCommand c(AdcCommand::CMD_SUP);
		for(StringIterC i = features.begin(); i != features.end(); ++i)
			c.addParam(*i);
		send(c);
	}
	void inf(bool withToken);
	void get(const string& aType, const string& aName, const int64_t aStart, const int64_t aBytes) { send(AdcCommand(AdcCommand::CMD_GET).addParam(aType).addParam(aName).addParam(Util::toString(aStart)).addParam(Util::toString(aBytes))); }
	void snd(const string& aType, const string& aName, const int64_t aStart, const int64_t aBytes) { send(AdcCommand(AdcCommand::CMD_SND).addParam(aType).addParam(aName).addParam(Util::toString(aStart)).addParam(Util::toString(aBytes))); }

	void send(const AdcCommand& c) { send(c.toString(0, isSet(FLAG_NMDC))); }

	void supports(const StringList& feat) {
		string x;
		for(StringList::const_iterator i = feat.begin(); i != feat.end(); ++i) {
			x+= *i + ' ';
		}
		send("$Supports " + x + '|');
	}
	void setDataMode(int64_t aBytes = -1) { dcassert(socket); socket->setDataMode(aBytes); }
	void setLineMode(size_t rollback) { dcassert(socket); socket->setLineMode(rollback); }

	void connect(const string& aServer, uint16_t aPort) throw(SocketException, ThreadException);
	void accept(const Socket& aServer) throw(SocketException, ThreadException);

	void disconnect(bool graceless = false) { if(socket) socket->disconnect(graceless); }
	void transmitFile(InputStream* f) { socket->transmitFile(f); }

	const string& getDirectionString() {
		dcassert(isSet(FLAG_UPLOAD) ^ isSet(FLAG_DOWNLOAD));
		return isSet(FLAG_UPLOAD) ? UPLOAD : DOWNLOAD;
	}

	const User::Ptr& getUser() const { return user; }
	User::Ptr& getUser() { return user; }
	bool isSecure() const { return socket && socket->isSecure(); }
	bool isTrusted() const { return socket && socket->isTrusted(); }

	string getRemoteIp() const { return socket->getIp(); }
	Download* getDownload() { dcassert(isSet(FLAG_DOWNLOAD)); return download; }
	void setDownload(Download* d) { dcassert(isSet(FLAG_DOWNLOAD)); download = d; }
	Upload* getUpload() { dcassert(isSet(FLAG_UPLOAD)); return upload; }
	void setUpload(Upload* u) { dcassert(isSet(FLAG_UPLOAD)); upload = u; }

	void handle(AdcCommand::SUP t, const AdcCommand& c) { fire(t, this, c); }
	void handle(AdcCommand::INF t, const AdcCommand& c) { fire(t, this, c); }
	void handle(AdcCommand::GET t, const AdcCommand& c) { fire(t, this, c); }
	void handle(AdcCommand::SND t, const AdcCommand& c) { fire(t, this, c);	}
	void handle(AdcCommand::STA t, const AdcCommand& c) { fire(t, this, c);	}
	void handle(AdcCommand::RES t, const AdcCommand& c) { fire(t, this, c); }
	void handle(AdcCommand::GFI t, const AdcCommand& c) { fire(t, this, c);	}

	// Ignore any other ADC commands for now
	template<typename T> void handle(T , const AdcCommand& ) { }

	GETSET(string, hubUrl, HubUrl);
	GETSET(string, token, Token);
	GETSET(States, state, State);
	GETSET(uint32_t, lastActivity, LastActivity);
private:
	BufferedSocket* socket;
	bool secure;
	User::Ptr user;

	static const string UPLOAD, DOWNLOAD;

	union {
		Download* download;
		Upload* upload;
	};

	// We only want ConnectionManager to create this...
	UserConnection(bool secure_) throw() : state(STATE_UNCONNECTED), lastActivity(0),
		socket(0), secure(secure_), download(NULL) {
	}

	virtual ~UserConnection() throw() {
		BufferedSocket::putSocket(socket);
	}
	friend struct DeleteFunction;

	UserConnection(const UserConnection&);
	UserConnection& operator=(const UserConnection&);

	void setUser(const User::Ptr& aUser) {
		user = aUser;
	}

	void onLine(const string& aLine) throw();

	void send(const string& aString) {
		lastActivity = GET_TICK();
		socket->write(aString);
	}

	virtual void on(Connected) throw() {
		lastActivity = GET_TICK();
		fire(UserConnectionListener::Connected(), this);
	}
	virtual void on(Line, const string&) throw();
	virtual void on(Data, uint8_t* data, size_t len) throw() {
		lastActivity = GET_TICK();
		fire(UserConnectionListener::Data(), this, data, len);
	}
	virtual void on(BytesSent, size_t bytes, size_t actual) throw() {
		lastActivity = GET_TICK();
		fire(UserConnectionListener::BytesSent(), this, bytes, actual);
	}
	virtual void on(ModeChange) throw() {
		lastActivity = GET_TICK();
		fire(UserConnectionListener::ModeChange(), this);
	}
	virtual void on(TransmitDone) throw() { fire(UserConnectionListener::TransmitDone(), this); }
	virtual void on(Failed, const string&) throw();
};

#endif // !defined(USER_CONNECTION_H)
