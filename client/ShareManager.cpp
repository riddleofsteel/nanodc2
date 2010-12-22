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

#include "ShareManager.h"
#include "ResourceManager.h"

#include "CryptoManager.h"
#include "ClientManager.h"
#include "LogManager.h"
#include "HashManager.h"

#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "File.h"
#include "FilteredFile.h"
#include "BZUtils.h"

#ifndef _WIN32
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>
#endif

#include <limits>

ShareManager::ShareManager() : hits(0), xmlListLen(0), bzXmlListLen(0), 
	xmlDirty(true), refreshDirs(false), update(false), initial(true), listN(0), refreshing(0), 
	lastXmlUpdate(0), lastFullUpdate(GET_TICK()), bloom(1<<20)
{
	SettingsManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
	DownloadManager::getInstance()->addListener(this);
	HashManager::getInstance()->addListener(this);
}

ShareManager::~ShareManager() {
	SettingsManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	DownloadManager::getInstance()->removeListener(this);
	HashManager::getInstance()->removeListener(this);

	join();

	StringList lists = File::findFiles(Util::getConfigPath(), "files?*.xml.bz2");
	for_each(lists.begin(), lists.end(), File::deleteFile);

	for(Directory::MapIter j = directories.begin(); j != directories.end(); ++j) {
		delete j->second;
	}
}

ShareManager::Directory::~Directory() {
	for(MapIter i = directories.begin(); i != directories.end(); ++i)
		delete i->second;
}

string ShareManager::toVirtual(const TTHValue& tth) throw(ShareException) {
	Lock l(cs);
	HashFileIter i = tthIndex.find(tth);
	if(i != tthIndex.end()) {
		return i->second->getADCPath();
	} else {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}
}

string ShareManager::toReal(const string& virtualFile) throw(ShareException) {
	if(virtualFile == "MyList.DcLst") {
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");
	} else if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		generateXmlList();
		return getBZXmlFile();
	} else {
		string realFile;
		Lock l(cs);

		Directory::File::Iter it;
		if(!checkFile(virtualFile, realFile, it)) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}
		return realFile;
	}
}

TTHValue ShareManager::getTTH(const string& virtualFile) throw(ShareException) {
	Lock l(cs);
	string realFile;
	Directory::File::Iter it;
	if(!checkFile(virtualFile, realFile, it))
		throw ShareException();
	return it->getTTH();
}

MemoryInputStream* ShareManager::getTree(const string& virtualFile) {
	TigerTree tree;
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		if(!HashManager::getInstance()->getTree(TTHValue(virtualFile.substr(4)), tree))
			return 0;
	} else {
		try {
			TTHValue tth = getTTH(virtualFile);
			HashManager::getInstance()->getTree(tth, tree);
		} catch(const Exception&) {
			return 0;
		}
	}

	vector<uint8_t> buf = tree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

AdcCommand ShareManager::getFileInfo(const string& aFile) throw(ShareException) {
	if(aFile == Transfer::USER_LIST_NAME) {
		generateXmlList();
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(xmlListLen));
		cmd.addParam("TR", xmlRoot.toBase32());
		return cmd;
	} else if(aFile == Transfer::USER_LIST_NAME_BZ) {
		generateXmlList();

		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(bzXmlListLen));
		cmd.addParam("TR", bzXmlRoot.toBase32());
		return cmd;
	}

	if(aFile.compare(0, 4, "TTH/") != 0)
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);

	Lock l(cs);
	TTHValue val(aFile.substr(4));
	HashFileIter i = tthIndex.find(val);
	if(i == tthIndex.end()) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	Directory::File::Set::const_iterator f = i->second;
	AdcCommand cmd(AdcCommand::CMD_RES);
	cmd.addParam("FN", f->getADCPath());
	cmd.addParam("SI", Util::toString(f->getSize()));
	cmd.addParam("TR", f->getTTH().toBase32());
	return cmd;
}

StringPairIter ShareManager::findVirtual(const string& realName) {
	for(StringPairIter i = virtualMap.begin(); i != virtualMap.	end(); ++i) {
		if(Util::stricmp(realName, i->second) == 0)
			return i;
	}
	return virtualMap.end();
}

StringPairIter ShareManager::findReal(const string& virtualName) {
	for(StringPairIter i = virtualMap.begin(); i != virtualMap.	end(); ++i) {
		if(Util::stricmp(virtualName, i->first) == 0)
			return i;
	}
	return virtualMap.end();
}

bool ShareManager::checkFile(const string& virtualFile, string& realFile, Directory::File::Iter& it) {
	string file;
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		file = toVirtual(TTHValue(virtualFile.substr(4)));
	} else if(virtualFile.empty() || virtualFile[0] != '/') {
		return false;
	} else {
		file = virtualFile;
	}

	string::size_type i = file.find('/', 1);
	if(i == string::npos || i == 1) {
		return false;
	}

	StringPairIter k = findReal(file.substr(1, i-1));
	if(k == virtualMap.end()) {
		return false;
	}

	file = file.substr(i + 1);
	
	Directory::MapIter mi = directories.find(k->second);
	if(mi == directories.end())
		return false;
	Directory* d = mi->second;

	string::size_type j = 0;
	while( (i = file.find('/', j)) != string::npos) {
		mi = d->directories.find(file.substr(j, i-j));
		j = i + 1;
		if(mi == d->directories.end())
			return false;
		d = mi->second;
	}

	it = find_if(d->files.begin(), d->files.end(), Directory::File::StringComp(file.substr(j)));
	if(it == d->files.end())
		return false;

	
#ifdef _WIN32
	replace_if(file.begin(), file.end(), bind2nd(equal_to<char>(), '/'), '\\');
#endif

	realFile = k->second + file;
	return true;
}

string ShareManager::validateVirtual(const string& aVirt) {
	string tmp = aVirt;
	string::size_type idx;

	while( (idx = tmp.find_first_of("$|:\\/")) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

void ShareManager::load(SimpleXML& aXml) {
	Lock l(cs);

	if(aXml.findChild("Share")) {
		aXml.stepIn();
		while(aXml.findChild("Directory")) {
			const string& virt = aXml.getChildAttrib("Virtual");
			string d(aXml.getChildData()), newVirt;

			if(d[d.length() - 1] != PATH_SEPARATOR)
				d += PATH_SEPARATOR;
			if(!virt.empty()) {
				newVirt = virt;
				if(newVirt[newVirt.length() - 1] == PATH_SEPARATOR) {
					newVirt.erase(newVirt.length() -1, 1);
				}
			} else {
				newVirt = Util::getLastDir(d);
			}

			// get rid of bad characters in virtual names
			newVirt = validateVirtual(newVirt);

			// add only unique directories
			if(findReal(newVirt) == virtualMap.end()) {
				Directory* dp = new Directory(newVirt);
				directories[d] = dp;
				virtualMap.push_back(make_pair(newVirt, d));
			}
		}
		aXml.stepOut();
	}
}

struct ShareLoader : public SimpleXMLReader::CallBack {
	ShareLoader(ShareManager::Directory::Map& aDirs, StringPairList& aVirts) : dirs(aDirs), virts(aVirts), cur(NULL), depth(0) { }
	virtual void startTag(const string& name, StringPairList& attribs, bool simple) {
		if(name == "Directory") {
			if(depth == 0) {
				const string& name = getAttrib(attribs, "Name", 0);
				for(StringPairIter i = virts.begin(); i != virts.end(); ++i) {
					if(i->first == name) {
						cur = dirs[i->second];
						break;
					}
				}
			} else if(cur != 0) {
				cur = new ShareManager::Directory(getAttrib(attribs, "Name", 0), cur);
				cur->addType(SearchManager::TYPE_DIRECTORY); // needed since we match our own name in directory searches
				cur->getParent()->directories[cur->getName()] = cur;
			}

			if(simple && cur)
				cur = cur->getParent();
			else
				depth++;
		} else if(cur != NULL && name == "File") {
			const string& fname = getAttrib(attribs, "Name", 0);
			const string& size = getAttrib(attribs, "Size", 1);
			const string& root = getAttrib(attribs, "TTH", 2);
			if(fname.empty() || size.empty() || (root.size() != 39)) {
				dcdebug("Invalid file found: %s\n", fname.c_str());
				return;
			}
			cur->files.insert(ShareManager::Directory::File(fname, Util::toInt64(size), cur, TTHValue(root)));
		}
	}
	virtual void endTag(const string& name, const string&) {
		if(name == "Directory") {
			depth--;
			if(cur) {
				cur = cur->getParent();
			}
		}
	}

private:
	ShareManager::Directory::Map& dirs;
	StringPairList& virts;

	ShareManager::Directory* cur;
	size_t depth;
};

bool ShareManager::loadCache() {
	try {
		ShareLoader loader(directories, virtualMap);
		string txt;
		::File ff(Util::getConfigPath() + "files.xml.bz2", ::File::READ, ::File::OPEN);
		FilteredInputStream<UnBZFilter, false> f(&ff);
		const size_t BUF_SIZE = 64*1024;
		AutoArray<char> buf(BUF_SIZE);
		size_t len;
		for(;;) {
			size_t n = BUF_SIZE;
			len = f.read(buf, n);
			txt.append(buf, len);
			if(len < BUF_SIZE)
				break;
		}

		SimpleXMLReader(&loader).fromXML(txt);

		for(Directory::MapIter i = directories.begin(); i != directories.end(); ++i) {
			addTree(*i->second);
		}

		return true;
	} catch(const Exception& e) {
		dcdebug("%s\n", e.getError().c_str());
	}
	return false;
}

void ShareManager::save(SimpleXML& aXml) {
	Lock l(cs);

	aXml.addTag("Share");
	aXml.stepIn();
	for(StringPairIter i = virtualMap.begin(); i != virtualMap.end(); ++i) {
		aXml.addTag("Directory", i->second);
		aXml.addChildAttrib("Virtual", i->first);
	}
	aXml.stepOut();
}

void ShareManager::addDirectory(const string& aDirectory, const string& aName) throw(ShareException) {
	if(aDirectory.empty() || aName.empty()) {
		throw ShareException(STRING(NO_DIRECTORY_SPECIFIED));
	}

	if(Util::stricmp(SETTING(TEMP_DOWNLOAD_DIRECTORY), aDirectory) == 0) {
		throw ShareException(STRING(DONT_SHARE_TEMP_DIRECTORY));
	}

	string d(aDirectory);

	if(d[d.length() - 1] != PATH_SEPARATOR)
		d += PATH_SEPARATOR;

	string vName = validateVirtual(aName);

	Directory* dp = NULL;
	{
		Lock l(cs);

		for(Directory::MapIter i = directories.begin(); i != directories.end(); ++i) {
			if(Util::strnicmp(d, i->first, i->first.length()) == 0) {
				// Trying to share an already shared directory
				throw ShareException(STRING(DIRECTORY_ALREADY_SHARED));
			} else if(Util::strnicmp(d, i->first, d.length()) == 0) {
				// Trying to share a parent directory
				throw ShareException(STRING(REMOVE_ALL_SUBDIRECTORIES));
			}
		}

		if(findReal(vName) != virtualMap.end()) {
			throw ShareException(STRING(VIRTUAL_NAME_EXISTS));
		}
	}

	dp = buildTree(d, NULL);
	dp->setName(vName);

	{
		Lock l(cs);
		addTree(*dp);

		directories[d] = dp;
		virtualMap.push_back(make_pair(vName, d));
		setDirty();
	}
}

void ShareManager::removeDirectory(const string& aDirectory) {
	string d(aDirectory);

	if(d[d.length() - 1] != PATH_SEPARATOR)
		d += PATH_SEPARATOR;

	{
		Lock l(cs);

		Directory::MapIter i = directories.find(d);
		if(i != directories.end()) {
			delete i->second;
			directories.erase(i);
		}

		for(StringPairIter j = virtualMap.begin(); j != virtualMap.end(); ++j) {
			if(Util::stricmp(j->second.c_str(), d.c_str()) == 0) {
				virtualMap.erase(j);
				break;
			}
		}

		rebuildIndices();
		setDirty();
	}

	HashManager::getInstance()->stopHashing(d);
}

void ShareManager::renameDirectory(const string& oName, const string& nName) throw(ShareException) {
	Lock l(cs);
	//Find the virtual name
	if (findReal(nName) != virtualMap.end()) {
		throw ShareException(STRING(VIRTUAL_NAME_EXISTS));
	} else {
		StringPairIter i = findReal(oName);
		// Valid newName, lets rename
		i->first = nName;

		//rename the directory, so it will be updated once
		//a new list is generated.
		if( directories.find(i->second) != directories.end() ) {
			directories.find(i->second)->second->setName(nName);
		}
	}

	//Do not call setDirty here since there might be more
	//dirs that should be renamed, this is to avoid creating
	//a new list during renaming.
}

int64_t ShareManager::getShareSize(const string& aDir) throw() {
	Lock l(cs);
	dcassert(aDir.size()>0);
	Directory::MapIter i = directories.find(aDir);

	if(i != directories.end()) {
		return i->second->getSize();
	}

	return -1;
}

int64_t ShareManager::getShareSize() throw() {
	Lock l(cs);
	int64_t tmp = 0;
	for(Directory::MapIter i = directories.begin(); i != directories.end(); ++i) {
		tmp += i->second->getSize();
	}
	return tmp;
}

size_t ShareManager::getSharedFiles() throw() {
	Lock l(cs);
	size_t tmp = 0;
	for(Directory::MapIter i = directories.begin(); i != directories.end(); ++i) {
		tmp += i->second->countFiles();
	}
	return tmp;
}


string ShareManager::Directory::getADCPath() const throw() {
	if(parent == NULL)
		return '/' + name + '/';
	return parent->getADCPath() + name + '/';
}
string ShareManager::Directory::getFullName() const throw() {
	if(parent == NULL)
		return getName() + '\\';
	return parent->getFullName() + getName() + '\\';
}

void ShareManager::Directory::addType(uint32_t type) throw() {
	if(!hasType(type)) {
		fileTypes |= (1 << type);
		if(getParent() != NULL)
			getParent()->addType(type);
	}
}

class FileFindIter {
#ifdef _WIN32
public:
	/** End iterator constructor */
	FileFindIter() : handle(INVALID_HANDLE_VALUE) { }
	/** Begin iterator constructor, path in utf-8 */
	FileFindIter(const string& path) : handle(INVALID_HANDLE_VALUE) {
		handle = ::FindFirstFile(Text::toT(path).c_str(), &data);
	}

	~FileFindIter() {
		if(handle != INVALID_HANDLE_VALUE) {
			::FindClose(handle);
		}
	}

	FileFindIter& operator++() {
		if(!::FindNextFile(handle, &data)) {
			::FindClose(handle);
			handle = INVALID_HANDLE_VALUE;
		}
		return *this;
	}
	bool operator!=(const FileFindIter& rhs) const { return handle != rhs.handle; }

	struct DirData : public WIN32_FIND_DATA {
		string getFileName() {
			return Text::fromT(cFileName);
		}

		bool isDirectory() {
			return (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) > 0;
		}

		bool isHidden() {
			return ((dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) || (cFileName[0] == L'.'));
		}

		int64_t getSize() {
			return (int64_t)nFileSizeLow | ((int64_t)nFileSizeHigh)<<32;
		}

		uint32_t getLastWriteTime() {
			return File::convertTime(&ftLastWriteTime);
		}
	};


private:
	HANDLE handle;
#else
// This code has been cleaned up/fixed a little.
public:
	FileFindIter() {
		dir = NULL;
		data.ent = NULL;
	}

	~FileFindIter() {
		if (dir) closedir(dir);
	}

	FileFindIter(const string& name) {
		dir = opendir(name.c_str());
		if (!dir)
			return;
		data.base = name;
		data.ent = readdir(dir);
		if (!data.ent) {
			closedir(dir);
			dir = NULL;
		}
	}

	FileFindIter& operator++() {
		if (!dir)
			return *this;
		data.ent = readdir(dir);
		if (!data.ent) {
			closedir(dir);
			dir = NULL;
		}
		return *this;
	}

	// good enough to to say if it's null
	bool operator !=(const FileFindIter& rhs) const {
		return dir != rhs.dir;
	}

	struct DirData {
		DirData() : ent(NULL) {}
		string getFileName() {
			if (!ent) return Util::emptyString;
			return string(ent->d_name);
		}
		bool isDirectory() {
			struct stat inode;
			if (!ent) return false;
			if (stat((base + PATH_SEPARATOR + ent->d_name).c_str(), &inode) == -1) return false;
			return S_ISDIR(inode.st_mode);
		}
		bool isHidden() {
			if (!ent) return false;
			return ent->d_name[0] == '.';
		}
		int64_t getSize() {
			struct stat inode;
			if (!ent) return false;
			if (stat((base + PATH_SEPARATOR + ent->d_name).c_str(), &inode) == -1) return 0;
			return inode.st_size;
		}
		uint32_t getLastWriteTime() {
			struct stat inode;
			if (!ent) return false;
			if (stat((base + PATH_SEPARATOR + ent->d_name).c_str(), &inode) == -1) return 0;
			return inode.st_mtime;
		}
		struct dirent* ent;
		string base;
	};
private:
	DIR* dir;
#endif

public:

	DirData& operator*() { return data; }
	DirData* operator->() { return &data; }

private:
	DirData data;

};

ShareManager::Directory* ShareManager::buildTree(const string& aName, Directory* aParent) {
	Directory* dir = new Directory(Util::getLastDir(aName), aParent);
	dir->addType(SearchManager::TYPE_DIRECTORY); // needed since we match our own name in directory searches

	Directory::File::Iter lastFileIter = dir->files.begin();

	FileFindIter end;
#ifdef _WIN32
		for(FileFindIter i(aName + "*"); i != end; ++i) {
#else
	//the fileiter just searches directorys for now, not sure if more
	//will be needed later
	//for(FileFindIter i(aName + "*"); i != end; ++i) {
	for(FileFindIter i(aName); i != end; ++i) {
#endif
		string name = i->getFileName();

		if(name == "." || name == "..")
			continue;
		if(name.find('$') != string::npos) {
			LogManager::getInstance()->message(STRING(FORBIDDEN_DOLLAR_FILE) + name + " (" + STRING(SIZE) + ": " + Util::toString(File::getSize(name)) + " " + STRING(B) + ") (" + STRING(DIRECTORY) + ": \"" + aName + "\")");
			continue;
		}
		if(!BOOLSETTING(SHARE_HIDDEN) && i->isHidden() )
			continue;

		if(i->isDirectory()) {
			string newName = aName + name + PATH_SEPARATOR;
			if(Util::stricmp(newName + PATH_SEPARATOR, SETTING(TEMP_DOWNLOAD_DIRECTORY)) != 0) {
				dir->directories[name] = buildTree(newName, dir);
			}
		} else {
			// Not a directory, assume it's a file...make sure we're not sharing the settings file...
			if( (Util::stricmp(name.c_str(), "DCPlusPlus.xml") != 0) &&
				(Util::stricmp(name.c_str(), "Favorites.xml") != 0)) {

				int64_t size = i->getSize();
				string fileName = aName + name;
				if(Util::stricmp(fileName, SETTING(TLS_PRIVATE_KEY_FILE)) == 0) {
					continue;
				}
				try {
					//@todo race condition - if hashmanager finishes hashing before buildtree is done...
					if(HashManager::getInstance()->checkTTH(fileName, size, i->getLastWriteTime()))
						lastFileIter = dir->files.insert(lastFileIter, Directory::File(name, size, dir, HashManager::getInstance()->getTTH(fileName, size)));
				} catch(const HashException&) {
				}
			}
		}
	}

	return dir;
}

void ShareManager::addTree(Directory& dir) {
	bloom.add(Text::toLower(dir.getName()));

	for(Directory::MapIter i = dir.directories.begin(); i != dir.directories.end(); ++i) {
		addTree(*i->second);
	}

	for(Directory::File::Iter i = dir.files.begin(); i != dir.files.end(); ) {
		addFile(dir, i++);
	}
}

void ShareManager::rebuildIndices() {
	tthIndex.clear();
	bloom.clear();

	for(Directory::Map::const_iterator i = directories.begin(); i != directories.end(); ++i) {
		addTree(*i->second);
	}
}

void ShareManager::addFile(Directory& dir, Directory::File::Iter i) {
	const Directory::File& f = *i;

	HashFileIter j = tthIndex.find(f.getTTH());
	if(j == tthIndex.end()) {
		dir.size+=f.getSize();
	} else {
		if(!SETTING(LIST_DUPES)) {
			LogManager::getInstance()->message(STRING(DUPLICATE_FILE_NOT_SHARED) + dir.getFullName() + f.getName() + " (" + STRING(SIZE) + ": " + Util::toString(f.getSize()) + " " + STRING(B) + ") " + STRING(DUPLICATE_MATCH) + j->second->getParent()->getFullName() + j->second->getName() );
			dir.files.erase(i);
			return;
		}
	}

	dir.addType(getType(f.getName()));

	tthIndex.insert(make_pair(f.getTTH(), i));
	bloom.add(Text::toLower(f.getName()));
}

void ShareManager::refresh(bool dirs /* = false */, bool aUpdate /* = true */, bool block /* = false */) throw(ThreadException, ShareException) {
	if(Thread::safeExchange(refreshing, 1) == 1) {
		LogManager::getInstance()->message(STRING(FILE_LIST_REFRRESH_IN_PROGRESS));
		return;
	}

	update = aUpdate;
	refreshDirs = dirs;
	join();
	bool cached = false;
	if(initial) {
		cached = loadCache();
		initial = false;
	}
	try {
		start();
		if(block && !cached) {
			join();
		} else {
			setThreadPriority(Thread::LOW);
		}
	} catch(const ThreadException& e) {
		LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FAILED) + e.getError());
	}
}

int ShareManager::run() {
	LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_INITIATED));
	{
		if(refreshDirs) {
			lastFullUpdate = GET_TICK();
			StringPairList dirs;
			Directory::Map newDirs;
			{
				Lock l(cs);
				dirs = virtualMap;
			}

			for(StringPairIter i = dirs.begin(); i != dirs.end(); ++i) {
				Directory* dp = buildTree(i->second, 0);
				dp->setName(i->first);
				newDirs.insert(make_pair(i->second, dp));
			}

			{
				Lock l(cs);
				for(Directory::MapIter i = directories.begin(); i != directories.end(); ++i) {
					delete i->second;
				}
				directories = newDirs;

				rebuildIndices();
			}
			refreshDirs = false;
		}
	}


	LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FINISHED));
	if(update) {
		ClientManager::getInstance()->infoUpdated();
	}
	refreshing = 0;
	return 0;
}

void ShareManager::generateXmlList() {
	Lock l(cs);
	if(xmlDirty && (lastXmlUpdate + 15 * 60 * 1000 < GET_TICK() || lastXmlUpdate < lastFullUpdate)) {
		listN++;

		try {
			string tmp2;
			string indent;

			string newXmlName = Util::getConfigPath() + "files" + Util::toString(listN) + ".xml.bz2";
			{
				File f(newXmlName, File::WRITE, File::TRUNCATE | File::CREATE);
				// We don't care about the leaves...
				CalcOutputStream<TTFilter<1024*1024*1024>, false> bzTree(&f);
				FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
				CountOutputStream<false> count(&bzipper);
				CalcOutputStream<TTFilter<1024*1024*1024>, false> newXmlFile(&count);

				newXmlFile.write(SimpleXML::utf8Header);
				newXmlFile.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"/\" Generator=\"" APPNAME " " VERSIONSTRING "\">\r\n");
				for(Directory::MapIter i = directories.begin(); i != directories.end(); ++i) {
					i->second->toXml(newXmlFile, indent, tmp2, true);
				}
				newXmlFile.write("</FileListing>");
				newXmlFile.flush();

				xmlListLen = count.getCount();

				newXmlFile.getFilter().getTree().finalize();
				bzTree.getFilter().getTree().finalize();

				xmlRoot = newXmlFile.getFilter().getTree().getRoot();
				bzXmlRoot = bzTree.getFilter().getTree().getRoot();
			}

			if(bzXmlRef.get()) {
				bzXmlRef.reset();
				File::deleteFile(getBZXmlFile());
			}

			try {
				File::renameFile(newXmlName, Util::getConfigPath() + "files.xml.bz2");
				newXmlName = Util::getConfigPath() + "files.xml.bz2";
			} catch(const FileException&) {
				// Ignore, this is for caching only...
			}
			bzXmlRef = unique_ptr<File>(new File(newXmlName, File::READ, File::OPEN));
			setBZXmlFile(newXmlName);
			bzXmlListLen = File::getSize(newXmlName);
		} catch(const Exception&) {
			// No new file lists...
		}

		xmlDirty = false;
		lastXmlUpdate = GET_TICK();
	}
}

MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse) {
	if(dir[0] != '/' || dir[dir.size()-1] != '/')
		return NULL;

	string xml = SimpleXML::utf8Header;
	string tmp;
	xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"" + SimpleXML::escape(dir, tmp, false) + "\" Generator=\"" APPNAME " " VERSIONSTRING "\">\r\n";
	StringOutputStream sos(xml);
	string indent = "\t";

	Lock l(cs);
	if(dir == "/") {
		for(ShareManager::Directory::MapIter i = directories.begin(); i != directories.end(); ++i) {
			tmp.clear();
			i->second->toXml(sos, indent, tmp, recurse);
		}
	} else {
		string::size_type i = 1, j = 1;
		ShareManager::Directory::MapIter it = directories.end();
		bool first = true;
		while( (i = dir.find('/', j)) != string::npos) {
			if(i == j) {
				j++;
				continue;
			}

			if(first) {
				first = false;
				StringPairIter k = findReal(dir.substr(j, i-j));
				if(k == virtualMap.end())
					return NULL;
				it = directories.find(k->second);
				if(it == directories.end())
					return NULL;
			} else {
				ShareManager::Directory::MapIter it2 = it->second->directories.find(dir.substr(j, i-j));
				if(it2 == it->second->directories.end()) {
					return NULL;
				}
				it = it2;
			}
			j = i + 1;
		}
		for(ShareManager::Directory::MapIter it2 = it->second->directories.begin(); it2 != it->second->directories.end(); ++it2) {
			it2->second->toXml(sos, indent, tmp, recurse);
		}
		it->second->filesToXml(sos, indent, tmp);
	}

	xml += "</FileListing>";
	return new MemoryInputStream(xml);
}

static const string& escaper(const string& n, string& tmp) {
	if(SimpleXML::needsEscape(n, true, false)) {
		tmp.clear();
		tmp.append(n);
		return SimpleXML::escape(tmp, true, false);
	}
	return n;
}

#define LITERAL(n) n, sizeof(n)-1
void ShareManager::Directory::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList) {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(escaper(name, tmp2));

	if(fullList) {
		xmlFile.write(LITERAL("\">\r\n"));

		indent += '\t';
		for(MapIter i = directories.begin(); i != directories.end(); ++i) {
			i->second->toXml(xmlFile, indent, tmp2, fullList);
		}

		filesToXml(xmlFile, indent, tmp2);

		indent.erase(indent.length()-1);
		xmlFile.write(indent);
		xmlFile.write(LITERAL("</Directory>\r\n"));
	} else {
		if(directories.empty() && files.empty()) {
			xmlFile.write(LITERAL("\" />\r\n"));
		} else {
			xmlFile.write(LITERAL("\" Incomplete=\"1\" />\r\n"));
		}
	}
}

void ShareManager::Directory::filesToXml(OutputStream& xmlFile, string& indent, string& tmp2) {
	for(Directory::File::Iter i = files.begin(); i != files.end(); ++i) {
		const Directory::File& f = *i;

		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(escaper(f.getName(), tmp2));
		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(f.getSize()));
		xmlFile.write(LITERAL("\" TTH=\""));
		tmp2.clear();
		xmlFile.write(f.getTTH().toBase32(tmp2));
		xmlFile.write(LITERAL("\"/>\r\n"));
	}
}

// These ones we can look up as ints (4 bytes...)...

static const char* typeAudio[] = { ".mp3", ".mp2", ".mid", ".wav", ".ogg", ".wma" };
static const char* typeCompressed[] = { ".zip", ".ace", ".rar" };
static const char* typeDocument[] = { ".htm", ".doc", ".txt", ".nfo" };
static const char* typeExecutable[] = { ".exe" };
static const char* typePicture[] = { ".jpg", ".gif", ".png", ".eps", ".img", ".pct", ".psp", ".pic", ".tif", ".rle", ".bmp", ".pcx" };
static const char* typeVideo[] = { ".mpg", ".mov", ".asf", ".avi", ".pxp", ".wmv", ".ogm", ".mkv" };

static const string type2Audio[] = { ".au", ".aiff", ".flac" };
static const string type2Picture[] = { ".ai", ".ps", ".pict" };
static const string type2Video[] = { ".rm", ".divx", ".mpeg" };

#define IS_TYPE(x) ( type == (*((uint32_t*)x)) )
#define IS_TYPE2(x) (Util::stricmp(aString.c_str() + aString.length() - x.length(), x.c_str()) == 0)

static bool checkType(const string& aString, int aType) {
	if(aType == SearchManager::TYPE_ANY)
		return true;

	if(aString.length() < 5)
		return false;

	const char* c = aString.c_str() + aString.length() - 3;
	if(!Text::isAscii(c))
		return false;

	uint32_t type = '.' | (Text::asciiToLower(c[0]) << 8) | (Text::asciiToLower(c[1]) << 16) | (((uint32_t)Text::asciiToLower(c[2])) << 24);

	switch(aType) {
	case SearchManager::TYPE_AUDIO:
		{
			for(size_t i = 0; i < (sizeof(typeAudio) / sizeof(typeAudio[0])); i++) {
				if(IS_TYPE(typeAudio[i])) {
					return true;
				}
			}
			if( IS_TYPE2(type2Audio[0]) || IS_TYPE2(type2Audio[1]) ) {
				return true;
			}
		}
		break;
	case SearchManager::TYPE_COMPRESSED:
		if( IS_TYPE(typeCompressed[0]) || IS_TYPE(typeCompressed[1]) || IS_TYPE(typeCompressed[2]) ) {
			return true;
		}
		break;
	case SearchManager::TYPE_DOCUMENT:
		if( IS_TYPE(typeDocument[0]) || IS_TYPE(typeDocument[1]) ||
			IS_TYPE(typeDocument[2]) || IS_TYPE(typeDocument[3]) ) {
			return true;
		}
		break;
	case SearchManager::TYPE_EXECUTABLE:
		if(IS_TYPE(typeExecutable[0]) ) {
			return true;
		}
		break;
	case SearchManager::TYPE_PICTURE:
		{
			for(size_t i = 0; i < (sizeof(typePicture) / sizeof(typePicture[0])); i++) {
				if(IS_TYPE(typePicture[i])) {
					return true;
				}
			}
			if( IS_TYPE2(type2Picture[0]) || IS_TYPE2(type2Picture[1]) || IS_TYPE2(type2Picture[2]) ) {
				return true;
			}
		}
		break;
	case SearchManager::TYPE_VIDEO:
		{
			for(size_t i = 0; i < (sizeof(typeVideo) / sizeof(typeVideo[0])); i++) {
				if(IS_TYPE(typeVideo[i])) {
					return true;
				}
			}
			if( IS_TYPE2(type2Video[0]) || IS_TYPE2(type2Video[1]) || IS_TYPE2(type2Video[2]) ) {
				return true;
			}
		}
		break;
	default:
		dcasserta(0);
		break;
	}
	return false;
}

SearchManager::TypeModes ShareManager::getType(const string& aFileName) {
	if(aFileName[aFileName.length() - 1] == PATH_SEPARATOR) {
		return SearchManager::TYPE_DIRECTORY;
	}

	if(checkType(aFileName, SearchManager::TYPE_VIDEO))
		return SearchManager::TYPE_VIDEO;
	else if(checkType(aFileName, SearchManager::TYPE_AUDIO))
		return SearchManager::TYPE_AUDIO;
	else if(checkType(aFileName, SearchManager::TYPE_COMPRESSED))
		return SearchManager::TYPE_COMPRESSED;
	else if(checkType(aFileName, SearchManager::TYPE_DOCUMENT))
		return SearchManager::TYPE_DOCUMENT;
	else if(checkType(aFileName, SearchManager::TYPE_EXECUTABLE))
		return SearchManager::TYPE_EXECUTABLE;
	else if(checkType(aFileName, SearchManager::TYPE_PICTURE))
		return SearchManager::TYPE_PICTURE;

	return SearchManager::TYPE_ANY;
}

/**
 * Alright, the main point here is that when searching, a search string is most often found in
 * the filename, not directory name, so we want to make that case faster. Also, we want to
 * avoid changing StringLists unless we absolutely have to --> this should only be done if a string
 * has been matched in the directory name. This new stringlist should also be used in all descendants,
 * but not the parents...
 */
void ShareManager::Directory::search(SearchResult::List& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults) throw() {
	// Skip everything if there's nothing to find here (doh! =)
	if(!hasType(aFileType))
		return;

	StringSearch::List* cur = &aStrings;
	unique_ptr<StringSearch::List> newStr;

	// Find any matches in the directory name
	for(StringSearch::Iter k = aStrings.begin(); k != aStrings.end(); ++k) {
		if(k->match(name)) {
			if(!newStr.get()) {
				newStr = unique_ptr<StringSearch::List>(new StringSearch::List(aStrings));
			}
			newStr->erase(remove(newStr->begin(), newStr->end(), *k), newStr->end());
		}
	}

	if(newStr.get() != 0) {
		cur = newStr.get();
	}

	bool sizeOk = (aSearchType != SearchManager::SIZE_ATLEAST) || (aSize == 0);
	if( (cur->empty()) &&
		(((aFileType == SearchManager::TYPE_ANY) && sizeOk) || (aFileType == SearchManager::TYPE_DIRECTORY)) ) {
		// We satisfied all the search words! Add the directory...(NMDC searches don't support directory size)
		SearchResult* sr = new SearchResult(SearchResult::TYPE_DIRECTORY, 0, getFullName(), TTHValue());
		aResults.push_back(sr);
		ShareManager::getInstance()->setHits(ShareManager::getInstance()->getHits()+1);
	}

	if(aFileType != SearchManager::TYPE_DIRECTORY) {
		for(File::Iter i = files.begin(); i != files.end(); ++i) {

			if(aSearchType == SearchManager::SIZE_ATLEAST && aSize > i->getSize()) {
				continue;
			} else if(aSearchType == SearchManager::SIZE_ATMOST && aSize < i->getSize()) {
				continue;
			}
			StringSearch::Iter j = cur->begin();
			for(; j != cur->end() && j->match(i->getName()); ++j)
				;	// Empty

			if(j != cur->end())
				continue;

			// Check file type...
			if(checkType(i->getName(), aFileType)) {
				SearchResult* sr = new SearchResult(SearchResult::TYPE_FILE, i->getSize(), getFullName() + i->getName(), i->getTTH());
				aResults.push_back(sr);
				ShareManager::getInstance()->setHits(ShareManager::getInstance()->getHits()+1);
				if(aResults.size() >= maxResults) {
					break;
				}
			}
		}
	}

	for(Directory::MapIter l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		l->second->search(aResults, *cur, aSearchType, aSize, aFileType, aClient, maxResults);
	}
}

void ShareManager::search(SearchResult::List& results, const string& aString, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults) {
	Lock l(cs);
	if(aFileType == SearchManager::TYPE_TTH) {
		if(aString.compare(0, 4, "TTH:") == 0) {
			TTHValue tth(aString.substr(4));
			HashFileIter i = tthIndex.find(tth);
			if(i != tthIndex.end()) {
				SearchResult* sr = new SearchResult(SearchResult::TYPE_FILE, i->second->getSize(),
					i->second->getParent()->getFullName() + i->second->getName(), i->second->getTTH());

				results.push_back(sr);
				ShareManager::getInstance()->addHits(1);
			}
		}
		return;
	}
	StringTokenizer<string> t(Text::toLower(aString), '$');
	StringList& sl = t.getTokens();
	if(!bloom.match(sl))
		return;

	StringSearch::List ssl;
	for(StringList::iterator i = sl.begin(); i != sl.end(); ++i) {
		if(!i->empty()) {
			ssl.push_back(StringSearch(*i));
		}
	}
	if(ssl.empty())
		return;

	for(Directory::MapIter j = directories.begin(); (j != directories.end()) && (results.size() < maxResults); ++j) {
		j->second->search(results, ssl, aSearchType, aSize, aFileType, aClient, maxResults);
	}
}

namespace {
	inline uint16_t toCode(char a, char b) { return (uint16_t)a | ((uint16_t)b)<<8; }
}

ShareManager::AdcSearch::AdcSearch(const StringList& params) : include(&includeX), gt(0),
	lt(numeric_limits<int64_t>::max()), hasRoot(false), isDirectory(false)
{
	for(StringIterC i = params.begin(); i != params.end(); ++i) {
		const string& p = *i;
		if(p.length() <= 2)
			continue;

		uint16_t cmd = toCode(p[0], p[1]);
		if(toCode('T', 'R') == cmd) {
			hasRoot = true;
			root = TTHValue(p.substr(2));
			return;
		} else if(toCode('A', 'N') == cmd) {
			includeX.push_back(StringSearch(p.substr(2)));
		} else if(toCode('N', 'O') == cmd) {
			exclude.push_back(StringSearch(p.substr(2)));
		} else if(toCode('E', 'X') == cmd) {
			ext.push_back(p.substr(2));
		} else if(toCode('G', 'E') == cmd) {
			gt = Util::toInt64(p.substr(2));
		} else if(toCode('L', 'E') == cmd) {
			lt = Util::toInt64(p.substr(2));
		} else if(toCode('E', 'Q') == cmd) {
			lt = gt = Util::toInt64(p.substr(2));
		} else if(toCode('T', 'Y') == cmd) {
			isDirectory = (p[2] == '2');
		}
	}
}

void ShareManager::Directory::search(SearchResult::List& aResults, AdcSearch& aStrings, StringList::size_type maxResults) throw() {
	StringSearch::List* cur = aStrings.include;
	StringSearch::List* old = aStrings.include;

	unique_ptr<StringSearch::List> newStr;

	// Find any matches in the directory name
	for(StringSearch::Iter k = cur->begin(); k != cur->end(); ++k) {
		if(k->match(name) && !aStrings.isExcluded(name)) {
			if(!newStr.get()) {
				newStr = unique_ptr<StringSearch::List>(new StringSearch::List(*cur));
			}
			newStr->erase(remove(newStr->begin(), newStr->end(), *k), newStr->end());
		}
	}

	if(newStr.get() != 0) {
		cur = newStr.get();
	}

	bool sizeOk = (aStrings.gt == 0);
	if( cur->empty() && aStrings.ext.empty() && sizeOk ) {
		// We satisfied all the search words! Add the directory...
		SearchResult* sr = new SearchResult(SearchResult::TYPE_DIRECTORY, getSize(), getFullName(), TTHValue());
		aResults.push_back(sr);
		ShareManager::getInstance()->setHits(ShareManager::getInstance()->getHits()+1);
	}

	if(!aStrings.isDirectory) {
		for(File::Iter i = files.begin(); i != files.end(); ++i) {

			if(!(i->getSize() >= aStrings.gt)) {
				continue;
			} else if(!(i->getSize() <= aStrings.lt)) {
				continue;
			}

			if(aStrings.isExcluded(i->getName()))
				continue;

			StringSearch::Iter j = cur->begin();
			for(; j != cur->end() && j->match(i->getName()); ++j)
				;	// Empty

			if(j != cur->end())
				continue;

			// Check file type...
			if(aStrings.hasExt(i->getName())) {

				SearchResult* sr = new SearchResult(SearchResult::TYPE_FILE,
					i->getSize(), getFullName() + i->getName(), i->getTTH());
				aResults.push_back(sr);
				ShareManager::getInstance()->addHits(1);
				if(aResults.size() >= maxResults) {
					return;
				}
			}
		}
	}

	for(Directory::MapIter l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		l->second->search(aResults, aStrings, maxResults);
	}
	aStrings.include = old;
}

void ShareManager::search(SearchResult::List& results, const StringList& params, StringList::size_type maxResults) {
	AdcSearch srch(params);

	Lock l(cs);

	if(srch.hasRoot) {
		HashFileIter i = tthIndex.find(srch.root);
		if(i != tthIndex.end()) {
			SearchResult* sr = new SearchResult(SearchResult::TYPE_FILE,
				i->second->getSize(), i->second->getParent()->getFullName() + i->second->getName(),
				i->second->getTTH());
			results.push_back(sr);
			ShareManager::getInstance()->addHits(1);
		}
		return;
	}

	for(StringSearch::Iter i = srch.includeX.begin(); i != srch.includeX.end(); ++i) {
		if(!bloom.match(i->getPattern()))
			return;
	}

	for(Directory::MapIter j = directories.begin(); (j != directories.end()) && (results.size() < maxResults); ++j) {
		j->second->search(results, srch, maxResults);
	}
}

int64_t ShareManager::Directory::getSize() {
	int64_t tmp = size;
	for(MapIter i = directories.begin(); i != directories.end(); ++i)
		tmp+=i->second->getSize();
	return tmp;
}

size_t ShareManager::Directory::countFiles() {
	size_t tmp = files.size();
	for(MapIter i = directories.begin(); i != directories.end(); ++i)
		tmp+=i->second->countFiles();
	return tmp;
}

ShareManager::Directory* ShareManager::getDirectory(const string& fname) {
	for(Directory::MapIter mi = directories.begin(); mi != directories.end(); ++mi) {
		if(Util::strnicmp(fname, mi->first, mi->first.length()) == 0) {
			Directory* d = mi->second;

			string::size_type i;
			string::size_type j = mi->first.length();
			while( (i = fname.find(PATH_SEPARATOR, j)) != string::npos) {
				mi = d->directories.find(fname.substr(j, i-j));
				j = i + 1;
				if(mi == d->directories.end())
					return NULL;
				d = mi->second;
			}
			return d;
		}
	}
	return NULL;
}

void ShareManager::on(DownloadManagerListener::Complete, Download* d) throw() {
	if(BOOLSETTING(ADD_FINISHED_INSTANTLY)) {
		// Check if finished download is supposed to be shared
		Lock l(cs);
		const string& n = d->getTarget();
		for(Directory::MapIter i = directories.begin(); i != directories.end(); i++) {
			if(Util::strnicmp(i->first, n, i->first.size()) == 0 && n[i->first.size()] == PATH_SEPARATOR) {
				string s = n.substr(i->first.size()+1);
				try {
					// Schedule for hashing, it'll be added automatically later on...
					HashManager::getInstance()->checkTTH(n, d->getSize(), 0);
				} catch(const Exception&) {
					// Not a vital feature...
				}
				break;
			}
		}
	}
}

void ShareManager::on(HashManagerListener::TTHDone, const string& fname, const TTHValue& root) throw() {
	Lock l(cs);
	Directory* d = getDirectory(fname);
	if(d != NULL) {
		Directory::File::Iter i = d->findFile(Util::getFileName(fname));
		if(i != d->files.end()) {
			if(root != i->getTTH())
				tthIndex.erase(i->getTTH());
			// Get rid of false constness...
			Directory::File* f = const_cast<Directory::File*>(&(*i));
			f->setTTH(root);
			tthIndex.insert(make_pair(f->getTTH(), i));
		} else {
			string name = Util::getFileName(fname);
			int64_t size = File::getSize(fname);
			Directory::File::Iter it = d->files.insert(Directory::File(name, size, d, root)).first;
			addFile(*d, it);
		}
		setDirty();
	}
}

void ShareManager::on(TimerManagerListener::Minute, uint32_t tick) throw() {
	if(SETTING(AUTO_REFRESH_TIME) > 0) {
		if(lastFullUpdate + SETTING(AUTO_REFRESH_TIME) * 60 * 1000 < tick) {
			try {
				refresh(true, true);
			} catch(const ShareException&) {
			}
		}
	}
}
