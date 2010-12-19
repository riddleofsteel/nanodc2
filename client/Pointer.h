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

#if !defined(POINTER_H)
#define POINTER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Thread.h"

class PointerBase
{
public:
	void inc() throw() {
		dcassert(ref>=0);
		Thread::safeInc(ref);
	}

	void dec() throw() {
		dcassert(ref>0);

		if ( (Thread::safeDec(ref)) == 0 ) {
			//dcdebug("Smart Object at 0x%08x deleted\n", this);
			delete this;
		}
	}
	bool unique() throw() {
		return (ref == 1);
	}

protected:
	PointerBase() throw() : ref(0) { }

	virtual ~PointerBase() throw() {
		dcassert(!ref);
	}

private:
	volatile long ref;
};

/**
 * Note; don't forget to make the destructor virtual if deriving from this class
 */
template <class T>
class Pointer
{
public:
	Pointer ( PointerBase *aBase = 0) throw() : base(aBase) {
		if ( base ) {
			base->inc();
		}
	}

	Pointer( const Pointer &rhs ) throw() : base(rhs.base) {
		if ( base ) {
			base->inc();
		}
	}

	Pointer &operator =( const Pointer &rhs ) throw() {
		if ( rhs.base ) {
			rhs.base->inc();
		}

		if ( base ) {
			base->dec();
		}

		base = rhs.base;
		return *this;
	}

	Pointer &operator =( T* rhs ) throw() {
		if (rhs) {
			rhs->inc();
			if ( base ) {
				base->dec();
			}
			base = rhs;
		}


		return *this;
	}

	~Pointer() throw() {
		if ( base ) {
			base->dec();
		}
	}

	T*			operator->()		{ return		asT();	}
	T&			operator* ()		{ return	   *asT();	}
	const T*	operator->()  const { return		asT();	}
	const T&	operator* ()  const { return	   *asT();	}

	operator		  bool()  const { return base != NULL; }

	bool operator==(T* rhs) const { return (T*)base == rhs; }
	bool operator==(const Pointer& rhs) const { return base == rhs.base; }
	bool operator!=(T* rhs) const { return (T*)base != rhs; }
	bool operator!=(const Pointer& rhs) const { return base != rhs.base; }
	bool operator<(T* rhs) const { return (T*)base < rhs; }
	bool operator<(const Pointer& rhs) const { return base < rhs.base; }
	bool operator>(T* rhs) const { return (T*)base > rhs; }
	bool operator>(const Pointer& rhs) const { return base > rhs.base; }


	static void swap ( Pointer &lhs, Pointer &rhs ) {
		PointerBase *temp = lhs.base;
		lhs.base = rhs.base;
		rhs.base = temp;
	}

	void release() {
		if ( base ) {
			base->dec();
			base = 0;
		}
	}
private:
	PointerBase* base;

	T* asT () {
		dcassert(base);
		return (T*)base;
	}

	const T* asT()	const {
		dcassert(base);
		return (T*)base;
	}
};

template <class T>
bool operator==(T* lhs, const Pointer<T>& rhs) { return rhs == lhs; }
template <class T>
bool operator<(T* lhs, const Pointer<T>& rhs) { return rhs > lhs; }
template <class T>
bool operator>(T* lhs, const Pointer<T>& rhs) { return rhs < lhs; }

struct DeleteFunction {
	template<typename T>
	void operator()(const T& p) const { delete p; }
};

#endif // !defined(POINTER_H)
