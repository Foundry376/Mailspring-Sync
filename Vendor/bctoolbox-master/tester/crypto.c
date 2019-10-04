/*
	bctoolbox
    Copyright (C) 2017  Belledonne Communications SARL


    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include "bctoolbox_tester.h"
#include "bctoolbox/crypto.h"
#ifdef HAVE_MBEDTLS
/* used to cross test ECDH25519 */
#include "mbedtls/ecdh.h"
#endif /* HAVE_MBEDTLS */

static void DHM(void) {

	int i;
	bctbx_DHMContext_t *alice,*bob;
	bctbx_rng_context_t *RNG;
	uint8_t availableAlgos[2]={BCTBX_DHM_2048, BCTBX_DHM_3072};
	uint8_t availableAlgosNb=2;

	/* Init the RNG */
	RNG = bctbx_rng_context_new();

	/* Get all the available DH algos */
	//availableAlgosNb=bctbx_getDHMAvailableAlgos(availableAlgos);

	for (i=0; i<availableAlgosNb; i++) {
		 uint8_t secretSize=0;
		 uint32_t keySize=0;

		 switch (availableAlgos[i]) {
			 case BCTBX_DHM_2048:
				secretSize = 32;
				keySize = 256;
				break;

			 case BCTBX_DHM_3072:
				secretSize = 32;
				keySize = 384;
				break;

		 }

		/* Create Alice and Bob contexts */
		alice = bctbx_CreateDHMContext(availableAlgos[i], secretSize);
		bob = bctbx_CreateDHMContext(availableAlgos[i], secretSize);

		/* Generate keys pairs */
		bctbx_DHMCreatePublic(alice, (int (*)(void *, uint8_t *, size_t))bctbx_rng_get, RNG);
		bctbx_DHMCreatePublic(bob, (int (*)(void *, uint8_t *, size_t))bctbx_rng_get, RNG);

		/* exchange public keys */
		alice->peer = (uint8_t *)malloc(keySize*sizeof(uint8_t));
		memcpy(alice->peer,bob->self,keySize);
		bob->peer = (uint8_t *)malloc(keySize*sizeof(uint8_t));
		memcpy(bob->peer,alice->self,keySize);

		/* compute shared secrets */
		bctbx_DHMComputeSecret(alice, (int (*)(void *, uint8_t *, size_t))bctbx_rng_get, RNG);
		bctbx_DHMComputeSecret(bob, (int (*)(void *, uint8_t *, size_t))bctbx_rng_get, RNG);

		/* compare the secrets */
		BC_ASSERT_TRUE(memcmp(alice->key,bob->key,keySize)==0);

		/* clear contexts */
		bctbx_DestroyDHMContext(alice);
		bctbx_DestroyDHMContext(bob);
	}

	/* clear contexts */
	bctbx_rng_context_free(RNG);
}

static void ECDH_exchange(bctbx_ECDHContext_t *alice, bctbx_ECDHContext_t *bob) {
	/* exchange public keys */
	bctbx_ECDHSetPeerPublicKey(alice, bob->selfPublic, alice->pointCoordinateLength);
	bctbx_ECDHSetPeerPublicKey(bob, alice->selfPublic, bob->pointCoordinateLength);

	/* compute shared secrets */
	bctbx_ECDHComputeSecret(alice, NULL, NULL);
	bctbx_ECDHComputeSecret(bob, NULL, NULL);

	/* compare the secrets */
	BC_ASSERT_TRUE(memcmp(alice->sharedSecret, bob->sharedSecret, alice->pointCoordinateLength)==0);
}

static void ECDH(void) {
	if (!bctbx_crypto_have_ecc()) {
		bctbx_warning("test skipped as we don't have Elliptic Curve Cryptography in bctoolbox");
		return;
	}

	/* Patterns */
	uint8_t ECDHpattern_X25519_alicePrivate[] = {0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45, 0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0, 0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a};
	uint8_t ECDHpattern_X25519_alicePublic[] = {0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b, 0x7d, 0xdc, 0xb4, 0x3e, 0xf7, 0x5a, 0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38, 0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a};
	uint8_t ECDHpattern_X25519_bobPrivate[] = {0x5d, 0xab, 0x08, 0x7e, 0x62, 0x4a, 0x8a, 0x4b, 0x79, 0xe1, 0x7f, 0x8b, 0x83, 0x80, 0x0e, 0xe6, 0x6f, 0x3b, 0xb1, 0x29, 0x26, 0x18, 0xb6, 0xfd, 0x1c, 0x2f, 0x8b, 0x27, 0xff, 0x88, 0xe0, 0xeb};
	uint8_t ECDHpattern_X25519_bobPublic[] = {0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4, 0xd3, 0x5b, 0x61, 0xc2, 0xec, 0xe4, 0x35, 0x37, 0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78, 0x67, 0x4d, 0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f};
	uint8_t ECDHpattern_X25519_sharedSecret[] = {0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1, 0x72, 0x8e, 0x3b, 0xf4, 0x80, 0x35, 0x0f, 0x25, 0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1, 0x9e, 0x33, 0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42};
	uint8_t alicePublic_libSignalPattern[] = { 0x1b, 0xb7, 0x59, 0x66, 0xf2, 0xe9, 0x3a, 0x36, 0x91, 0xdf, 0xff, 0x94, 0x2b, 0xb2, 0xa4, 0x66, 0xa1, 0xc0, 0x8b, 0x8d, 0x78, 0xca, 0x3f, 0x4d, 0x6d, 0xf8, 0xb8, 0xbf, 0xa2, 0xe4, 0xee, 0x28};
	uint8_t alicePrivate_libSignalPattern[] = { 0xc8, 0x06, 0x43, 0x9d, 0xc9, 0xd2, 0xc4, 0x76, 0xff, 0xed, 0x8f, 0x25, 0x80, 0xc0, 0x88, 0x8d, 0x58, 0xab, 0x40, 0x6b, 0xf7, 0xae, 0x36, 0x98, 0x87, 0x90, 0x21, 0xb9, 0x6b, 0xb4, 0xbf, 0x59};
	uint8_t bobPublic_libSignalPattern[] = { 0x65, 0x36, 0x14, 0x99, 0x3d, 0x2b, 0x15, 0xee, 0x9e, 0x5f, 0xd3, 0xd8, 0x6c, 0xe7, 0x19, 0xef, 0x4e, 0xc1, 0xda, 0xae, 0x18, 0x86, 0xa8, 0x7b, 0x3f, 0x5f, 0xa9, 0x56, 0x5a, 0x27, 0xa2, 0x2f};
	uint8_t bobPrivate_libSignalPattern[] = { 0xb0, 0x3b, 0x34, 0xc3, 0x3a, 0x1c, 0x44, 0xf2, 0x25, 0xb6, 0x62, 0xd2, 0xbf, 0x48, 0x59, 0xb8, 0x13, 0x54, 0x11, 0xfa, 0x7b, 0x03, 0x86, 0xd4, 0x5f, 0xb7, 0x5d, 0xc5, 0xb9, 0x1b, 0x44, 0x66};
	uint8_t shared_libSignalPattern[] = { 0x32, 0x5f, 0x23, 0x93, 0x28, 0x94, 0x1c, 0xed, 0x6e, 0x67, 0x3b, 0x86, 0xba, 0x41, 0x01, 0x74, 0x48, 0xe9, 0x9b, 0x64, 0x9a, 0x9c, 0x38, 0x06, 0xc1, 0xdd, 0x7c, 0xa4, 0xc4, 0x77, 0xe6, 0x29};
	uint8_t ECDHpattern_X448_alicePrivate[] = {0x9a, 0x8f, 0x49, 0x25, 0xd1, 0x51, 0x9f, 0x57, 0x75, 0xcf, 0x46, 0xb0, 0x4b, 0x58, 0x00, 0xd4, 0xee, 0x9e, 0xe8, 0xba, 0xe8, 0xbc, 0x55, 0x65, 0xd4, 0x98, 0xc2, 0x8d, 0xd9, 0xc9, 0xba, 0xf5, 0x74, 0xa9, 0x41, 0x97, 0x44, 0x89, 0x73, 0x91, 0x00, 0x63, 0x82, 0xa6, 0xf1, 0x27, 0xab, 0x1d, 0x9a, 0xc2, 0xd8, 0xc0, 0xa5, 0x98, 0x72, 0x6b};
	uint8_t ECDHpattern_X448_alicePublic[] = {0x9b, 0x08, 0xf7, 0xcc, 0x31, 0xb7, 0xe3, 0xe6, 0x7d, 0x22, 0xd5, 0xae, 0xa1, 0x21, 0x07, 0x4a, 0x27, 0x3b, 0xd2, 0xb8, 0x3d, 0xe0, 0x9c, 0x63, 0xfa, 0xa7, 0x3d, 0x2c, 0x22, 0xc5, 0xd9, 0xbb, 0xc8, 0x36, 0x64, 0x72, 0x41, 0xd9, 0x53, 0xd4, 0x0c, 0x5b, 0x12, 0xda, 0x88, 0x12, 0x0d, 0x53, 0x17, 0x7f, 0x80, 0xe5, 0x32, 0xc4, 0x1f, 0xa0};
	uint8_t ECDHpattern_X448_bobPrivate[] = {0x1c, 0x30, 0x6a, 0x7a, 0xc2, 0xa0, 0xe2, 0xe0, 0x99, 0x0b, 0x29, 0x44, 0x70, 0xcb, 0xa3, 0x39, 0xe6, 0x45, 0x37, 0x72, 0xb0, 0x75, 0x81, 0x1d, 0x8f, 0xad, 0x0d, 0x1d, 0x69, 0x27, 0xc1, 0x20, 0xbb, 0x5e, 0xe8, 0x97, 0x2b, 0x0d, 0x3e, 0x21, 0x37, 0x4c, 0x9c, 0x92, 0x1b, 0x09, 0xd1, 0xb0, 0x36, 0x6f, 0x10, 0xb6, 0x51, 0x73, 0x99, 0x2d};
	uint8_t ECDHpattern_X448_bobPublic[] = {0x3e, 0xb7, 0xa8, 0x29, 0xb0, 0xcd, 0x20, 0xf5, 0xbc, 0xfc, 0x0b, 0x59, 0x9b, 0x6f, 0xec, 0xcf, 0x6d, 0xa4, 0x62, 0x71, 0x07, 0xbd, 0xb0, 0xd4, 0xf3, 0x45, 0xb4, 0x30, 0x27, 0xd8, 0xb9, 0x72, 0xfc, 0x3e, 0x34, 0xfb, 0x42, 0x32, 0xa1, 0x3c, 0xa7, 0x06, 0xdc, 0xb5, 0x7a, 0xec, 0x3d, 0xae, 0x07, 0xbd, 0xc1, 0xc6, 0x7b, 0xf3, 0x36, 0x09};
	uint8_t ECDHpattern_X448_sharedSecret[] = {0x07, 0xff, 0xf4, 0x18, 0x1a, 0xc6, 0xcc, 0x95, 0xec, 0x1c, 0x16, 0xa9, 0x4a, 0x0f, 0x74, 0xd1, 0x2d, 0xa2, 0x32, 0xce, 0x40, 0xa7, 0x75, 0x52, 0x28, 0x1d, 0x28, 0x2b, 0xb6, 0x0c, 0x0b, 0x56, 0xfd, 0x24, 0x64, 0xc3, 0x35, 0x54, 0x39, 0x36, 0x52, 0x1c, 0x24, 0x40, 0x30, 0x85, 0xd5, 0x9a, 0x44, 0x9a, 0x50, 0x37, 0x51, 0x4a, 0x87, 0x9d};

	int i;
	bctbx_ECDHContext_t *alice,*bob;
	bctbx_rng_context_t *RNG;
	uint8_t availableAlgos[2]={BCTBX_ECDH_X25519, BCTBX_ECDH_X448};
	uint8_t availableAlgosNb=2;

	/********************************************************************/
	/*** Do a random generation and exchange with all available algos ***/
	/********************************************************************/
	/* Init the RNG */
	RNG = bctbx_rng_context_new();

	for (i=0; i<availableAlgosNb; i++) {

		/* Create Alice and Bob contexts */
		alice = bctbx_CreateECDHContext(availableAlgos[i]);
		bob = bctbx_CreateECDHContext(availableAlgos[i]);

		/* Generate keys pairs */
		bctbx_ECDHCreateKeyPair(alice, (int (*)(void *, uint8_t *, size_t))bctbx_rng_get, RNG);
		bctbx_ECDHCreateKeyPair(bob, (int (*)(void *, uint8_t *, size_t))bctbx_rng_get, RNG);

		/* do the exchange, it will check secrets are equals */
		ECDH_exchange(alice, bob);

		/* clear contexts */
		bctbx_DestroyECDHContext(alice);
		bctbx_DestroyECDHContext(bob);
	}

	/* clear contexts */
	bctbx_rng_context_free(RNG);

	/********************************************************************/
	/*** Run an exchange using patterns from RFC7748                    */
	/********************************************************************/
	/*** Run it on the X25519 patterns ***/
	/* set contexts */
	alice = bctbx_CreateECDHContext(BCTBX_ECDH_X25519);
	bob = bctbx_CreateECDHContext(BCTBX_ECDH_X25519);

	/* Set private and public value */
	bctbx_ECDHSetSecretKey(alice, ECDHpattern_X25519_alicePrivate, 32);
	bctbx_ECDHSetSelfPublicKey(alice, ECDHpattern_X25519_alicePublic, 32);
	bctbx_ECDHSetSecretKey(bob, ECDHpattern_X25519_bobPrivate, 32);
	bctbx_ECDHSetSelfPublicKey(bob, ECDHpattern_X25519_bobPublic, 32);

	/* Perform the key exchange and compute shared secret, it will check shared secrets are matching */
	ECDH_exchange(alice, bob);

	/* check shared secret according to RFC7748 patterns */
	BC_ASSERT_TRUE(memcmp(alice->sharedSecret, ECDHpattern_X25519_sharedSecret, 32)==0);

	/* clear contexts */
	bctbx_DestroyECDHContext(alice);
	bctbx_DestroyECDHContext(bob);

	/********************************************************************/
	/*** Run an key derivation and exchange using patterns from RFC7748 */
	/********************************************************************/
	/*** Run it on the X25519 patterns ***/
	/* set contexts */
	alice = bctbx_CreateECDHContext(BCTBX_ECDH_X25519);
	bob = bctbx_CreateECDHContext(BCTBX_ECDH_X25519);

	/* Set private and derive the public value */
	bctbx_ECDHSetSecretKey(alice, ECDHpattern_X25519_alicePrivate, 32);
	bctbx_ECDHDerivePublicKey(alice);
	bctbx_ECDHSetSecretKey(bob, ECDHpattern_X25519_bobPrivate, 32);
	bctbx_ECDHDerivePublicKey(bob);

	/* check the public value according to RFC7748 patterns */
	BC_ASSERT_TRUE(memcmp(alice->selfPublic, ECDHpattern_X25519_alicePublic, 32)==0);
	BC_ASSERT_TRUE(memcmp(bob->selfPublic, ECDHpattern_X25519_bobPublic, 32)==0);

	/* Perform the key exchange and compute shared secret, it will check shared secrets are matching */
	ECDH_exchange(alice, bob);

	/* check shared secret according to RFC7748 patterns */
	BC_ASSERT_TRUE(memcmp(alice->sharedSecret, ECDHpattern_X25519_sharedSecret, 32)==0);

	/* clear contexts */
	bctbx_DestroyECDHContext(alice);
	bctbx_DestroyECDHContext(bob);
	/**********************************************************************/
	/*** Run an key derivation and exchange using patterns from libsignal */
	/**********************************************************************/
	/* Do another one using pattern retrieved from libsignal tests */
	/* set contexts */
	alice = bctbx_CreateECDHContext(BCTBX_ECDH_X25519);
	bob = bctbx_CreateECDHContext(BCTBX_ECDH_X25519);

	/* Set private and derive the public value */
	bctbx_ECDHSetSecretKey(alice, alicePrivate_libSignalPattern, 32);
	bctbx_ECDHDerivePublicKey(alice);
	bctbx_ECDHSetSecretKey(bob, bobPrivate_libSignalPattern, 32);
	bctbx_ECDHDerivePublicKey(bob);

	/* check the public value according to libsignal patterns */
	BC_ASSERT_TRUE(memcmp(alice->selfPublic, alicePublic_libSignalPattern, 32)==0);
	BC_ASSERT_TRUE(memcmp(bob->selfPublic, bobPublic_libSignalPattern, 32)==0);

	/* Perform the key exchange and compute shared secret, it will check shared secrets are matching */
	ECDH_exchange(alice, bob);

	/* check shared secret according to libsignal patterns */
	BC_ASSERT_TRUE(memcmp(alice->sharedSecret, shared_libSignalPattern, 32)==0);

	/* clear contexts */
	bctbx_DestroyECDHContext(alice);
	bctbx_DestroyECDHContext(bob);

	/********************************************************************/
	/*** Run an exchange using patterns from RFC7748                    */
	/********************************************************************/
	/*** Run it on the X448 patterns ***/
	/* set contexts */
	alice = bctbx_CreateECDHContext(BCTBX_ECDH_X448);
	bob = bctbx_CreateECDHContext(BCTBX_ECDH_X448);

	/* Set private and derive the public value */
	bctbx_ECDHSetSecretKey(alice, ECDHpattern_X448_alicePrivate, 56);
	bctbx_ECDHSetSelfPublicKey(alice, ECDHpattern_X448_alicePublic, 56);
	bctbx_ECDHSetSecretKey(bob, ECDHpattern_X448_bobPrivate, 56);
	bctbx_ECDHSetSelfPublicKey(bob, ECDHpattern_X448_bobPublic, 56);

	/* Perform the key exchange and compute shared secret, it will check shared secrets are matching */
	ECDH_exchange(alice, bob);

	/* check shared secret according to RFC7748 patterns */
	BC_ASSERT_TRUE(memcmp(alice->sharedSecret, ECDHpattern_X448_sharedSecret, 56)==0);

	/* clear contexts */
	bctbx_DestroyECDHContext(alice);
	bctbx_DestroyECDHContext(bob);

	/********************************************************************/
	/*** Run an key derivation and exchange using patterns from RFC7748 */
	/********************************************************************/
	/*** Run it on the X448 patterns ***/
	/* set contexts */
	alice = bctbx_CreateECDHContext(BCTBX_ECDH_X448);
	bob = bctbx_CreateECDHContext(BCTBX_ECDH_X448);

	/* Set private and derive the public value */
	bctbx_ECDHSetSecretKey(alice, ECDHpattern_X448_alicePrivate, 56);
	bctbx_ECDHDerivePublicKey(alice);
	bctbx_ECDHSetSecretKey(bob, ECDHpattern_X448_bobPrivate, 56);
	bctbx_ECDHDerivePublicKey(bob);

	/* check the public value according to RFC7748 patterns */
	BC_ASSERT_TRUE(memcmp(alice->selfPublic, ECDHpattern_X448_alicePublic, 56)==0);
	BC_ASSERT_TRUE(memcmp(bob->selfPublic, ECDHpattern_X448_bobPublic, 56)==0);

	/* Perform the key exchange and compute shared secret, it will check shared secrets are matching */
	ECDH_exchange(alice, bob);

	/* check shared secret according to RFC7748 patterns */
	BC_ASSERT_TRUE(memcmp(alice->sharedSecret, ECDHpattern_X448_sharedSecret, 56)==0);

	/* clear contexts */
	bctbx_DestroyECDHContext(alice);
	bctbx_DestroyECDHContext(bob);
}

/* just check compatibility of ECDH exchange between mbedtls implementation and decaf one */
/* mbedtls works with all buffer in big endian, while rfc 7748 specify little endian encoding, so all buffer in or out of mbedtls_mpi_read/write must be reversed */
static void ECDH25519compat(void) {

#ifdef HAVE_MBEDTLS
	int i;
	bctbx_ECDHContext_t *alice=NULL;
	bctbx_rng_context_t *RNG;
	mbedtls_ecdh_context *bob=NULL;
	uint8_t tmpBuffer[32];
	uint8_t bobPublic[32];
	uint8_t bobShared[32];

	if (!bctbx_crypto_have_ecc()) {
		bctbx_warning("test skipped as we don't have Elliptic Curve Cryptography in bctoolbox");
		return;
	}


	/* Init the RNG */
	RNG = bctbx_rng_context_new();

	/* Create Alice and Bob contexts */
	alice = bctbx_CreateECDHContext(BCTBX_ECDH_X25519);

	bob=(mbedtls_ecdh_context *)bctbx_malloc(sizeof(mbedtls_ecdh_context));
	mbedtls_ecdh_init(bob);
	mbedtls_ecp_group_load(&(bob->grp), MBEDTLS_ECP_DP_CURVE25519 );

	/* Generate keys pairs */
	bctbx_ECDHCreateKeyPair(alice, (int (*)(void *, uint8_t *, size_t))bctbx_rng_get, RNG);
	mbedtls_ecdh_gen_public(&(bob->grp), &(bob->d), &(bob->Q), (int (*)(void *, unsigned char *, size_t))bctbx_rng_get, RNG);
	mbedtls_mpi_write_binary(&(bob->Q.X), tmpBuffer, 32 );
	/* tmpBuffer is in big endian, but we need it in little endian, reverse it */
	for (i=0; i<32; i++) {
		bobPublic[i]=tmpBuffer[31-i];
	}

	/* exchange public keys */
	alice->peerPublic = (uint8_t *)malloc(alice->pointCoordinateLength*sizeof(uint8_t));
	memcpy(alice->peerPublic, bobPublic, alice->pointCoordinateLength);

	/* compute shared secrets */
	bctbx_ECDHComputeSecret(alice, NULL, NULL);

	mbedtls_mpi_lset(&(bob->Qp.Z), 1);
	/* alice->selfPublic is in little endian, but mbedtls expect it in big, reverse it */
	for (i=0; i<32; i++) {
		tmpBuffer[i]=alice->selfPublic[31-i];
	}

	mbedtls_mpi_read_binary(&(bob->Qp.X), tmpBuffer, 32);
	/* generate shared secret */
	mbedtls_ecdh_compute_shared(&(bob->grp), &(bob->z), &(bob->Qp), &(bob->d),  (int (*)(void *, unsigned char *, size_t))bctbx_rng_get, RNG);
	/* copy it in the output buffer */
	mbedtls_mpi_write_binary(&(bob->z), tmpBuffer, 32);
	/* reverse it as we need it in little endian */
	for (i=0; i<32; i++) {
		bobShared[i]=tmpBuffer[31-i];
	}

	/* compare the secrets */
	BC_ASSERT_TRUE(memcmp(alice->sharedSecret, bobShared, alice->pointCoordinateLength)==0);

	/* clear contexts */
	bctbx_DestroyECDHContext(alice);
	mbedtls_ecdh_free(bob);
	free(bob);

	/* clear contexts */
	bctbx_rng_context_free(RNG);
#else /* HAVE_MBEDTLS */
	bctbx_warning("test skipped as we don't have mbedtls in bctoolbox");
#endif /* HAVE_MBEDTLS */
}

static char *importantMessage1 = "The most obvious mechanical phenomenon in electrical and magnetical experiments is the mutual action by which bodies in certain states set each other in motion while still at a sensible distance from each other. The first step, therefore, in reducing these phenomena into scientific form, is to ascertain the magnitude and direction of the force acting between the bodies, and when it is found that this force depends in a certain way upon the relative position of the bodies and on their electric or magnetic condition, it seems at first sight natural to explain the facts by assuming the existence of something either at rest or in motion in each body, constituting its electric or magnetic state, and capable of acting at a distance according to mathematical laws.In this way mathematical theories of statical electricity, of magnetism, of the mechanical action between conductors carrying currents, and of the induction of currents have been formed. In these theories the force acting between the two bodies is treated with reference only to the condition of the bodies and their relative position, and without any express consideration of the surrounding medium. These theories assume, more or less explicitly, the existence of substances the particles of which have the property of acting on one another at a distance by attraction or repulsion. The most complete development of a theory of this kind is that of M.W. Weber[1], who has made the same theory include electrostatic and electromagnetic phenomena. In doing so, however, he has found it necessary to assume that the force between two particles depends on their relative velocity, as well as on their distance. This theory, as developed by MM. W. Weber and C. Neumann[2], is exceedingly ingenious, and wonderfully comprehensive in its application to the phenomena of statical electricity, electromagnetic attractions, induction of current and diamagnetic phenomena; and it comes to us with the more authority, as it has served to guide the speculations of one who has made so great an advance in the practical part of electric science, both by introducing a consistent system of units in electrical measurement, and by actually determining electrical quantities with an accuracy hitherto unknown.";

static char *importantMessage2 = " The mechanical difficulties, however, which are involved in the assumption of particles acting at a distance with forces which depend on their velocities are such as to prevent me from considering this theory as an ultimate one though it may have been, and may yet be useful in leading to the coordination of phenomena. I have therefore preferred to seek an explanation of the fact in another direction, by supposing them to be produced by actions which go on in the surrounding medium as well as in the excited bodies, and endeavouring to explain the action between distant bodies without assuming the existence of forces capable of acting directly at sensible distances.";

static void EdDSA(void) {
	int i;
	bctbx_EDDSAContext_t *james,*world;
	bctbx_rng_context_t *RNG;
	uint8_t availableAlgos[2]={BCTBX_EDDSA_25519, BCTBX_EDDSA_448};
	uint8_t availableAlgosNb=2;
	uint8_t signature[128]; /* buffer to store the signature, must be at least twice the size of the longer point coordinate (57*2) */
	size_t signatureLength = 128;
	uint8_t context[250];

	if (!bctbx_crypto_have_ecc()) {
		bctbx_warning("test skipped as we don't have Elliptic Curve Cryptography in bctoolbox");
		return;
	}


	/* Init the RNG */
	RNG = bctbx_rng_context_new();


	for (i=0; i<availableAlgosNb; i++) {
		signatureLength = 128; /* reset buffer length */
		/* create contexts */
		james = bctbx_CreateEDDSAContext(availableAlgos[i]);
		world = bctbx_CreateEDDSAContext(availableAlgos[i]);

		/* generate a random context */
		bctbx_rng_get(RNG, context, 250);

		/* generate a private and public key for james */
		bctbx_EDDSACreateKeyPair(james,  (int (*)(void *, unsigned char *, size_t))bctbx_rng_get, RNG);

		/* james sign the important message */
		bctbx_EDDSA_sign(james, (uint8_t *)importantMessage1, strlen(importantMessage1), context, 250, signature, &signatureLength);
		BC_ASSERT_NOT_EQUAL(signatureLength, 0, int, "%d");

		/* world get james public key */
		bctbx_EDDSA_setPublicKey(world, james->publicKey, james->pointCoordinateLength);

		/* world verifies that the important message was signed by james */
		BC_ASSERT_EQUAL(bctbx_EDDSA_verify(world, (uint8_t *)importantMessage1, strlen(importantMessage1), context, 250, signature, signatureLength), BCTBX_VERIFY_SUCCESS, int, "%d");

		/* twist the signature to get it wrong and verify again, it shall fail */
		signature[0] ^=0xFF;
		BC_ASSERT_EQUAL(bctbx_EDDSA_verify(world, (uint8_t *)importantMessage1, strlen(importantMessage1), context, 250, signature, signatureLength), BCTBX_VERIFY_FAILED, int, "%d");

		/* twist the context to get it wrong and verify again, it shall fail */
		signature[0] ^=0xFF;
		context[0] ^=0xFF;
		BC_ASSERT_EQUAL(bctbx_EDDSA_verify(world, (uint8_t *)importantMessage1, strlen(importantMessage1), context, 250, signature, signatureLength), BCTBX_VERIFY_FAILED, int, "%d");

		/* cleaning */
		bctbx_DestroyEDDSAContext(james);
		bctbx_DestroyEDDSAContext(world);
	}

	bctbx_rng_context_free(RNG);
}

static void ed25519_to_x25519_keyconversion(void) {
	uint8_t pattern_ed25519_publicKey[] = {0xA4, 0xBF, 0x35, 0x3D, 0x6C, 0x9D, 0x51, 0xCA,  0x6D, 0x98, 0x88, 0xA6, 0x26, 0x8C, 0xF2, 0xE8, 0xA5, 0xAD, 0x58, 0x97, 0x00, 0x5B, 0x58, 0xCC,  0x46, 0x82, 0xEB, 0x88, 0x21, 0x9A, 0xC0, 0x18};
	uint8_t pattern_ed25519_secretKey[] = {0x9E, 0xEE, 0x80, 0x89, 0xA1, 0x47, 0x6E, 0x4B,  0x01, 0x70, 0xE4, 0x74, 0x06, 0xE1, 0xCE, 0xF8, 0x62, 0x53, 0xE1, 0xC2, 0x3C, 0xDD, 0x63, 0x53,  0x8D, 0x2B, 0xF0, 0x3B, 0x52, 0xD9, 0x6C, 0x39};
	uint8_t pattern_x25519_publicKey[] = {0x53, 0x97, 0x95, 0x45, 0x1A, 0x04, 0xB5, 0xDD,  0x42, 0xD2, 0x73, 0x32, 0x9C, 0x1A, 0xC9, 0xFE, 0x58, 0x3A, 0x82, 0xF1, 0x82, 0xE8, 0xD7, 0xA5,  0xAD, 0xCB, 0xD0, 0x27, 0x6E, 0x03, 0xD7, 0x70};
	bctbx_ECDHContext_t *aliceECDH =  bctbx_CreateECDHContext(BCTBX_ECDH_X25519);
	bctbx_EDDSAContext_t *aliceEDDSA =  bctbx_CreateEDDSAContext(BCTBX_EDDSA_25519);

	if (!bctbx_crypto_have_ecc()) {
		bctbx_warning("test skipped as we don't have Elliptic Curve Cryptography in bctoolbox");
		return;
	}


	/* Start from ed25519 secret key and derive the public one */
	bctbx_EDDSA_setSecretKey(aliceEDDSA, pattern_ed25519_secretKey, 32);
	bctbx_EDDSADerivePublicKey(aliceEDDSA);
	BC_ASSERT_TRUE(memcmp(aliceEDDSA->publicKey, pattern_ed25519_publicKey, 32)==0);

	/* Convert ed25519 private to x25519 and check it derives to the correct public key
	(do not check direct value of private as some bits are masked at use but not necessarily during conversion) */
	bctbx_EDDSA_ECDH_privateKeyConversion(aliceEDDSA, aliceECDH);
	bctbx_ECDHDerivePublicKey(aliceECDH);
	BC_ASSERT_TRUE(memcmp(aliceECDH->selfPublic, pattern_x25519_publicKey, 32)==0);

	/* Convert directly ed25519 public to x25519 and check we stick to pattern */
	bctbx_EDDSA_ECDH_publicKeyConversion(aliceEDDSA, aliceECDH, BCTBX_ECDH_ISPEER);/* store it in peerPublic just for this test purpose */
	BC_ASSERT_TRUE(memcmp(aliceECDH->peerPublic, pattern_x25519_publicKey, 32)==0);

	/* cleaning */
	bctbx_DestroyEDDSAContext(aliceEDDSA);
	bctbx_DestroyECDHContext(aliceECDH);
}

static void sign_and_key_exchange(void) {
	int i;
	bctbx_rng_context_t *RNG;
	bctbx_ECDHContext_t *aliceECDH = NULL;
	bctbx_EDDSAContext_t *aliceEDDSA =  NULL;
	bctbx_ECDHContext_t *bobECDH =  NULL;
	bctbx_EDDSAContext_t *bobEDDSA =  NULL;
	uint8_t availableAlgosEDDSA[2]={BCTBX_EDDSA_25519, BCTBX_EDDSA_448};
	uint8_t availableAlgosECDH[2]={BCTBX_ECDH_X25519, BCTBX_ECDH_X448};
	uint8_t availableAlgosNb=2;
	uint8_t signature1[128]; /* buffer to store the signature, must be at least twice the size of the longer point coordinate (57*2) */
	size_t signatureLength1 = 128;
	uint8_t signature2[128]; /* buffer to store the signature, must be at least twice the size of the longer point coordinate (57*2) */
	size_t signatureLength2 = 128;
	uint8_t tmpKeyBuffer[64]; /* hold the EDDSA public key while swapping them between bob and alice */
	uint8_t context1[250];
	uint8_t context2[250];

	if (!bctbx_crypto_have_ecc()) {
		bctbx_warning("test skipped as we don't have Elliptic Curve Cryptography in bctoolbox");
		return;
	}

	/* Init the RNG */
	RNG = bctbx_rng_context_new();

	for (i=0; i<availableAlgosNb; i++) {
		/* generate EdDSA keys */
		aliceEDDSA = bctbx_CreateEDDSAContext(availableAlgosEDDSA[i]);
		bobEDDSA = bctbx_CreateEDDSAContext(availableAlgosEDDSA[i]);
		bctbx_EDDSACreateKeyPair(aliceEDDSA,  (int (*)(void *, unsigned char *, size_t))bctbx_rng_get, RNG);
		bctbx_EDDSACreateKeyPair(bobEDDSA,  (int (*)(void *, unsigned char *, size_t))bctbx_rng_get, RNG);

		/* Convert self keys to Montgomery form */
		aliceECDH = bctbx_CreateECDHContext(availableAlgosECDH[i]);
		bobECDH = bctbx_CreateECDHContext(availableAlgosECDH[i]);
		bctbx_EDDSA_ECDH_privateKeyConversion(aliceEDDSA, aliceECDH);
		bctbx_EDDSA_ECDH_publicKeyConversion(aliceEDDSA, aliceECDH, BCTBX_ECDH_ISSELF);
		bctbx_EDDSA_ECDH_privateKeyConversion(bobEDDSA, bobECDH);
		bctbx_EDDSA_ECDH_publicKeyConversion(bobEDDSA, bobECDH, BCTBX_ECDH_ISSELF);

		/* generate a random context */
		bctbx_rng_get(RNG, context1, 250);
		bctbx_rng_get(RNG, context2, 250);

		/* sign a message */
		bctbx_EDDSA_sign(aliceEDDSA, (uint8_t *)importantMessage1, strlen(importantMessage1), context1, 250, signature1, &signatureLength1);
		BC_ASSERT_NOT_EQUAL(signatureLength1, 0, int, "%d");
		bctbx_EDDSA_sign(bobEDDSA, (uint8_t *)importantMessage2, strlen(importantMessage2), context2, 250, signature2, &signatureLength2);
		BC_ASSERT_NOT_EQUAL(signatureLength2, 0, int, "%d");

		/* exchange EDDSA keys: Warning: reuse the original EDDSA context, it means we will loose our self EDDSA public key */
		memcpy(tmpKeyBuffer, bobEDDSA->publicKey, bobEDDSA->pointCoordinateLength);
		bctbx_EDDSA_setPublicKey(bobEDDSA, aliceEDDSA->publicKey, aliceEDDSA->pointCoordinateLength);
		bctbx_EDDSA_setPublicKey(aliceEDDSA, tmpKeyBuffer, bobEDDSA->pointCoordinateLength);
		/* convert peer public key to ECDH format, peer public keys are now in the EDDSA context  */
		bctbx_EDDSA_ECDH_publicKeyConversion(aliceEDDSA, aliceECDH, BCTBX_ECDH_ISPEER);
		bctbx_EDDSA_ECDH_publicKeyConversion(bobEDDSA, bobECDH, BCTBX_ECDH_ISPEER);

		/* Verify signed messages */
		BC_ASSERT_EQUAL(bctbx_EDDSA_verify(bobEDDSA, (uint8_t *)importantMessage1, strlen(importantMessage1), context1, 250, signature1, signatureLength1), BCTBX_VERIFY_SUCCESS, int, "%d");
		BC_ASSERT_EQUAL(bctbx_EDDSA_verify(aliceEDDSA, (uint8_t *)importantMessage2, strlen(importantMessage2), context2, 250, signature2, signatureLength2), BCTBX_VERIFY_SUCCESS, int, "%d");

		/* Compute shared secret and compare them */
		bctbx_ECDHComputeSecret(aliceECDH, NULL, NULL);
		bctbx_ECDHComputeSecret(bobECDH, NULL, NULL);

		/* compare the secrets */
		BC_ASSERT_TRUE(memcmp(aliceECDH->sharedSecret, bobECDH->sharedSecret, aliceECDH->pointCoordinateLength)==0);

		/* reset signatureLength for next run */
		signatureLength1=signatureLength2=128;

		/* cleaning */
		bctbx_DestroyEDDSAContext(aliceEDDSA);
		bctbx_DestroyECDHContext(aliceECDH);
		bctbx_DestroyEDDSAContext(bobEDDSA);
		bctbx_DestroyECDHContext(bobECDH);
	}

	bctbx_rng_context_free(RNG);
}

static void hash_test(void) {
	/* SHA patterns */
	char *sha_input = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

	uint8_t sha256_pattern[] = {0xcf, 0x5b, 0x16, 0xa7, 0x78, 0xaf, 0x83, 0x80, 0x03, 0x6c, 0xe5, 0x9e, 0x7b, 0x04, 0x92, 0x37, 0x0b, 0x24, 0x9b, 0x11, 0xe8, 0xf0, 0x7a, 0x51, 0xaf, 0xac, 0x45, 0x03, 0x7a, 0xfe, 0xe9, 0xd1};
	uint8_t sha384_pattern[] = {0x09, 0x33, 0x0c, 0x33, 0xf7, 0x11, 0x47, 0xe8, 0x3d, 0x19, 0x2f, 0xc7, 0x82, 0xcd, 0x1b, 0x47, 0x53, 0x11, 0x1b, 0x17, 0x3b, 0x3b, 0x05, 0xd2, 0x2f, 0xa0, 0x80, 0x86, 0xe3, 0xb0, 0xf7, 0x12, 0xfc, 0xc7, 0xc7, 0x1a, 0x55, 0x7e, 0x2d, 0xb9, 0x66, 0xc3, 0xe9, 0xfa, 0x91, 0x74, 0x60, 0x39};
	uint8_t sha512_pattern[] = {0x8e, 0x95, 0x9b, 0x75, 0xda, 0xe3, 0x13, 0xda, 0x8c, 0xf4, 0xf7, 0x28, 0x14, 0xfc, 0x14, 0x3f, 0x8f, 0x77, 0x79, 0xc6, 0xeb, 0x9f, 0x7f, 0xa1, 0x72, 0x99, 0xae, 0xad, 0xb6, 0x88, 0x90, 0x18, 0x50, 0x1d, 0x28, 0x9e, 0x49, 0x00, 0xf7, 0xe4, 0x33, 0x1b, 0x99, 0xde, 0xc4, 0xb5, 0x43, 0x3a, 0xc7, 0xd3, 0x29, 0xee, 0xb6, 0xdd, 0x26, 0x54, 0x5e, 0x96, 0xe5, 0x5b, 0x87, 0x4b, 0xe9, 0x09};

	/* HMAC SHA patterns from RFC 4231 test case 7 */
	uint8_t hmac_sha_key[] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
	uint8_t hmac_sha_data[] = {0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x61, 0x20, 0x74, 0x65, 0x73, 0x74, 0x20, 0x75, 0x73, 0x69, 0x6e, 0x67, 0x20, 0x61, 0x20, 0x6c, 0x61, 0x72, 0x67, 0x65, 0x72, 0x20, 0x74, 0x68, 0x61, 0x6e, 0x20, 0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x2d, 0x73, 0x69, 0x7a, 0x65, 0x20, 0x6b, 0x65, 0x79, 0x20, 0x61, 0x6e, 0x64, 0x20, 0x61, 0x20, 0x6c, 0x61, 0x72, 0x67, 0x65, 0x72, 0x20, 0x74, 0x68, 0x61, 0x6e, 0x20, 0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x2d, 0x73, 0x69, 0x7a, 0x65, 0x20, 0x64, 0x61, 0x74, 0x61, 0x2e, 0x20, 0x54, 0x68, 0x65, 0x20, 0x6b, 0x65, 0x79, 0x20, 0x6e, 0x65, 0x65, 0x64, 0x73, 0x20, 0x74, 0x6f, 0x20, 0x62, 0x65, 0x20, 0x68, 0x61, 0x73, 0x68, 0x65, 0x64, 0x20, 0x62, 0x65, 0x66, 0x6f, 0x72, 0x65, 0x20, 0x62, 0x65, 0x69, 0x6e, 0x67, 0x20, 0x75, 0x73, 0x65, 0x64, 0x20, 0x62, 0x79, 0x20, 0x74, 0x68, 0x65, 0x20, 0x48, 0x4d, 0x41, 0x43, 0x20, 0x61, 0x6c, 0x67, 0x6f, 0x72, 0x69, 0x74, 0x68, 0x6d, 0x2e};
	uint8_t hmac_sha256_pattern[] = {0x9b, 0x09, 0xff, 0xa7, 0x1b, 0x94, 0x2f, 0xcb, 0x27, 0x63, 0x5f, 0xbc, 0xd5, 0xb0, 0xe9, 0x44, 0xbf, 0xdc, 0x63, 0x64, 0x4f, 0x07, 0x13, 0x93, 0x8a, 0x7f, 0x51, 0x53, 0x5c, 0x3a, 0x35, 0xe2};
	uint8_t hmac_sha384_pattern[] = {0x66, 0x17, 0x17, 0x8e, 0x94, 0x1f, 0x02, 0x0d, 0x35, 0x1e, 0x2f, 0x25, 0x4e, 0x8f, 0xd3, 0x2c, 0x60, 0x24, 0x20, 0xfe, 0xb0, 0xb8, 0xfb, 0x9a, 0xdc, 0xce, 0xbb, 0x82, 0x46, 0x1e, 0x99, 0xc5, 0xa6, 0x78, 0xcc, 0x31, 0xe7, 0x99, 0x17, 0x6d, 0x38, 0x60, 0xe6, 0x11, 0x0c, 0x46, 0x52, 0x3e};
	uint8_t hmac_sha512_pattern[] = {0xe3, 0x7b, 0x6a, 0x77, 0x5d, 0xc8, 0x7d, 0xba, 0xa4, 0xdf, 0xa9, 0xf9, 0x6e, 0x5e, 0x3f, 0xfd, 0xde, 0xbd, 0x71, 0xf8, 0x86, 0x72, 0x89, 0x86, 0x5d, 0xf5, 0xa3, 0x2d, 0x20, 0xcd, 0xc9, 0x44, 0xb6, 0x02, 0x2c, 0xac, 0x3c, 0x49, 0x82, 0xb1, 0x0d, 0x5e, 0xeb, 0x55, 0xc3, 0xe4, 0xde, 0x15, 0x13, 0x46, 0x76, 0xfb, 0x6d, 0xe0, 0x44, 0x60, 0x65, 0xc9, 0x74, 0x40, 0xfa, 0x8c, 0x6a, 0x58};

	uint8_t outputBuffer[64];

	bctbx_sha256((uint8_t *)sha_input, strlen(sha_input), 32, outputBuffer);
	BC_ASSERT_TRUE(memcmp(outputBuffer, sha256_pattern, 32)==0);

	bctbx_sha384((uint8_t *)sha_input, strlen(sha_input), 48, outputBuffer);
	BC_ASSERT_TRUE(memcmp(outputBuffer, sha384_pattern, 48)==0);

	bctbx_sha512((uint8_t *)sha_input, strlen(sha_input), 64, outputBuffer);
	BC_ASSERT_TRUE(memcmp(outputBuffer, sha512_pattern, 64)==0);

	bctbx_hmacSha256(hmac_sha_key, sizeof(hmac_sha_key), hmac_sha_data, sizeof(hmac_sha_data), 32, outputBuffer);
	BC_ASSERT_TRUE(memcmp(outputBuffer, hmac_sha256_pattern, 32)==0);

	bctbx_hmacSha384(hmac_sha_key, sizeof(hmac_sha_key), hmac_sha_data, sizeof(hmac_sha_data), 48, outputBuffer);
	BC_ASSERT_TRUE(memcmp(outputBuffer, hmac_sha384_pattern, 48)==0);

	bctbx_hmacSha512(hmac_sha_key, sizeof(hmac_sha_key), hmac_sha_data, sizeof(hmac_sha_data), 64, outputBuffer);
	BC_ASSERT_TRUE(memcmp(outputBuffer, hmac_sha512_pattern, 64)==0);

}

static test_t crypto_tests[] = {
	TEST_NO_TAG("Diffie-Hellman Key exchange", DHM),
	TEST_NO_TAG("Elliptic Curve Diffie-Hellman Key exchange", ECDH),
	TEST_NO_TAG("ECDH25519 decaf-mbedtls", ECDH25519compat),
	TEST_NO_TAG("EdDSA sign and verify", EdDSA),
	TEST_NO_TAG("Ed25519 to X25519 key conversion", ed25519_to_x25519_keyconversion),
	TEST_NO_TAG("Sign message and exchange key using the same base secret", sign_and_key_exchange),
	TEST_NO_TAG("Hash functions", hash_test)
};

test_suite_t crypto_test_suite = {"Crypto", NULL, NULL, NULL, NULL,
							   sizeof(crypto_tests) / sizeof(crypto_tests[0]), crypto_tests};
