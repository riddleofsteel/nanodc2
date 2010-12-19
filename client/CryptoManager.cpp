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

#include "CryptoManager.h"

#include "BitInputStream.h"
#include "BitOutputStream.h"
#include "ResourceManager.h"
#include "LogManager.h"
#include "ClientManager.h"

#ifdef _WIN32
#include "../bzip2/bzlib.h"
#else
#include <bzlib.h>
#endif

CryptoManager::CryptoManager()
:
	dh(DH_new()),
	certsLoaded(false),
	lock("EXTENDEDPROTOCOLABCABCABCABCABCABC"),
	pk("DCPLUSPLUS" VERSIONSTRING "ABCABC")
{
	SSL_library_init();

	// Probably should check the return value of these.
	clientContext = SSL_CTX_new(TLSv1_client_method());
	clientVerContext = SSL_CTX_new(TLSv1_client_method());
	serverContext = SSL_CTX_new(TLSv1_server_method());
	serverVerContext = SSL_CTX_new(TLSv1_server_method());

	static unsigned char dh512_p[] =
	{
		0xDA,0x58,0x3C,0x16,0xD9,0x85,0x22,0x89,0xD0,0xE4,0xAF,0x75,
			0x6F,0x4C,0xCA,0x92,0xDD,0x4B,0xE5,0x33,0xB8,0x04,0xFB,0x0F,
			0xED,0x94,0xEF,0x9C,0x8A,0x44,0x03,0xED,0x57,0x46,0x50,0xD3,
			0x69,0x99,0xDB,0x29,0xD7,0x76,0x27,0x6B,0xA2,0xD3,0xD4,0x12,
			0xE2,0x18,0xF4,0xDD,0x1E,0x08,0x4C,0xF6,0xD8,0x00,0x3E,0x7C,
			0x47,0x74,0xE8,0x33,
	};

	static unsigned char dh512_g[] =
	{
		0x02,
	};

	if(dh) {
		dh->p = BN_bin2bn(dh512_p, sizeof(dh512_p), 0);
		dh->g = BN_bin2bn(dh512_g, sizeof(dh512_g), 0);

		if (!dh->p || !dh->g) {
			DH_free(dh);
			dh = 0;
		} else {
			SSL_CTX_set_tmp_dh(serverContext, dh);
			SSL_CTX_set_tmp_dh(serverVerContext, dh);
		}
	}
}

CryptoManager::~CryptoManager() {
	if(serverContext)
		SSL_CTX_free(serverContext);
	if(clientContext)
		SSL_CTX_free(clientContext);
	if(serverVerContext)
		SSL_CTX_free(serverVerContext);
	if(clientVerContext)
		SSL_CTX_free(clientVerContext);
	if(dh)
		DH_free(dh);
}

bool CryptoManager::TLSOk() const throw() {
	return BOOLSETTING(USE_TLS) && certsLoaded;
}

void CryptoManager::generateCertificate() throw(CryptoException) {
	// Generate certificate using OpenSSL
	if(SETTING(TLS_PRIVATE_KEY_FILE).empty()) {
		throw CryptoException("No private key file chosen");
	}
	if(SETTING(TLS_CERTIFICATE_FILE).empty()) {
		throw CryptoException("No certificate file chosen");
	}

#ifdef _WIN32
	wstring cmd = L"openssl.exe genrsa -out \"" + Text::utf8ToWide(SETTING(TLS_PRIVATE_KEY_FILE)) + L"\" 2048";
	PROCESS_INFORMATION pi = { 0 };
	STARTUPINFO si = { 0 };
	si.cb = sizeof(si);

	if(!CreateProcess(0, const_cast<wchar_t*>(cmd.c_str()), 0, 0, FALSE, 0, 0, 0, &si, &pi)) {
		throw CryptoException(Util::translateError(::GetLastError()));
	}
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	cmd = L"openssl.exe req -x509 -new -batch -days 3650 -key \"" + Text::utf8ToWide(SETTING(TLS_PRIVATE_KEY_FILE)) +
		L"\" -out \"" + Text::utf8ToWide(SETTING(TLS_CERTIFICATE_FILE)) + L"\" -subj \"/CN=" +
		Text::utf8ToWide(ClientManager::getInstance()->getMyCID().toBase32()) + L"\"";

	if(!CreateProcess(0, const_cast<wchar_t*>(cmd.c_str()), 0, 0, FALSE, 0, 0, 0, &si, &pi)) {
		throw CryptoException(Util::translateError(::GetLastError()));
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
#else
	string cmd = "openssl genrsa -out \"" + Text::utf8ToAcp(SETTING(TLS_PRIVATE_KEY_FILE)) + "\" 2048";
	if (system(cmd.c_str()) == -1) {
		throw CryptoException("Failed to spawn process: openssl");
	}

	cmd = "openssl req -x509 -new -batch -days 3650 -key \"" + Text::utf8ToAcp(SETTING(TLS_PRIVATE_KEY_FILE)) +
		"\" -out \"" + Text::utf8ToAcp(SETTING(TLS_CERTIFICATE_FILE)) + "\" -subj \"/CN=" +
		Text::utf8ToAcp(ClientManager::getInstance()->getMyCID().toBase32()) + "\"";
	if (system(cmd.c_str()) == -1) {
		throw CryptoException("Failed to spawn process: openssl");
	}
#endif
}

void CryptoManager::loadCertificates() throw() {
	if(!BOOLSETTING(USE_TLS))
		return;

	SSL_CTX_set_verify(serverContext, SSL_VERIFY_NONE, 0);
	SSL_CTX_set_verify(clientContext, SSL_VERIFY_NONE, 0);
	SSL_CTX_set_verify(clientVerContext, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, 0);
	SSL_CTX_set_verify(serverVerContext, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, 0);

	const string& cert = SETTING(TLS_CERTIFICATE_FILE);
	const string& key = SETTING(TLS_PRIVATE_KEY_FILE);

	if(cert.empty() || key.empty()) {
		LogManager::getInstance()->message(STRING(NO_CERTIFICATE_FILE_SET));
		return;
	}

	if(File::getSize(cert) == -1 || File::getSize(key) == -1) {
		// Try to generate them...
		try {
			generateCertificate();
		} catch(const CryptoException& e) {
			LogManager::getInstance()->message(STRING(CERTIFICATE_GENERATION_FAILED) + e.getError());
		}
	}

	if(SSL_CTX_use_certificate_file(serverContext, SETTING(TLS_CERTIFICATE_FILE).c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS) {
		LogManager::getInstance()->message(STRING(FAILED_TO_LOAD_CERTIFICATE));
		return;
	}
	if(SSL_CTX_use_certificate_file(clientContext, SETTING(TLS_CERTIFICATE_FILE).c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS) {
		LogManager::getInstance()->message(STRING(FAILED_TO_LOAD_CERTIFICATE));
		return;
	}

	if(SSL_CTX_use_certificate_file(serverVerContext, SETTING(TLS_CERTIFICATE_FILE).c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS) {
		LogManager::getInstance()->message(STRING(FAILED_TO_LOAD_CERTIFICATE));
		return;
	}
	if(SSL_CTX_use_certificate_file(clientVerContext, SETTING(TLS_CERTIFICATE_FILE).c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS) {
		LogManager::getInstance()->message(STRING(FAILED_TO_LOAD_CERTIFICATE));
		return;
	}

	if(SSL_CTX_use_PrivateKey_file(serverContext, SETTING(TLS_PRIVATE_KEY_FILE).c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS) {
		LogManager::getInstance()->message(STRING(FAILED_TO_LOAD_PRIVATE_KEY));
		return;
	}
	if(SSL_CTX_use_PrivateKey_file(clientContext, SETTING(TLS_PRIVATE_KEY_FILE).c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS) {
		LogManager::getInstance()->message(STRING(FAILED_TO_LOAD_PRIVATE_KEY));
		return;
	}

	if(SSL_CTX_use_PrivateKey_file(serverVerContext, SETTING(TLS_PRIVATE_KEY_FILE).c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS) {
		LogManager::getInstance()->message(STRING(FAILED_TO_LOAD_PRIVATE_KEY));
		return;
	}
	if(SSL_CTX_use_PrivateKey_file(clientVerContext, SETTING(TLS_PRIVATE_KEY_FILE).c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS) {
		LogManager::getInstance()->message(STRING(FAILED_TO_LOAD_PRIVATE_KEY));
		return;
	}

	StringList certs = File::findFiles(SETTING(TLS_TRUSTED_CERTIFICATES_PATH), "*.pem");
	StringList certs2 = File::findFiles(SETTING(TLS_TRUSTED_CERTIFICATES_PATH), "*.crt");
	certs.insert(certs.end(), certs2.begin(), certs2.end());

	for(StringIter i = certs.begin(); i != certs.end(); ++i) {
		if(
			SSL_CTX_load_verify_locations(clientContext, i->c_str(), NULL) != SSL_SUCCESS ||
			SSL_CTX_load_verify_locations(clientVerContext, i->c_str(), NULL) != SSL_SUCCESS ||
			SSL_CTX_load_verify_locations(serverContext, i->c_str(), NULL) != SSL_SUCCESS ||
			SSL_CTX_load_verify_locations(serverVerContext, i->c_str(), NULL) != SSL_SUCCESS
		) {
			LogManager::getInstance()->message("Failed to load trusted certificate from " + *i);
		}
	}

	certsLoaded = true;
}


SSLSocket* CryptoManager::getClientSocket(bool allowUntrusted) throw(SocketException) {
	return new SSLSocket(allowUntrusted ? clientContext : clientVerContext);
}
SSLSocket* CryptoManager::getServerSocket(bool allowUntrusted) throw(SocketException) {
	return new SSLSocket(allowUntrusted ? serverContext : serverVerContext);
}


void CryptoManager::decodeBZ2(const uint8_t* is, size_t sz, string& os) throw (CryptoException) {
	bz_stream bs = { 0 };

	if(BZ2_bzDecompressInit(&bs, 0, 0) != BZ_OK)
		throw(CryptoException(STRING(DECOMPRESSION_ERROR)));

	// We assume that the files aren't compressed more than 2:1...if they are it'll work anyway,
	// but we'll have to do multiple passes...
	size_t bufsize = 2*sz;
	AutoArray<char> buf(bufsize);

	bs.avail_in = sz;
	bs.avail_out = bufsize;
	bs.next_in = (char*)(const_cast<uint8_t*>(is));
	bs.next_out = buf;

	int err;

	os.clear();

	while((err = BZ2_bzDecompress(&bs)) == BZ_OK) {
		if (bs.avail_in == 0 && bs.avail_out > 0) { // error: BZ_UNEXPECTED_EOF
			BZ2_bzDecompressEnd(&bs);
			throw CryptoException(STRING(DECOMPRESSION_ERROR));
		}
		os.append(buf, bufsize-bs.avail_out);
		bs.avail_out = bufsize;
		bs.next_out = buf;
	}

	if(err == BZ_STREAM_END)
		os.append(buf, bufsize-bs.avail_out);

	BZ2_bzDecompressEnd(&bs);

	if(err < 0) {
		// This was a real error
		throw CryptoException(STRING(DECOMPRESSION_ERROR));
	}
}

string CryptoManager::keySubst(const uint8_t* aKey, size_t len, size_t n) {
	AutoArray<uint8_t> temp(len + n * 10);

	size_t j=0;

	for(size_t i = 0; i<len; i++) {
		if(isExtra(aKey[i])) {
			temp[j++] = '/'; temp[j++] = '%'; temp[j++] = 'D';
			temp[j++] = 'C'; temp[j++] = 'N';
			switch(aKey[i]) {
			case 0: temp[j++] = '0'; temp[j++] = '0'; temp[j++] = '0'; break;
			case 5: temp[j++] = '0'; temp[j++] = '0'; temp[j++] = '5'; break;
			case 36: temp[j++] = '0'; temp[j++] = '3'; temp[j++] = '6'; break;
			case 96: temp[j++] = '0'; temp[j++] = '9'; temp[j++] = '6'; break;
			case 124: temp[j++] = '1'; temp[j++] = '2'; temp[j++] = '4'; break;
			case 126: temp[j++] = '1'; temp[j++] = '2'; temp[j++] = '6'; break;
			}
			temp[j++] = '%'; temp[j++] = '/';
		} else {
			temp[j++] = aKey[i];
		}
	}
	return string((char*)(uint8_t*)temp, j);
}

string CryptoManager::makeKey(const string& aLock) {
	if(aLock.size() < 3)
		return Util::emptyString;

	AutoArray<uint8_t> temp(aLock.length());
	uint8_t v1;
	size_t extra=0;

	v1 = (uint8_t)(aLock[0]^5);
	v1 = (uint8_t)(((v1 >> 4) | (v1 << 4)) & 0xff);
	temp[0] = v1;

	string::size_type i;

	for(i = 1; i<aLock.length(); i++) {
		v1 = (uint8_t)(aLock[i]^aLock[i-1]);
		v1 = (uint8_t)(((v1 >> 4) | (v1 << 4))&0xff);
		temp[i] = v1;
		if(isExtra(temp[i]))
			extra++;
	}

	temp[0] = (uint8_t)(temp[0] ^ temp[aLock.length()-1]);

	if(isExtra(temp[0])) {
		extra++;
	}

	return keySubst(temp, aLock.length(), extra);
}

