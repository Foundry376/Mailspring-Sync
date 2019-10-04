/*
crypto.c : functions common to all the crypto backends
Copyright (C) 2017  Belledonne Communications SARL

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <bctoolbox/crypto.h>

/*****************************************************************************/
/***** Cleaning                                                          *****/
/*****************************************************************************/

/**
 * @brief force a buffer value to zero in a way that shall prevent the compiler from optimizing it out
 *
 * @param[in/out]	buffer	the buffer to be cleared
 * @param[in]		size	buffer size
 */
void bctbx_clean(void *buffer, size_t size) {
	//TODO: use memset_s or SecureZeroMemory when available
	volatile uint8_t *p = buffer;
	while(size--) *p++ = 0;
}

/**
 * @brief encrypt the file in input buffer for linphone encrypted file transfer
 *
 * @param[in/out]	cryptoContext	a context already initialized using bctbx_aes_gcm_context_new
 * @param[in]		key		encryption key
 * @param[in]		length	buffer size
 * @param[in]		plain	buffer holding the input data
 * @param[out]		cipher	buffer to store the output data
 */
int bctbx_aes_gcm_encryptFile(void **cryptoContext, unsigned char *key, size_t length, char *plain, char *cipher) {
	bctbx_aes_gcm_context_t *gcmContext;

	if (key == NULL) return -1;

	if (*cryptoContext == NULL) { /* first call to the function, allocate a crypto context and initialise it */
		/* key contains 192bits of key || 64 bits of Initialisation Vector, no additional data */
		gcmContext = bctbx_aes_gcm_context_new(key, 24, NULL, 0, key+24, 8, BCTBX_GCM_ENCRYPT);
		*cryptoContext = gcmContext;
	} else { /* this is not the first call, get the context */
		gcmContext = (bctbx_aes_gcm_context_t *)*cryptoContext;
	}

	if (length != 0) {
		bctbx_aes_gcm_process_chunk(gcmContext, (const uint8_t *)plain, length, (uint8_t *)cipher);
	} else { /* length is 0, finish the stream, no tag to be generated */
		bctbx_aes_gcm_finish(gcmContext, NULL, 0);
		*cryptoContext = NULL;
	}

	return 0;
}

/**
 * @brief decrypt the file in input buffer for linphone encrypted file transfer
 *
 * @param[in/out]	cryptoContext	a context already initialized using bctbx_aes_gcm_context_new
 * @param[in]		key		encryption key
 * @param[in]		length	buffer size
 * @param[out]		plain	buffer holding the output data
 * @param[int]		cipher	buffer to store the input data
 */
int bctbx_aes_gcm_decryptFile(void **cryptoContext, unsigned char *key, size_t length, char *plain, char *cipher) {
	bctbx_aes_gcm_context_t *gcmContext;

	if (key == NULL) return -1;

	if (*cryptoContext == NULL) { /* first call to the function, allocate a crypto context and initialise it */
		/* key contains 192bits of key || 64 bits of Initialisation Vector, no additional data */
		gcmContext = bctbx_aes_gcm_context_new(key, 24, NULL, 0, key+24, 8, BCTBX_GCM_DECRYPT);
		*cryptoContext = gcmContext;
	} else { /* this is not the first call, get the context */
		gcmContext = (bctbx_aes_gcm_context_t *)*cryptoContext;
	}

	if (length != 0) {
		bctbx_aes_gcm_process_chunk(gcmContext, (const unsigned char *)cipher, length, (unsigned char *)plain);
	} else { /* lenght is 0, finish the stream */
		bctbx_aes_gcm_finish(gcmContext, NULL, 0);
		*cryptoContext = NULL;
	}

	return 0;
}
