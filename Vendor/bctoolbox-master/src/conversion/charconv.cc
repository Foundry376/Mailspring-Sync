/*
 * charconv.cc
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

#include <errno.h>
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#include <string.h>

#include "bctoolbox/charconv.h"
#include "bctoolbox/logging.h"
#include "bctoolbox/port.h"

static char *convert_from_to (const char *str, const char *from, const char *to) {
	if (!from || !to)
		return NULL;

	if (strcasecmp(from, to) == 0)
		return bctbx_strdup(str);

	char *in_buf = (char *) str;
	char *out_buf, *ptr;
	size_t in_left = strlen(str) + 1;
	size_t out_left = in_left + in_left/10; // leave a marge of 10%
	iconv_t cd;

	setlocale(LC_CTYPE, "");

	const char* r_from = strcasecmp("locale", from) == 0 ? nl_langinfo(CODESET) : from;
	const char* r_to = strcasecmp("locale", to) == 0 ? nl_langinfo(CODESET) : to;

	if (strcasecmp(r_from, r_to) == 0) {
		return bctbx_strdup(str);
	}

	cd = iconv_open(r_to, r_from);

	if (cd != (iconv_t)-1) {
		size_t ret;
		size_t out_len = out_left;

		out_buf = (char *) bctbx_malloc(out_left);
		ptr = out_buf; // Keep a pointer to the beginning of this buffer to be able to realloc

		ret = iconv(cd, &in_buf, &in_left, &out_buf, &out_left);
		while (ret == (size_t)-1 && errno == E2BIG) {
			ptr = (char *) bctbx_realloc(ptr, out_len*2);
			out_left = out_len;
			out_buf = ptr + out_left;
			out_len *= 2;

			ret = iconv(cd, &in_buf, &in_left, &out_buf, &out_left);
		}
		iconv_close(cd);

		if (ret == (size_t)-1 && errno != E2BIG) {
			bctbx_error("Error while converting a string from '%s' to '%s': %s", from, to, strerror(errno));
			bctbx_free(ptr);

			return bctbx_strdup(str);
		}
	} else {
		bctbx_error("Unable to open iconv content descriptor from '%s' to '%s': %s", from, to, strerror(errno));

		return bctbx_strdup(str);
	}

	return ptr;
}

char *bctbx_locale_to_utf8 (const char *str) {
	const char *default_encoding = bctbx_get_default_encoding();

	if (!strcmp(default_encoding, "UTF-8"))
		return bctbx_strdup(str);

	return convert_from_to(str, default_encoding, "UTF-8");
}

char *bctbx_utf8_to_locale (const char *str) {
	const char *default_encoding = bctbx_get_default_encoding();

	if (!strcmp(default_encoding, "UTF-8"))
		return bctbx_strdup(str);

	return convert_from_to(str, "UTF-8", default_encoding);
}

char *bctbx_convert_any_to_utf8 (const char *str, const char *encoding) {
	return convert_from_to(str, encoding, "UTF-8");
}
