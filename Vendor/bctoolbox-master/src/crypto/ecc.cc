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

#include <bctoolbox/crypto.h>

#ifdef HAVE_DECAF

#include "decaf.h"
#include "decaf/ed255.h"
#include "decaf/ed448.h"

int bctbx_crypto_have_ecc(void) {
	/* Check our re-defines of key length are matching the decaf ones */
	static_assert(BCTBX_ECDH_X25519_PUBLIC_SIZE == DECAF_X25519_PUBLIC_BYTES, "forwarding DECAF defines mismatch");
	static_assert(BCTBX_ECDH_X25519_PRIVATE_SIZE == DECAF_X25519_PRIVATE_BYTES, "forwarding DECAF defines mismatch");
	static_assert(BCTBX_ECDH_X448_PUBLIC_SIZE == DECAF_X448_PUBLIC_BYTES, "forwarding DECAF defines mismatch");
	static_assert(BCTBX_ECDH_X448_PRIVATE_SIZE == DECAF_X448_PRIVATE_BYTES, "forwarding DECAF defines mismatch");

	static_assert(BCTBX_EDDSA_25519_PUBLIC_SIZE == DECAF_EDDSA_25519_PUBLIC_BYTES, "forwarding DECAF defines mismatch");
	static_assert(BCTBX_EDDSA_25519_PRIVATE_SIZE == DECAF_EDDSA_25519_PRIVATE_BYTES, "forwarding DECAF defines mismatch");
	static_assert(BCTBX_EDDSA_25519_SIGNATURE_SIZE == DECAF_EDDSA_25519_SIGNATURE_BYTES, "forwarding DECAF defines mismatch");
	static_assert(BCTBX_EDDSA_448_PUBLIC_SIZE == DECAF_EDDSA_448_PUBLIC_BYTES, "forwarding DECAF defines mismatch");
	static_assert(BCTBX_EDDSA_448_PRIVATE_SIZE == DECAF_EDDSA_448_PRIVATE_BYTES, "forwarding DECAF defines mismatch");
	static_assert(BCTBX_EDDSA_448_SIGNATURE_SIZE == DECAF_EDDSA_448_SIGNATURE_BYTES, "forwarding DECAF defines mismatch");

	return TRUE;
}

/**
 * @brief Return a 32 bits unsigned integer, each bit set to one matches an
 * available key agreement algorithm as defined in bctoolbox/include/crypto.h
 *
 * This function is implemented in ecc.c as all other backend crypto libraries
 * (polarssl-1.2, polarssl-1.3/1.4, mbedtls implement DHM2048 and DHM3072
 *
 * @return An unsigned integer of 32 flags matching key agreement algos
 */
uint32_t bctbx_key_agreement_algo_list(void) {
	return BCTBX_DHM_2048|BCTBX_DHM_3072|BCTBX_ECDH_X25519|BCTBX_ECDH_X448;
}

/*****************************************************************************/
/*** Elliptic Curve Diffie-Hellman - ECDH                                  ***/
/*****************************************************************************/

/* Create and initialise the ECDH Context */
bctbx_ECDHContext_t *bctbx_CreateECDHContext(const uint8_t ECDHAlgo) {
	/* create the context */
	bctbx_ECDHContext_t *context = (bctbx_ECDHContext_t *)bctbx_malloc(sizeof(bctbx_ECDHContext_t));
	memset (context, 0, sizeof(bctbx_ECDHContext_t));

	/* initialise pointer to NULL to ensure safe call to free() when destroying context */
	context->secret = NULL;
	context->sharedSecret = NULL;
	context->selfPublic = NULL;
	context->peerPublic = NULL;
	context->cryptoModuleData = NULL; /* decaf do not use any context for these operations */

	/* set parameters in the context */
	context->algo=ECDHAlgo;

	switch (ECDHAlgo) {
		case BCTBX_ECDH_X25519:
			context->pointCoordinateLength = DECAF_X25519_PUBLIC_BYTES;
			context->secretLength = DECAF_X25519_PRIVATE_BYTES;
			break;
		case BCTBX_ECDH_X448:
			context->pointCoordinateLength = DECAF_X448_PUBLIC_BYTES;
			context->secretLength = DECAF_X448_PRIVATE_BYTES;
			break;
		default:
			bctbx_free(context);
			return NULL;
			break;
	}
	return context;
}

/**
 *
 * @brief	Set the given secret key in the ECDH context
 *
 * @param[in/out]	context		ECDH context, will store the given secret key if length is matching the pre-setted algo for this context
 * @param[in]		secret		The buffer holding the secret, is duplicated in the ECDH context
 * @param[in]		secretLength	Length of previous buffer, must match the algo type setted at context creation
 */
void bctbx_ECDHSetSecretKey(bctbx_ECDHContext_t *context, const uint8_t *secret, const size_t secretLength) {
	if (context!=NULL && context->secretLength==secretLength) {
		if (context->secret == NULL) { /* allocate a new buffer */
			context->secret = (uint8_t *)bctbx_malloc(context->secretLength);
		} else { /* or make sure we wipe out the existing one */
			memset(context->secret, 0, context->secretLength);
		}
		memcpy(context->secret, secret, secretLength);
	}
}

/**
 *
 * @brief	Set the given self public key in the ECDH context
 *		Warning: no check if it matches the private key value
 *
 * @param[in/out]	context			ECDH context, will store the given self public key if length is matching the pre-setted algo for this context
 * @param[in]		selfPublic		The buffer holding the self public key, is duplicated in the ECDH context
 * @param[in]		selfPublicLength	Length of previous buffer, must match the algo type setted at context creation
 */
void bctbx_ECDHSetSelfPublicKey(bctbx_ECDHContext_t *context, const uint8_t *selfPublic, const size_t selfPublicLength) {
	if (context!=NULL && context->pointCoordinateLength==selfPublicLength) {
		if (context->selfPublic == NULL) {
			context->selfPublic = (uint8_t *)bctbx_malloc(selfPublicLength);
		}
		memcpy(context->selfPublic, selfPublic, selfPublicLength);
	}
}
/**
 *
 * @brief	Set the given peer public key in the ECDH context
 *
 * @param[in/out]	context			ECDH context, will store the given peer public key if length is matching the pre-setted algo for this context
 * @param[in]		peerPublic		The buffer holding the peer public key, is duplicated in the ECDH context
 * @param[in]		peerPublicLength	Length of previous buffer, must match the algo type setted at context creation
 */
BCTBX_PUBLIC void bctbx_ECDHSetPeerPublicKey(bctbx_ECDHContext_t *context, const uint8_t *peerPublic, const size_t peerPublicLength) {
	if (context!=NULL && context->pointCoordinateLength==peerPublicLength) {
		/* allocate public key buffer if needed */
		if (context->peerPublic == NULL) {
			context->peerPublic = (uint8_t *)bctbx_malloc(peerPublicLength);
		}
		memcpy(context->peerPublic, peerPublic, peerPublicLength);
	}
}

/**
 *
 * @brief	Derive the public key from the secret setted in context and using preselected algo, following RFC7748
 *
 * @param[in/out]	context		The context holding algo setting and secret, used to store public key
 */
void bctbx_ECDHDerivePublicKey(bctbx_ECDHContext_t *context) {
	if (context!=NULL && context->secret!=NULL) {
		/* allocate public key buffer if needed */
		if (context->selfPublic == NULL) {
			context->selfPublic = (uint8_t *)bctbx_malloc(context->pointCoordinateLength);
		}

		/* then generate the public value */
		switch (context->algo) {
			case BCTBX_ECDH_X25519:
				decaf_x25519_derive_public_key(context->selfPublic, context->secret);
				break;
			case BCTBX_ECDH_X448:
				decaf_x448_derive_public_key(context->selfPublic, context->secret);
				break;
			default:
				break;
		}
	}
}

/* generate the random secret and compute the public value */
void bctbx_ECDHCreateKeyPair(bctbx_ECDHContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext) {
	if (context!=NULL) {
		/* first generate the random bytes of self secret and store it in context(do it directly instead of creating a temp buffer and calling SetSecretKey) */
		if (context->secret == NULL) { /* allocate buffer if needed */
			context->secret = (uint8_t *)bctbx_malloc(context->secretLength);
		} else { /* otherwise make sure we wipe out previous secret */
			memset(context->secret, 0, context->secretLength);
		}
		rngFunction(rngContext, context->secret, context->secretLength);

		/* Then derive the public key */
		bctbx_ECDHDerivePublicKey(context);
	}
}

/* compute secret - the ->peerPublic field of context must have been set before calling this function */
void bctbx_ECDHComputeSecret(bctbx_ECDHContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext) {
	if (context != NULL && context->secret!=NULL && context->peerPublic!=NULL) {
		if (context->sharedSecret == NULL) { /* allocate buffer if needed */
			context->sharedSecret = (uint8_t *)bctbx_malloc(context->pointCoordinateLength);
		} else { /* otherwise make sure we wipe out previous secret */
			memset(context->sharedSecret, 0, context->pointCoordinateLength);
		}

		switch (context->algo) {
			case BCTBX_ECDH_X25519:
				if (decaf_x25519(context->sharedSecret, context->peerPublic, context->secret)==DECAF_FAILURE) {
					bctbx_free(context->sharedSecret);
					memset(context->sharedSecret, 0, context->pointCoordinateLength);
					context->sharedSecret=NULL;
				}
				break;
			case BCTBX_ECDH_X448:
				if (decaf_x448(context->sharedSecret, context->peerPublic, context->secret)==DECAF_FAILURE) {
					bctbx_free(context->sharedSecret);
					memset(context->sharedSecret, 0, context->pointCoordinateLength);
					context->sharedSecret=NULL;
				}
				break;
			default:
				break;
		}
	}
}

/* clean DHM context */
void bctbx_DestroyECDHContext(bctbx_ECDHContext_t *context) {
	if (context!= NULL) {
		/* key and secret must be erased from memory and not just freed */
		if (context->secret != NULL) {
			bctbx_clean(context->secret, context->secretLength);
			free(context->secret);
			context->secret=NULL;
		}
		free(context->selfPublic);
		context->selfPublic=NULL;
		if (context->sharedSecret != NULL) {
			bctbx_clean(context->sharedSecret, context->pointCoordinateLength);
			free(context->sharedSecret);
			context->sharedSecret=NULL;
		}
		free(context->peerPublic);
		context->peerPublic=NULL;
		free(context);
	}
}


/*****************************************************************************/
/*** Edwards Curve Digital Signature Algorithm - EdDSA                     ***/
/*****************************************************************************/

/* Create and initialise the EDDSA Context */
bctbx_EDDSAContext_t *bctbx_CreateEDDSAContext(uint8_t EDDSAAlgo) {
	/* create the context */
	bctbx_EDDSAContext_t *context = (bctbx_EDDSAContext_t *)bctbx_malloc(sizeof(bctbx_EDDSAContext_t));
	memset (context, 0, sizeof(bctbx_EDDSAContext_t));

	/* initialise pointer to NULL to ensure safe call to free() when destroying context */
	context->secretKey = NULL;
	context->publicKey = NULL;
	context->cryptoModuleData = NULL; /* decaf do not use any context for these operations */

	/* set parameters in the context */
	context->algo=EDDSAAlgo;

	switch (EDDSAAlgo) {
		case BCTBX_EDDSA_25519:
			context->pointCoordinateLength = DECAF_EDDSA_25519_PUBLIC_BYTES;
			context->secretLength = DECAF_EDDSA_25519_PRIVATE_BYTES;
			break;
		case BCTBX_EDDSA_448:
			context->pointCoordinateLength = DECAF_EDDSA_448_PUBLIC_BYTES;
			context->secretLength = DECAF_EDDSA_448_PRIVATE_BYTES;
			break;
		default:
			bctbx_free(context);
			return NULL;
			break;
	}
	return context;
}

/* generate the random secret and compute the public value */
void bctbx_EDDSACreateKeyPair(bctbx_EDDSAContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext) {
	/* first generate the random bytes of self secret and store it in context */
	if (context->secretKey==NULL) {
		context->secretKey = (uint8_t *)bctbx_malloc(context->secretLength);
	}
	rngFunction(rngContext, context->secretKey, context->secretLength);

	/* then generate the public value */
	bctbx_EDDSADerivePublicKey(context);
}

/* using the secret key present in context, derive the public one */
void bctbx_EDDSADerivePublicKey(bctbx_EDDSAContext_t *context) {
	/* check we have a context and it was set to get a public key matching the length of the given one */
	if (context != NULL) {
		if (context->secretKey != NULL) { /* don't go further if we have no secret key in context */
			if (context->publicKey == NULL) { /* delete existing key if any */
				context->publicKey = (uint8_t *)bctbx_malloc(context->pointCoordinateLength);
			}

			/* then generate the public value */
			switch (context->algo) {
				case BCTBX_EDDSA_25519:
					decaf_ed25519_derive_public_key(context->publicKey, context->secretKey);
					break;
				case BCTBX_EDDSA_448:
					decaf_ed448_derive_public_key(context->publicKey, context->secretKey);
					break;
				default:
					break;
			}
		}
	}
}

/* clean EDDSA context */
void bctbx_DestroyEDDSAContext(bctbx_EDDSAContext_t *context) {
	if (context!= NULL) {
		/* secretKey must be erased from memory and not just freed */
		if (context->secretKey != NULL) {
			bctbx_clean(context->secretKey, context->secretLength);
			free(context->secretKey);
		}
		free(context->publicKey);
		free(context);
	}
}

/**
 *
 * @brief Sign the message given using private key and EdDSA algo set in context
 *
 * @param[in]		context			EDDSA context storing the algorithm to use(ed448 or ed25519) and the private key to use
 * @param[in]		message			The message to be signed
 * @param[in]		messageLength		Length of the message buffer
 * @param [in]		associatedData		A "context" for this signature of up to 255 bytes.
 * @param [in]		associatedDataLength	Length of the context.
 * @param[out]		signature		The signature
 * @param[in/out]	signatureLength		The size of the signature buffer as input, the size of the actual signature as output
 *
 */
void bctbx_EDDSA_sign(bctbx_EDDSAContext_t *context, const uint8_t *message, const size_t messageLength, const uint8_t *associatedData, const uint8_t associatedDataLength, uint8_t *signature, size_t *signatureLength) {
	if (context!=NULL) {
		switch (context->algo) {
			case BCTBX_EDDSA_25519:
				if (*signatureLength>=DECAF_EDDSA_25519_SIGNATURE_BYTES) { /* check the buffer is large enough to hold the signature */
					decaf_ed25519_sign ( signature, context->secretKey, context->publicKey, message, messageLength, 0, associatedData, associatedDataLength);
					*signatureLength=DECAF_EDDSA_25519_SIGNATURE_BYTES;
					return;
				}
				break;
			case BCTBX_EDDSA_448:
				if (*signatureLength>=DECAF_EDDSA_448_SIGNATURE_BYTES) { /* check the buffer is large enough to hold the signature */
					decaf_ed448_sign ( signature, context->secretKey, context->publicKey, message, messageLength, 0, associatedData, associatedDataLength); 
					*signatureLength=DECAF_EDDSA_448_SIGNATURE_BYTES;
					return;
				}
				break;
		default:
			break;
		}
	}
	*signatureLength=0;
}

/**
 *
 * @brief Set a public key in a EDDSA context to be used to verify messages signature
 *
 * @param[in/out]	context		EDDSA context storing the algorithm to use(ed448 or ed25519)
 * @param[in]		publicKey	The public to store in context
 * @param[in]		publicKeyLength	The length of previous buffer
 */
void bctbx_EDDSA_setPublicKey(bctbx_EDDSAContext_t *context, const uint8_t *publicKey, size_t publicKeyLength) {
	/* check we have a context and it was set to get a public key matching the length of the given one */
	if (context != NULL) {
		if (context->pointCoordinateLength == publicKeyLength) {
			/* allocate key buffer if needed */
			if (context->publicKey == NULL) {
				context->publicKey = (uint8_t *)bctbx_malloc(publicKeyLength);
			}
			memcpy(context->publicKey, publicKey, publicKeyLength);
		}
	}
}
/**
 *
 * @brief Set a private key in a EDDSA context to be used to sign message
 *
 * @param[in/out]	context		EDDSA context storing the algorithm to use(ed448 or ed25519)
 * @param[in]		secretKey	The secret to store in context
 * @param[in]		secretKeyLength	The length of previous buffer
 */
void bctbx_EDDSA_setSecretKey(bctbx_EDDSAContext_t *context, const uint8_t *secretKey, size_t secretKeyLength) {
	/* check we have a context and it was set to get a public key matching the length of the given one */
	if (context != NULL) {
		if (context->secretLength == secretKeyLength) {
			/* allocate key buffer if needed */
			if (context->secretKey == NULL) {
				context->secretKey = (uint8_t *)bctbx_malloc(secretKeyLength);
			}
			memcpy(context->secretKey, secretKey, secretKeyLength);
		}
	}
}

/**
 *
 * @brief Use the public key set in context to verify the given signature and message
 *
 * @param[in/out]	context			EDDSA context storing the algorithm to use(ed448 or ed25519) and public key
 * @param[in]		message			Message to verify
 * @param[in]		messageLength		Length of the message buffer
 * @param [in]		associatedData		A "context" for this signature of up to 255 bytes.
 * @param [in]		associatedDataLength	Length of the context.
 * @param[in]		signature		The signature
 * @param[in]		signatureLength		The size of the signature buffer
 *
 * @return BCTBX_VERIFY_SUCCESS or BCTBX_VERIFY_FAILED
 */
int bctbx_EDDSA_verify(bctbx_EDDSAContext_t *context, const uint8_t *message, size_t messageLength, const uint8_t *associatedData, const uint8_t associatedDataLength, const uint8_t *signature, size_t signatureLength) {
	int ret = BCTBX_VERIFY_FAILED;
	if (context!=NULL) {
		decaf_error_t retDecaf = DECAF_FAILURE;
		switch (context->algo) {
			case BCTBX_EDDSA_25519:
				if (signatureLength==DECAF_EDDSA_25519_SIGNATURE_BYTES) { /* check length of given signature */
					retDecaf = decaf_ed25519_verify (signature, context->publicKey, message, messageLength, 0, associatedData, associatedDataLength);
				}
				break;
			case BCTBX_EDDSA_448:
				if (signatureLength==DECAF_EDDSA_448_SIGNATURE_BYTES) { /* check lenght of given signature */
					retDecaf = decaf_ed448_verify (signature, context->publicKey, message, messageLength, 0, associatedData, associatedDataLength);
				}
				break;
			default:
				break;
		}
		if (retDecaf == DECAF_SUCCESS) {
			ret = BCTBX_VERIFY_SUCCESS;
		}
	}
	return ret;
}

/**
 *
 * @brief Convert a EDDSA private key to a ECDH private key
 *      pass the EDDSA private key through the hash function used in EdDSA
 *
 * @param[in]	ed	Context holding the current private key to convert
 * @param[out]	x	Context to store the private key for x25519 key exchange
*/
void bctbx_EDDSA_ECDH_privateKeyConversion(const bctbx_EDDSAContext_t *ed, bctbx_ECDHContext_t *x) {
	if (ed!=NULL && x!=NULL && ed->secretKey!=NULL) {
		if (ed->algo == BCTBX_EDDSA_25519 && x->algo == BCTBX_ECDH_X25519) {
			/* allocate key buffer if needed */
			if (x->secret==NULL) {
				x->secret = (uint8_t *)bctbx_malloc(x->secretLength);
			}
			decaf_ed25519_convert_private_key_to_x25519(x->secret, ed->secretKey);
		} else if (ed->algo == BCTBX_EDDSA_448 && x->algo == BCTBX_ECDH_X448) {
			/* allocate key buffer if needed */
			if (x->secret==NULL) {
				x->secret = (uint8_t *)bctbx_malloc(x->secretLength);
			}
			decaf_ed448_convert_private_key_to_x448(x->secret, ed->secretKey);
		}
	}
}

/**
 *
 * @brief Convert a EDDSA public key to a ECDH public key
 * 	point conversion : montgomeryX = (edwardsY + 1)*inverse(1 - edwardsY) mod p
 *
 * @param[in]	ed	Context holding the current public key to convert
 * @param[out]	x	Context to store the public key for x25519 key exchange
 * @param[in]	isSelf	Flag to decide where to store the public key in context: BCTBX_ECDH_ISPEER or BCTBX_ECDH_ISSELF
*/
void bctbx_EDDSA_ECDH_publicKeyConversion(const bctbx_EDDSAContext_t *ed, bctbx_ECDHContext_t *x, uint8_t isSelf) {
	if (ed!=NULL && x!=NULL && ed->publicKey!=NULL) {
		if (ed->algo == BCTBX_EDDSA_25519 && x->algo == BCTBX_ECDH_X25519) {
			if (isSelf==BCTBX_ECDH_ISPEER) {
				if (x->peerPublic==NULL) {
					x->peerPublic = (uint8_t *)bctbx_malloc(x->pointCoordinateLength);
				}
				decaf_ed25519_convert_public_key_to_x25519(x->peerPublic, ed->publicKey);
			} else {
				if (x->selfPublic==NULL) {
					x->selfPublic = (uint8_t *)bctbx_malloc(x->pointCoordinateLength);
				}
				decaf_ed25519_convert_public_key_to_x25519(x->selfPublic, ed->publicKey);
			}
		} else if (ed->algo == BCTBX_EDDSA_448 && x->algo == BCTBX_ECDH_X448) {
			if (isSelf==BCTBX_ECDH_ISPEER) {
				if (x->peerPublic==NULL) {
					x->peerPublic = (uint8_t *)bctbx_malloc(x->pointCoordinateLength);
				}
				decaf_ed448_convert_public_key_to_x448(x->peerPublic, ed->publicKey);
			} else {
				if (x->selfPublic==NULL) {
					x->selfPublic = (uint8_t *)bctbx_malloc(x->pointCoordinateLength);
				}
				decaf_ed448_convert_public_key_to_x448(x->selfPublic, ed->publicKey);
			}
		}
	}

}

#else /* HAVE_DECAF */
 /* This function is implemented in ecc.c as all other backend crypto libraries
 * (polarssl-1.2, polarssl-1.3/1.4, mbedtls implement DHM2048 and DHM3072
 */
uint32_t bctbx_key_agreement_algo_list(void) {
	return BCTBX_DHM_2048|BCTBX_DHM_3072;
}

/* We do not have lib decaf, implement empty stubs */
int bctbx_crypto_have_ecc(void) { return FALSE;}
bctbx_ECDHContext_t *bctbx_CreateECDHContext(const uint8_t ECDHAlgo) {return NULL;}
void bctbx_ECDHCreateKeyPair(bctbx_ECDHContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext) {return;}
void bctbx_ECDHSetSecretKey(bctbx_ECDHContext_t *context, const uint8_t *secret, const size_t secretLength){return;}
void bctbx_ECDHSetSelfPublicKey(bctbx_ECDHContext_t *context, const uint8_t *selfPublic, const size_t selfPublicLength){return;}
void bctbx_ECDHSetPeerPublicKey(bctbx_ECDHContext_t *context, const uint8_t *peerPublic, const size_t peerPublicLength){return;}
void bctbx_ECDHDerivePublicKey(bctbx_ECDHContext_t *context){return;}
void bctbx_ECDHComputeSecret(bctbx_ECDHContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext){return;}
void bctbx_DestroyECDHContext(bctbx_ECDHContext_t *context){return;}

bctbx_EDDSAContext_t *bctbx_CreateEDDSAContext(uint8_t EDDSAAlgo) {return NULL;}
void bctbx_EDDSACreateKeyPair(bctbx_EDDSAContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext) {return;}
void bctbx_EDDSADerivePublicKey(bctbx_EDDSAContext_t *context) {return;}
void bctbx_DestroyEDDSAContext(bctbx_EDDSAContext_t *context) {return;}
void bctbx_EDDSA_sign(bctbx_EDDSAContext_t *context, const uint8_t *message, const size_t messageLength, const uint8_t *associatedData, const uint8_t associatedDataLength, uint8_t *signature, size_t *signatureLength) {return;}
void bctbx_EDDSA_setPublicKey(bctbx_EDDSAContext_t *context, const uint8_t *publicKey, const size_t publicKeyLength) {return;}
void bctbx_EDDSA_setSecretKey(bctbx_EDDSAContext_t *context, const uint8_t *secretKey, const size_t secretKeyLength) {return;}
int bctbx_EDDSA_verify(bctbx_EDDSAContext_t *context, const uint8_t *message, size_t messageLength, const uint8_t *associatedData, const uint8_t associatedDataLength, const uint8_t *signature, size_t signatureLength) {return BCTBX_VERIFY_FAILED;}
void bctbx_EDDSA_ECDH_privateKeyConversion(const bctbx_EDDSAContext_t *ed, bctbx_ECDHContext_t *x) {return;}
void bctbx_EDDSA_ECDH_publicKeyConversion(const bctbx_EDDSAContext_t *ed, bctbx_ECDHContext_t *x, uint8_t isSelf) {return;}
#endif
