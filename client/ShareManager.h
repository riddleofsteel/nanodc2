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

#if !defined(SHARE_MANAGER_H)
#define SHARE_MANAGER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <memory>

#include "TimerManager.h"
#include "SearchManager.h"
#include "SettingsManager.h"
#include "HashManager.h"
#include "DownloadManager.h"

#include "Exception.h"
#include "CriticalSection.h"
#include "StringSearch.h"
#include "Singleton.h"
#include "BloomFilter.h"
#include "FastAlloc.h"
#include "MerkleTree.h"

using namespace std;

STANDARD_EXCEPTION(ShareException);

class SimpleXML;
class Client;
class File;
class OutputStream;
class MemoryInputStream;

struct ShareLoader;
class ShareManager : public Singleton<ShareManager>, private SettingsManagerListener, private Thread, private TimerManagerListener,
	private HashManagerListener, private DownloadManagerListener
{
public:
	/**
	 * @param aDirectory Physical directory location
	 * @param aName Virtual name
	 */
	void addDirectory(const string& aDirectory, const string & aName) throw(ShareException);
	void removeDirectory(const string& aName);
	void renameDirectory(const string& oName, const string& nName) throw(ShareException);
	string toVirtual(const TTHValue& tth) throw(ShareException);
	string toReal(const string& virtualFile) throw(ShareException);
	TTHValue getTTH(const string& virtualFile) throw(ShareException);

	void refresh(bool dirs = false, bool aUpdate = true, bool block = false) throw(ThreadException, ShareException);
	void setDirty() { xmlDirty = true; }

	void search(SearchResult::List& l, const string& aString, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults);
	void search(SearchResult::List& l, const StringList& params, StringList::size_type maxResults);

	StringPairList getDirectories() const { Lock l(cs); return virtualMap; }

	MemoryInputStream* generatePartialList(const string& dir, bool recurse);
	MemoryInputStream* getTree(const string& virtualFile);

	AdcCommand getFileInfo(const string& aFile) throw(ShareException);

	int64_t getShareSize() throw();
	int64_t getShareSize(const string& aDir) throw();

	size_t getSharedFiles() throw();

	string getShareSizeString() { return Util::toString(getShareSize()); }
	string getShareSizeString(const string& aDir) { return Util::toString(getShareSize(aDir)); }

	SearchManager::TypeModes getType(const string& fileName);

	string validateVirtual(const string& /*aVirt*/);

	void addHits(uint32_t aHits) {
		hits += aHits;
	}

	string getOwnListFile() {
		generateXmlList();
		return getBZXmlFile();
	}

	bool isTTHShared(const TTHValue& tth){
		Lock l(cs);
		return tthIndex.find(tth) != tthIndex.end();
	}

	GETSET(uint32_t, hits, Hits);
	GETSET(string, bzXmlFile, BZXmlFile);
private:
	struct AdcSearch;
	class Directory : public FastAlloc<Directory> {
	public:
		struct File {
			struct StringComp {
				StringComp(const string& s) : a(s) { }
				bool operator()(const File& b) const { return Util::stricmp(a, b.getName()) == 0; }
				const string& a;
			private:
				StringComp& operator=(const StringComp&);
			};
			struct FileLess {
				bool operator()(const File& a, const File& b) const { return (Util::stricmp(a.getName(), b.getName()) < 0); }
			};
			typedef set<File, FileLess> Set;
			typedef Set::iterator Iter;

			File() : size(0), parent(NULL) { }
			File(const string& aName, int64_t aSize, Directory* aParent, const TTHValue& aRoot) :
			name(aName), tth(aRoot), size(aSize), parent(aParent) { }
			File(const File& rhs) :
			name(rhs.getName()), tth(rhs.getTTH()), size(rhs.getSize()), parent(rhs.getParent()) { }

			~File() { }

			File& operator=(const File& rhs) {
				name = rhs.name; size = rhs.size; parent = rhs.parent; tth = rhs.tth;
				return *this;
			}

			bool operator==(const File& rhs) const {
				return getParent() == rhs.getParent() && (Util::stricmp(getName(), rhs.getName()) == 0);
			}

			string getADCPath() const { return parent->getADCPath() + name; }
			string getFullName() const { return parent->getFullName() + getName(); }

			GETSET(string, name, Name);
			GETSET(TTHValue, tth, TTH);
			GETSET(int64_t, size, Size);
			GETSET(Directory*, parent, Parent);
		};

		typedef Directory* Ptr;
		typedef HASH_MAP<string, Ptr> Map;
		typedef Map::iterator MapIter;

		int64_t size;
		Map directories;
		File::Set files;

		Directory(const string& aName = Util::emptyString, Directory* aParent = NULL) :
		size(0), name(aName), parent(aParent), fileTypes(0) {
		}

		~Directory();

		bool hasType(uint32_t type) const throw() {
			return ( (type == SearchManager::TYPE_ANY) || (fileTypes & (1 << type)) );
		}
		void addType(uint32_t type) throw();

		string getADCPath() const throw();
		string getFullName() const throw();

		int64_t getSize();
		size_t countFiles();

		void search(SearchResult::List& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults) throw();
		void search(SearchResult::List& aResults, AdcSearch& aStrings, StringList::size_type maxResults) throw();

		void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList);
		void filesToXml(OutputStream& xmlFile, string& indent, string& tmp2);

		File::Iter findFile(const string& aFile) { return find_if(files.begin(), files.end(), Directory::File::StringComp(aFile)); }

		GETSET(string, name, Name);
		GETSET(Directory*, parent, Parent);
	private:
		Directory(const Directory&);
		Directory& operator=(const Directory&);

		/** Set of flags that say which SearchManager::TYPE_* a directory contains */
		uint32_t fileTypes;

	};

	friend class Directory;
	friend struct ShareLoader;

	friend class Singleton<ShareManager>;
	ShareManager();

	virtual ~ShareManager();

	struct AdcSearch {
		AdcSearch(const StringList& params);

		bool isExcluded(const string& str) {
			for(StringSearch::Iter i = exclude.begin(); i != exclude.end(); ++i) {
				if(i->match(str))
					return true;
			}
			return false;
		}

		bool hasExt(const string& name) {
			if(ext.empty())
				return true;
			for(StringIter i = ext.begin(); i != ext.end(); ++i) {
				if(name.length() >= i->length() && Util::stricmp(name.c_str() + name.length() - i->length(), i->c_str()) == 0)
					return true;
			}
			return false;
		}

		StringSearch::List* include;
		StringSearch::List includeX;
		StringSearch::List exclude;
		StringList ext;

		int64_t gt;
		int64_t lt;

		TTHValue root;
		bool hasRoot;

		bool isDirectory;
	};

	typedef HASH_MAP_X(TTHValue, Directory::File::Set::const_iterator, TTHValue::Hash, equal_to<TTHValue>, less<TTHValue>) HashFileMap;
	typedef HashFileMap::iterator HashFileIter;

	HashFileMap tthIndex;

	int64_t xmlListLen;
	TTHValue xmlRoot;
	int64_t bzXmlListLen;
	TTHValue bzXmlRoot;
	std::auto_ptr<File> bzXmlRef;

	bool xmlDirty;
	bool refreshDirs;
	bool update;
	bool initial;

	int listN;

	volatile long refreshing;

	uint32_t lastXmlUpdate;
	uint32_t lastFullUpdate;

	mutable CriticalSection cs;

	// Map real name to directory structure
	Directory::Map directories;

	// Map virtual to real dir name
	StringPairList virtualMap;

	BloomFilter<5> bloom;

	/** Find virtual name from real name */
	StringPairIter findVirtual(const string& realName);
	/** Find real name from virtual name */
	StringPairIter findReal(const string& virtualName);

	bool checkFile(const string& virtualFile, string& realFile, Directory::File::Iter& it);

	Directory* buildTree(const string& aName, Directory* aParent);

	void rebuildIndices();

	void addTree(Directory& aDirectory);
	void addFile(Directory& dir, Directory::File::Iter i);
	void generateXmlList();
	bool loadCache();

	Directory* getDirectory(const string& fname);

	virtual int run();

	// DownloadManagerListener
	virtual void on(DownloadManagerListener::Complete, Download* d) throw();

	// HashManagerListener
	virtual void on(HashManagerListener::TTHDone, const string& fname, const TTHValue& root) throw();

	// SettingsManagerListener
	virtual void on(SettingsManagerListener::Save, SimpleXML& xml) throw() {
		save(xml);
	}
	virtual void on(SettingsManagerListener::Load, SimpleXML& xml) throw() {
		load(xml);
	}

	// TimerManagerListener
	virtual void on(TimerManagerListener::Minute, uint32_t tick) throw();
	void load(SimpleXML& aXml);
	void save(SimpleXML& aXml);

};

#endif // !defined(SHARE_MANAGER_H)
