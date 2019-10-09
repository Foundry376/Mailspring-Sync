/*
 * charconv.h
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

#ifndef BCTBX_CHARCONV_H
#define BCTBX_CHARCONV_H

#include "bctoolbox/port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the default encoding for the application.
 *
 * @param[in] encoding default encoding, "locale" to set it to the system's locale
 */
BCTBX_PUBLIC void bctbx_set_default_encoding (const char *encoding);

/**
 * @brief Return the default encoding for the application.
 *
 * @return a pointer to a null-terminated string containing the default encoding.
 */
BCTBX_PUBLIC const char *bctbx_get_default_encoding ();

/**
 * @brief Convert the given string from system locale to UTF8.
 *
 * @param[in] str string to convert
 *
 * @return a pointer to a null-terminated string containing the converted string. This buffer must then be freed
 * by caller. NULL on failure.
 */
BCTBX_PUBLIC char *bctbx_locale_to_utf8 (const char *str);

/**
 * @brief Convert the given string from UTF8 to system locale.
 *
 * @param[in] str string to convert
 *
 * @return a pointer to a null-terminated string containing the converted string. This buffer must then be freed
 * by caller. NULL on failure.
 */
BCTBX_PUBLIC char *bctbx_utf8_to_locale (const char *str);

/**
 * @brief Convert the given string.
 *
 * @param[in] str string to convert
 * @param[in] encoding charset of the string
 *
 * @return a pointer to a null-terminated string containing the converted string. This buffer must then be freed
 * by caller. NULL on failure.
 *
 * @note If encoding is equal to "locale" then it will use the system's locale
 * @note If encoding is UTF-8 then it returns a copy of str
 */
BCTBX_PUBLIC char *bctbx_convert_any_to_utf8 (const char *str, const char *encoding);

#ifdef __cplusplus
}
#endif

#endif /* BCTBX_CHARCONV_H */

