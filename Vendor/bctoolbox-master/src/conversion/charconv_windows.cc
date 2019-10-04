/*
 * charconv_windows.cc
 * Copyright (C) 2010-2018 Belledonne Communications SARL
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <algorithm>
#include <unordered_map>

#include "bctoolbox/charconv.h"
#include "bctoolbox/logging.h"
#include "bctoolbox/port.h"

static std::unordered_map <std::string, UINT> windowsCharset {
	{ "LOCALE", CP_ACP },
	{ "IBM037", 037 },
	{ "IBM437", 437 },
	{ "IBM500", 500 },
	{ "ASMO-708", 708 },
	{ "IBM775", 775 },
	{ "IBM850", 850 },
	{ "IBM852", 852 },
	{ "IBM855", 855 },
	{ "IBM857", 857 },
	{ "IBM860", 860 },
	{ "IBM861", 861 },
	{ "IBM863", 863 },
	{ "IBM864", 864 },
	{ "IBM865", 865 },
	{ "CP866", 866 },
	{ "IBM869", 869 },
	{ "IBM870", 870 },
	{ "WINDOWS-874", 874 },
	{ "CP875", 875 },
	{ "SHIFT_JIS", 932 },
	{ "GB2312", 936 },
	{ "BIG5", 950 },
	{ "IBM1026", 1026 },
	{ "UTF-16", 1200 },
	{ "WINDOWS-1250", 1250 },
	{ "WINDOWS-1251", 1251 },
	{ "WINDOWS-1252", 1252 },
	{ "WINDOWS-1253", 1253 },
	{ "WINDOWS-1254", 1254 },
	{ "WINDOWS-1255", 1255 },
	{ "WINDOWS-1256", 1256 },
	{ "WINDOWS-1257", 1257 },
	{ "WINDOWS-1258", 1258 },
	{ "JOHAB", 1361 },
	{ "MACINTOSH", 10000 },
	{ "UTF-32", 12000 },
	{ "UTF-32BE", 12001 },
	{ "US-ASCII", 20127 },
	{ "IBM273", 20273 },
	{ "IBM277", 20277 },
	{ "IBM278", 20278 },
	{ "IBM280", 20280 },
	{ "IBM284", 20284 },
	{ "IBM285", 20285 },
	{ "IBM290", 20290 },
	{ "IBM297", 20297 },
	{ "IBM420", 20420 },
	{ "IBM423", 20423 },
	{ "IBM424", 20424 },
	{ "KOI8-R", 20866 },
	{ "IBM871", 20871 },
	{ "IBM880", 20880 },
	{ "IBM905", 20905 },
	{ "EUC-JP", 20932 },
	{ "CP1025", 21025 },
	{ "KOI8-U", 21866 },
	{ "ISO-8859-1", 28591 },
	{ "ISO-8859-2", 28592 },
	{ "ISO-8859-3", 28593 },
	{ "ISO-8859-4", 28594 },
	{ "ISO-8859-5", 28595 },
	{ "ISO-8859-6", 28596 },
	{ "ISO-8859-7", 28597 },
	{ "ISO-8859-8", 28598 },
	{ "ISO-8859-9", 28599 },
	{ "ISO-8859-13", 28603 },
	{ "ISO-8859-15", 28605 },
	{ "ISO-2022-JP", 50222 },
	{ "CSISO2022JP", 50221 },
	{ "ISO-2022-KR", 50225 },
	{ "EUC-JP", 51932 },
	{ "EUC-CN", 51936 },
	{ "EUC-KR", 51949 },
	{ "GB18030", 54936 },
	{ "UTF-7", 65000 },
	{ "UTF-8", 65001 }
};

static std::string stringToUpper (const std::string &str) {
	std::string result(str.size(), ' ');
	std::transform(str.cbegin(), str.cend(), result.begin(), ::toupper);
	return result;
}

static char *convertFromTo (const char *str, const char *from, const char *to) {
	if (!from || !to)
		return NULL;

	if (strcasecmp(from, to) == 0)
		return bctbx_strdup(str);

	char* convertedStr;
	int nChar, nbByte;
	LPWSTR wideStr;
	UINT rFrom, rTo;

	try {
		rFrom = windowsCharset.at(stringToUpper(std::string(from)));
		rTo = windowsCharset.at(stringToUpper(std::string(to)));
	}
	catch (const std::out_of_range&) {
		bctbx_error("Error while converting a string from '%s' to '%s': unknown charset", from, to);
		return NULL;
	}

	nChar = MultiByteToWideChar(rFrom, 0, str, -1, NULL, 0);
	if (nChar == 0) return NULL;
	wideStr = (LPWSTR) bctbx_malloc(nChar*sizeof(wideStr[0]));
	if (wideStr == NULL) return NULL;
	nChar = MultiByteToWideChar(rFrom, 0, str, -1, wideStr, nChar);
	if (nChar == 0) {
		bctbx_free(wideStr);
		wideStr = 0;
	}

	nbByte = WideCharToMultiByte(rTo, 0, wideStr, -1, 0, 0, 0, 0);
	if (nbByte == 0) return NULL;
	convertedStr = (char *) bctbx_malloc(nbByte);
	if (convertedStr == NULL) return NULL;
	nbByte = WideCharToMultiByte(rTo, 0, wideStr, -1, convertedStr, nbByte, 0, 0);
	if (nbByte == 0) {
		bctbx_free(convertedStr);
		convertedStr = 0;
	}
	bctbx_free(wideStr);
	return convertedStr;
}

char *bctbx_locale_to_utf8 (const char *str) {
	const char *defaultEncoding = bctbx_get_default_encoding();

	if (!strcmp(defaultEncoding, "UTF-8"))
		return bctbx_strdup(str);

	return convertFromTo(str, defaultEncoding, "UTF-8");
}

char *bctbx_utf8_to_locale (const char *str) {
	const char *defaultEncoding = bctbx_get_default_encoding();

	if (!strcmp(defaultEncoding, "UTF-8"))
		return bctbx_strdup(str);

	return convertFromTo(str, "UTF-8", defaultEncoding);
}

char *bctbx_convert_any_to_utf8 (const char *str, const char *encoding) {
	return convertFromTo(str, encoding, "UTF-8");
}
