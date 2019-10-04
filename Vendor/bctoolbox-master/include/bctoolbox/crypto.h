/*
crypto.h
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
#ifndef BCTBX_CRYPTO_H
#define BCTBX_CRYPTO_H

#include "bctoolbox/port.h"
#include "bctoolbox/list.h"

/* key agreements settings defines */
/* Each algo is defined as a bit toggled in a 32 bits integer,
 * so we can easily ask for all availables ones
 */
#define BCTBX_DHM_UNSET		0x00000000
#define BCTBX_DHM_2048		0x00000001
#define BCTBX_DHM_3072		0x00000002
#define BCTBX_ECDH_X25519	0x00000004
#define BCTBX_ECDH_X448		0x00000008

/* EdDSA defines */
#define BCTBX_EDDSA_UNSET	0
#define BCTBX_EDDSA_25519	1
#define BCTBX_EDDSA_448		2

#define BCTBX_VERIFY_SUCCESS	0
#define BCTBX_VERIFY_FAILED	-1

/* Elliptic Curves Key lengths defines:
theses are redefines of the values defined in decaf headers made available to bctoolbox library users
bctoolbox will fail to compile if these values are not in sync with the decaf ones */
#define BCTBX_ECDH_X25519_PUBLIC_SIZE	32
#define BCTBX_ECDH_X25519_PRIVATE_SIZE	BCTBX_ECDH_X25519_PUBLIC_SIZE
#define BCTBX_ECDH_X448_PUBLIC_SIZE	56
#define BCTBX_ECDH_X448_PRIVATE_SIZE	BCTBX_ECDH_X448_PUBLIC_SIZE

#define BCTBX_EDDSA_25519_PUBLIC_SIZE	32
#define BCTBX_EDDSA_25519_PRIVATE_SIZE	BCTBX_EDDSA_25519_PUBLIC_SIZE
#define BCTBX_EDDSA_25519_SIGNATURE_SIZE	(BCTBX_EDDSA_25519_PUBLIC_SIZE + BCTBX_EDDSA_25519_PRIVATE_SIZE)
#define BCTBX_EDDSA_448_PUBLIC_SIZE	57
#define BCTBX_EDDSA_448_PRIVATE_SIZE	BCTBX_EDDSA_448_PUBLIC_SIZE
#define BCTBX_EDDSA_448_SIGNATURE_SIZE	(BCTBX_EDDSA_448_PUBLIC_SIZE + BCTBX_EDDSA_448_PRIVATE_SIZE)


/* SSL settings defines */
#define BCTBX_SSL_UNSET -1

#define BCTBX_SSL_IS_CLIENT 0
#define BCTBX_SSL_IS_SERVER 1

#define BCTBX_SSL_TRANSPORT_STREAM 0
#define BCTBX_SSL_TRANSPORT_DATAGRAM 1

#define BCTBX_SSL_VERIFY_NONE 0
#define BCTBX_SSL_VERIFY_OPTIONAL 1
#define BCTBX_SSL_VERIFY_REQUIRED 2

/* Encryption/decryption defines */
#define BCTBX_GCM_ENCRYPT     1
#define BCTBX_GCM_DECRYPT     0

/* Error codes : All error codes are negative and defined  on 32 bits on format -0x7XXXXXXX
 * in order to be sure to not overlap on crypto librairy (polarssl or mbedtls for now) which are defined on 16 bits 0x[7-0]XXX */
#define BCTBX_ERROR_UNSPECIFIED_ERROR			-0x70000000
#define BCTBX_ERROR_OUTPUT_BUFFER_TOO_SMALL		-0x70001000
#define BCTBX_ERROR_INVALID_BASE64_INPUT		-0x70002000
#define BCTBX_ERROR_INVALID_INPUT_DATA			-0x70004000
#define BCTBX_ERROR_UNAVAILABLE_FUNCTION		-0x70008000

/* key related */
#define BCTBX_ERROR_UNABLE_TO_PARSE_KEY		-0x70010000

/* Certificate related */
#define BCTBX_ERROR_INVALID_CERTIFICATE			-0x70020000
#define BCTBX_ERROR_CERTIFICATE_GENERATION_FAIL	-0x70020001
#define BCTBX_ERROR_CERTIFICATE_WRITE_PEM		-0x70020002
#define BCTBX_ERROR_CERTIFICATE_PARSE_PEM		-0x70020004
#define BCTBX_ERROR_UNSUPPORTED_HASH_FUNCTION	-0x70020008

/* SSL related */
#define BCTBX_ERROR_INVALID_SSL_CONFIG		-0x70030001
#define BCTBX_ERROR_INVALID_SSL_TRANSPORT	-0x70030002
#define BCTBX_ERROR_INVALID_SSL_ENDPOINT	-0x70030004
#define BCTBX_ERROR_INVALID_SSL_AUTHMODE	-0x70030008
#define BCTBX_ERROR_INVALID_SSL_CONTEXT		-0x70030010

#define BCTBX_ERROR_NET_WANT_READ			-0x70032000
#define BCTBX_ERROR_NET_WANT_WRITE			-0x70034000
#define BCTBX_ERROR_SSL_PEER_CLOSE_NOTIFY	-0x70038000
#define BCTBX_ERROR_NET_CONN_RESET			-0x70030000

/* Symmetric ciphers related */
#define BCTBX_ERROR_AUTHENTICATION_FAILED	-0x70040000

/* certificate verification flags codes */
#define BCTBX_CERTIFICATE_VERIFY_ALL_FLAGS				0xFFFFFFFF
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_EXPIRED		0x01  /**< The certificate validity has expired. */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_REVOKED		0x02  /**< The certificate has been revoked (is on a CRL). */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_CN_MISMATCH	0x04  /**< The certificate Common Name (CN) does not match with the expected CN. */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_NOT_TRUSTED	0x08  /**< The certificate is not correctly signed by the trusted CA. */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_MISSING		0x10  /**< Certificate was missing. */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_SKIP_VERIFY	0x20  /**< Certificate verification was skipped. */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_OTHER			0x0100  /**< Other reason (can be used by verify callback) */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_FUTURE			0x0200  /**< The certificate validity starts in the future. */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_KEY_USAGE      0x0400  /**< Usage does not match the keyUsage extension. */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_EXT_KEY_USAGE  0x0800  /**< Usage does not match the extendedKeyUsage extension. */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_NS_CERT_TYPE   0x1000  /**< Usage does not match the nsCertType extension. */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_MD         0x2000  /**< The certificate is signed with an unacceptable hash. */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_PK         0x4000  /**< The certificate is signed with an unacceptable PK alg (eg RSA vs ECDSA). */
#define BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_KEY        0x8000  /**< The certificate is signed with an unacceptable key (eg bad curve, RSA too short). */

#define BCTBX_CERTIFICATE_VERIFY_BADCRL_FUTURE			0x10000  /**< The CRL is from the future */
#define BCTBX_CERTIFICATE_VERIFY_BADCRL_NOT_TRUSTED		0x20000  /**< CRL is not correctly signed by the trusted CA. */
#define BCTBX_CERTIFICATE_VERIFY_BADCRL_EXPIRED			0x40000  /**< CRL is expired. */
#define BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_MD          0x80000  /**< The CRL is signed with an unacceptable hash. */
#define BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_PK          0x100000  /**< The CRL is signed with an unacceptable PK alg (eg RSA vs ECDSA). */
#define BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_KEY         0x200000  /**< The CRL is signed with an unacceptable key (eg bad curve, RSA too short). */


/* Hash functions type */
typedef enum bctbx_md_type {
	BCTBX_MD_UNDEFINED,
	BCTBX_MD_SHA1,
	BCTBX_MD_SHA224,
	BCTBX_MD_SHA256,
	BCTBX_MD_SHA384,
	BCTBX_MD_SHA512} bctbx_md_type_t;

/* Dtls srtp protection profile */
typedef enum bctbx_srtp_profile {
	BCTBX_SRTP_UNDEFINED,
	BCTBX_SRTP_AES128_CM_HMAC_SHA1_80,
	BCTBX_SRTP_AES128_CM_HMAC_SHA1_32,
	BCTBX_SRTP_NULL_HMAC_SHA1_80,
	BCTBX_SRTP_NULL_HMAC_SHA1_32
} bctbx_dtls_srtp_profile_t;

typedef enum bctbx_type_implementation {
	BCTBX_POLARSSL,
	BCTBX_POLARSSL1_2,
	BCTBX_MBEDTLS
} bctbx_type_implementation_t;

#ifdef __cplusplus
extern "C"{
#endif

/*****************************************************************************/
/****** Utils                                                           ******/
/*****************************************************************************/
/**
 * @brief Return a string translation of an error code
 * PolarSSL and mbedTLS error codes are on 16 bits always negatives, and these are forwarded to the crypto library error to string translation
 * Specific bctoolbox error code are on 32 bits, all in the form -0x7XXX XXXX
 * Output string is truncated if the buffer is too small and always include a null termination char
 *
 * @param[in]		error_code		The error code
 * @param[in/out]	buffer			Buffer to place error string representation
 * @param[in]		buffer_length	Size of the buffer in bytes.
 */
BCTBX_PUBLIC void bctbx_strerror(int32_t error_code, char *buffer, size_t buffer_length);

/**
 * @brief Encode a buffer into base64 format
 * @param[out]		output			base64 encoded buffer
 * @param[in/out]	output_length	output buffer max size and actual size of buffer after encoding
 * @param[in]		input			source plain buffer
 * @param[in]		input_length	Length in bytes of plain buffer to be encoded
 *
 * @return 0 if success or BCTBX_ERROR_OUTPUT_BUFFER_TOO_SMALL if the output buffer cannot contain the encoded data
 *
 * @note If the function is called with *output_length=0, set the requested buffer size in output_length
 */
BCTBX_PUBLIC int32_t bctbx_base64_encode(unsigned char *output, size_t *output_length, const unsigned char *input, size_t input_length);

/**
 * @brief Decode a base64 formatted buffer.
 * @param[out]		output			plain buffer
 * @param[in/out]	output_length	output buffer max size and actual size of buffer after decoding
 * @param[in]		input			source base64 encoded buffer
 * @param[in]		input_length	Length in bytes of base64 buffer to be decoded
 *
 * @return 0 if success, BCTBX_ERROR_OUTPUT_BUFFER_TOO_SMALL if the output buffer cannot contain the decoded data
 * or BCTBX_ERROR_INVALID_BASE64_INPUT if encoded buffer was incorrect base64 data
 *
 * @note If the function is called with *output_length=0, set the requested buffer size in output_length
 */
BCTBX_PUBLIC int32_t bctbx_base64_decode(unsigned char *output, size_t *output_length, const unsigned char *input, size_t input_length);

/*****************************************************************************/
/****** Random Number Generation                                        ******/
/*****************************************************************************/
/** @brief An opaque structure used to store RNG context
 * Instanciate pointers only and allocate them using the bctbx_rng_context_new() function
 */
typedef struct bctbx_rng_context_struct bctbx_rng_context_t;

/**
 * @brief Create and initialise the Random Number Generator context
 * @return a pointer to the RNG context
 */
BCTBX_PUBLIC bctbx_rng_context_t *bctbx_rng_context_new(void);

/**
 * @brief Get some random material
 *
 * @param[in/out]	context			The RNG context to be used
 * @param[out]		output			A destination buffer for the random material generated
 * @param[in]		output_length	Size in bytes of the output buffer and requested random material
 *
 * @return 0 on success
 */
BCTBX_PUBLIC int32_t bctbx_rng_get(bctbx_rng_context_t *context, unsigned char*output, size_t output_length);

/**
 * @brief Clear the RNG context and free internal buffer
 *
 * @param[in]	context		The RNG context to clear
 */
BCTBX_PUBLIC void bctbx_rng_context_free(bctbx_rng_context_t *context);

/*****************************************************************************/
/***** Signing key                                                       *****/
/*****************************************************************************/
/** @brief An opaque structure used to store the signing key context
 * Instanciate pointers only and allocate them using the bctbx_signing_key_new() function
 */
typedef struct bctbx_signing_key_struct bctbx_signing_key_t;

/**
 * @brief Create and initialise a signing key context
 * @return a pointer to the signing key context
 */
BCTBX_PUBLIC bctbx_signing_key_t *bctbx_signing_key_new(void);

/**
 * @brief Clear the signing key context and free internal buffer
 *
 * @param[in]	key		The signing key context to clear
 */
BCTBX_PUBLIC void  bctbx_signing_key_free(bctbx_signing_key_t *key);

/**
 * @brief	Write the key in a buffer as a PEM string
 *
 * @param[in]	key		The signing key to be extracted in PEM format
 *
 * @return a pointer to a null terminated string containing the key in PEM format. This buffer must then be freed by caller. NULL on failure.
 */
BCTBX_PUBLIC char *bctbx_signing_key_get_pem(bctbx_signing_key_t *key);

/**
 * @brief Parse signing key in PEM format from a null terminated string buffer
 *
 * @param[in/out]	key				An already initialised signing key context
 * @param[in]		buffer			The input buffer containing a PEM format key in a null terminated string
 * @param[in]		buffer_length	The length of input buffer, including the NULL termination char
 * @param[in]		password		Password for decryption(may be NULL)
 * @param[in]		passzord_length	size of password
 *
 * @return 0 on success
 */
BCTBX_PUBLIC int32_t bctbx_signing_key_parse(bctbx_signing_key_t *key, const char *buffer, size_t buffer_length, const unsigned char *password, size_t password_length);

/**
 * @brief Parse signing key from a file
 *
 * @param[in/out]	key				An already initialised signing key context
 * @param[in]		path			filename to read the key from
 * @param[in]		password		Password for decryption(may be NULL)
 *
 * @return 0 on success
 */
BCTBX_PUBLIC int32_t bctbx_signing_key_parse_file(bctbx_signing_key_t *key, const char *path, const char *password);

/*****************************************************************************/
/***** X509 Certificate                                                  *****/
/*****************************************************************************/
/** @brief An opaque structure used to store the certificate context
 * Instanciate pointers only and allocate them using the bctbx_x509_certificate_new() function
 */
typedef struct bctbx_x509_certificate_struct bctbx_x509_certificate_t;

/**
 * @brief Create and initialise a x509 certificate context
 * @return a pointer to the certificate context
 */
BCTBX_PUBLIC bctbx_x509_certificate_t *bctbx_x509_certificate_new(void);

/**
 * @brief Clear the certificate context and free internal buffer
 *
 * @param[in]	cert		The x509 certificate context to clear
 */
BCTBX_PUBLIC void  bctbx_x509_certificate_free(bctbx_x509_certificate_t *cert);

/**
 * @brief	Write the certificate in a buffer as a PEM string
 *
 * @param[in]	cert	The certificate to be extracted in PEM format
 *
 * @return a pointer to a null terminated string containing the certificate in PEM format. This buffer must then be freed by caller. NULL on failure.
 */
BCTBX_PUBLIC char *bctbx_x509_certificates_chain_get_pem(bctbx_x509_certificate_t *cert);

/**
 * @brief	Return an informational string about the certificate
 *
 * @param[out]	buf	Buffer to receive the output
 * @param[in]	size	Maximum output buffer size
 * @param[in]	prefix	A line prefix
 * @param[in]	cert	The x509 certificate
 *
 * @return	The length of the string written or a negative error code
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_get_info_string(char *buf, size_t size, const char *prefix, const bctbx_x509_certificate_t *cert);

/**
 * @brief Parse an x509 certificate in PEM format from a null terminated string buffer
 *
 * @param[in/out]	cert		An already initialised x509 certificate context
 * @param[in]		buffer		The input buffer containing a PEM format certificate in a null terminated string
 * @param[in]		buffer_length	The length of input buffer, including the NULL termination char
 *
 * @return	0 on success, negative error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_parse(bctbx_x509_certificate_t *cert, const char *buffer, size_t buffer_length);

/**
 * @brief Load one or more certificates and add them to the chained list
 *
 * @param[in/out]	cert	points to the start of the chain, can be an empty initialised certificate context
 * @param[in]		path	filename to read the certificate from
 *
 * @return	0 on success, negative error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_parse_file(bctbx_x509_certificate_t *cert, const char *path);

/**
 * @brief Load one or more certificates files from a path and add them to the chained list
 *
 * @param[in/out]	cert	points to the start of the chain, can be an empty initialised certificate context
 * @param[in]		path	directory to read certicates files from
 *
 * @return	0 on success, negative error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_parse_path(bctbx_x509_certificate_t *cert, const char *path);

/**
 * @brief Get the length in bytes of a certifcate chain in DER format
 *
 * @param[in]	cert	The certificate chain
 *
 * @return	The length in bytes of the certificate buffer in DER format, 0 if no certificate found
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_get_der_length(bctbx_x509_certificate_t *cert);

/**
 * @brief	Get the certificate in DER format in a null terminated string
 *
 * @param[in]		cert		The certificate chain
 * @param[in/out]	buffer		The buffer to hold the certificate
 * @param[in]		buffer_length	Maximum output buffer size
 *
 * @return 0 on success, negative error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_get_der(bctbx_x509_certificate_t *cert, unsigned char *buffer, size_t buffer_length);

/**
 * @brief Store the certificate subject DN in printable form into buf
 *
 * @param[in]		cert		The x509 certificate
 * @param[in/out]	dn		A buffer to store the DN string
 * @param[in]		dn_length	Maximum size to be written in buffer
 *
 * @return The length of the string written (not including the terminated nul byte), or a negative error code
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_get_subject_dn(const bctbx_x509_certificate_t *cert, char *dn, size_t dn_length);

/**
 * @brief Obtain the certificate subjects (all subjectAltName URIS and DNS + subject CN)
 *
 * @param[in]		cert		The x509 certificate
 * @return a list of allocated strings (char*), to be freed with bctbx_free()
 */
BCTBX_PUBLIC bctbx_list_t *bctbx_x509_certificate_get_subjects(const bctbx_x509_certificate_t *cert);

/**
 * @brief Generate certificate fingerprint (hash of the DER format certificate) hexadecimal format in a null terminated string
 *
 * @param[in]		cert			The x509 certificate
 * @param[in/out]	fingerprint		The buffer to hold the fingerprint(null terminated string in hexadecimal)
 * @param[in]		fingerprint_length	Maximum length of the fingerprint buffer
 * @param[in]		hash_algorithm		set to BCTBX_MD_UNDEFINED to use the hash used in certificate signature(recommended)
 *						or specify an other hash algorithm(BCTBX_MD_SHA1, BCTBX_MD_SHA224, BCTBX_MD_SHA256, BCTBX_MD_SHA384, BCTBX_MD_SHA512)
 * @return length of written on success, negative error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_get_fingerprint(const bctbx_x509_certificate_t *cert, char *fingerprint, size_t fingerprint_length, bctbx_md_type_t hash_algorithm);

/**
 * @brief Retrieve the certificate signature hash function
 *
 * @param[in]	cert		The x509 certificate
 * @param[out]	hash_algorithm	The hash algorithm used for the certificate signature or BCTBX_MD_UNDEFINED if unable to retrieve it
 *
 * @return 0 on success, negative error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_get_signature_hash_function(const bctbx_x509_certificate_t *certificate, bctbx_md_type_t *hash_algorithm);

/**
 * @brief Generate a self-signed certificate using RSA 3072 bits signature algorithm
 *
 * @param[in]		subject		The certificate subject
 * @param[in/out]	certificate	An empty intialised certificate pointer to hold the generated certificate
 * @param[in/out]	pkey		An empty initialised signing key pointer to hold the key generated and used to sign the certificate (RSA 3072 bits)
 * @param[out]		pem		If not null, a buffer to hold a PEM string of the certificate and key
 * @param[in]		pem_length	pem buffer length
 *
 * @return 0 on success, negative error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_generate_selfsigned(const char *subject, bctbx_x509_certificate_t *certificate, bctbx_signing_key_t *pkey, char *pem, size_t pem_length);

/**
 * @brief Convert underlying crypto library certificate flags into a printable string
 *
 * @param[out]	buffer		a buffer to hold the output string
 * @param[in]	buffer_size	maximum buffer size
 * @param[in]	flags		The flags from the underlying crypto library, provided in callback functions
 *
 * @return 0 on success, negative error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_flags_to_string(char *buffer, size_t buffer_size, uint32_t flags);

/**
 * @brief Set a certificate flags (using underlying crypto library defines)
 *
 * @param[in/out]	flags		The certificate flags holder directly provided by crypto library in a callback function
 * @param[in]		flags_to_set	Flags to be set, bctoolbox defines
 *
 * @return 0 on success, negative error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_set_flag(uint32_t *flags, uint32_t flags_to_set);

/**
 * @brief convert certificate flags from underlying crypto library defines to bctoolbox ones
 *
 * @param[in]	flags	certificate flags provided by the crypto library in a callback function
 *
 * @return same flag but using the bctoolbox API definitions
 */
BCTBX_PUBLIC uint32_t bctbx_x509_certificate_remap_flag(uint32_t flags);

/**
 * @brief Unset a certificate flags (using underlying crypto library defines)
 *
 * @param[in/out]	flags		The certificate flags holder directly provided by crypto library in a callback function
 * @param[in]		flags_to_set	Flags to be unset, bctoolbox defines
 *
 * @return 0 on success, negative error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_x509_certificate_unset_flag(uint32_t *flags, uint32_t flags_to_unset);


/*****************************************************************************/
/***** SSL                                                               *****/
/*****************************************************************************/
typedef struct bctbx_ssl_context_struct bctbx_ssl_context_t;
typedef struct bctbx_ssl_config_struct bctbx_ssl_config_t;
BCTBX_PUBLIC bctbx_type_implementation_t bctbx_ssl_get_implementation_type(void);
BCTBX_PUBLIC bctbx_ssl_context_t *bctbx_ssl_context_new(void);
BCTBX_PUBLIC void bctbx_ssl_context_free(bctbx_ssl_context_t *ssl_ctx);
BCTBX_PUBLIC int32_t bctbx_ssl_context_setup(bctbx_ssl_context_t *ssl_ctx, bctbx_ssl_config_t *ssl_config);

BCTBX_PUBLIC int32_t bctbx_ssl_close_notify(bctbx_ssl_context_t *ssl_ctx);
BCTBX_PUBLIC int32_t bctbx_ssl_session_reset(bctbx_ssl_context_t *ssl_ctx);
BCTBX_PUBLIC int32_t bctbx_ssl_read(bctbx_ssl_context_t *ssl_ctx, unsigned char *buf, size_t buf_length);
BCTBX_PUBLIC int32_t bctbx_ssl_write(bctbx_ssl_context_t *ssl_ctx, const unsigned char *buf, size_t buf_length);
BCTBX_PUBLIC int32_t bctbx_ssl_set_hostname(bctbx_ssl_context_t *ssl_ctx, const char *hostname);
BCTBX_PUBLIC int32_t bctbx_ssl_handshake(bctbx_ssl_context_t *ssl_ctx);
BCTBX_PUBLIC int32_t bctbx_ssl_set_hs_own_cert(bctbx_ssl_context_t *ssl_ctx, bctbx_x509_certificate_t *cert, bctbx_signing_key_t *key);
BCTBX_PUBLIC void bctbx_ssl_set_io_callbacks(bctbx_ssl_context_t *ssl_ctx, void *callback_data,
		int(*callback_send_function)(void *, const unsigned char *, size_t), /* callbacks args are: callback data, data buffer to be send, size of data buffer */
		int(*callback_recv_function)(void *, unsigned char *, size_t)); /* args: callback data, data buffer to be read, size of data buffer */
BCTBX_PUBLIC const bctbx_x509_certificate_t *bctbx_ssl_get_peer_certificate(bctbx_ssl_context_t *ssl_ctx);
BCTBX_PUBLIC const char *bctbx_ssl_get_ciphersuite(bctbx_ssl_context_t *ssl_ctx);
BCTBX_PUBLIC int bctbx_ssl_get_ciphersuite_id(const char* ciphersuite);
BCTBX_PUBLIC const char *bctbx_ssl_get_version(bctbx_ssl_context_t *ssl_ctx);
	
BCTBX_PUBLIC bctbx_ssl_config_t *bctbx_ssl_config_new(void);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_crypto_library_config(bctbx_ssl_config_t *ssl_config, void *internal_config);
BCTBX_PUBLIC void bctbx_ssl_config_free(bctbx_ssl_config_t *ssl_config);
BCTBX_PUBLIC void *bctbx_ssl_config_get_private_config(bctbx_ssl_config_t *ssl_config);
BCTBX_PUBLIC int32_t bctbx_ssl_config_defaults(bctbx_ssl_config_t *ssl_config, int endpoint, int transport);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_endpoint(bctbx_ssl_config_t *ssl_config, int endpoint);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_transport (bctbx_ssl_config_t *ssl_config, int transport);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_authmode(bctbx_ssl_config_t *ssl_config, int authmode);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_rng(bctbx_ssl_config_t *ssl_config, int(*rng_function)(void *, unsigned char *, size_t), void *rng_context);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_callback_verify(bctbx_ssl_config_t *ssl_config, int(*callback_function)(void *, bctbx_x509_certificate_t *, int, uint32_t *), void *callback_data);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_callback_cli_cert(bctbx_ssl_config_t *ssl_config, int(*callback_function)(void *, bctbx_ssl_context_t *, unsigned char *, size_t), void *callback_data);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_ca_chain(bctbx_ssl_config_t *ssl_config, bctbx_x509_certificate_t *ca_chain);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_own_cert(bctbx_ssl_config_t *ssl_config, bctbx_x509_certificate_t *cert, bctbx_signing_key_t *key);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_ciphersuites(bctbx_ssl_config_t *ssl_config,const int *ciphersuites);
	
/***** DTLS-SRTP functions *****/
BCTBX_PUBLIC bctbx_dtls_srtp_profile_t bctbx_ssl_get_dtls_srtp_protection_profile(bctbx_ssl_context_t *ssl_ctx);
BCTBX_PUBLIC int32_t bctbx_ssl_config_set_dtls_srtp_protection_profiles(bctbx_ssl_config_t *ssl_config, const bctbx_dtls_srtp_profile_t *profiles, size_t profiles_number);
BCTBX_PUBLIC int32_t bctbx_ssl_get_dtls_srtp_key_material(bctbx_ssl_context_t *ssl_ctx, char *output, size_t *output_length);
BCTBX_PUBLIC uint8_t bctbx_dtls_srtp_supported(void);
BCTBX_PUBLIC void bctbx_ssl_set_mtu(bctbx_ssl_context_t *ssl_ctx, uint16_t mtu);


/*****************************************************************************/
/***** Key exchanges defined algorithms                                  *****/
/*****************************************************************************/
/**
 * @brief Return a 32 bits unsigned integer, each bit set to one matches an
 * available key agreement algorithm as defined in bctoolbox/include/crypto.h
 *
 * @return An unsigned integer of 32 flags matching key agreement algos
 */
BCTBX_PUBLIC uint32_t bctbx_key_agreement_algo_list(void);

/*****************************************************************************/
/***** Diffie-Hellman-Merkle key exchange                                *****/
/*****************************************************************************/
/**
 * @brief Context for the Diffie-Hellman-Merkle key exchange
 *	Use RFC3526 values for G and P
 */
typedef struct bctbx_DHMContext_struct {
	uint8_t algo; /**< Algorithm used for the key exchange mapped to an int: BCTBX_DHM_2048, BCTBX_DHM_3072 */
	uint16_t primeLength; /**< Prime number length in bytes(256 or 384)*/
	uint8_t *secret; /**< the random secret (X), this field may not be used if the crypto module implementation already store this value in his context */
	uint8_t secretLength; /**< in bytes */
	uint8_t *key; /**< the key exchanged (G^Y)^X mod P */
	uint8_t *self; /**< this side of the public exchange G^X mod P */
	uint8_t *peer; /**< the other side of the public exchange G^Y mod P */
	void *cryptoModuleData; /**< a context needed by the crypto implementation */
}bctbx_DHMContext_t;

/**
 *
 * @brief Create a context for the DHM key exchange
 * 	This function will also instantiate the context needed by the actual implementation of the crypto module
 *
 * @param[in] DHMAlgo		The algorithm type(BCTBX_DHM_2048 or BCTBX_DHM_3072)
 * @param[in] secretLength	The length in byte of the random secret(X).
 *
 * @return The initialised context for the DHM calculation(must then be freed calling the destroyDHMContext function), NULL on error
 *
 */
BCTBX_PUBLIC bctbx_DHMContext_t *bctbx_CreateDHMContext(uint8_t DHMAlgo, uint8_t secretLength);

/**
 *
 * @brief Generate the private secret X and compute the public value G^X mod P
 * G, P and X length have been set by previous call to DHM_CreateDHMContext
 *
 * @param[in/out] 	context		DHM context, will store the public value in ->self after this call
 * @param[in] 		rngFunction	pointer to a random number generator used to create the secret X
 * @param[in]		rngContext	pointer to the rng context if neeeded
 *
 */
BCTBX_PUBLIC void bctbx_DHMCreatePublic(bctbx_DHMContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext);

/**
 *
 * @brief Compute the secret key G^X^Y mod p
 * G^X mod P has been computed in previous call to DHMCreatePublic
 * G^Y mod P must have been set in context->peer
 *
 * @param[in/out] 	context		Read the public values from context, export the key to context->key
 * @param[in]		rngFunction	Pointer to a random number generation function, used for blinding countermeasure, may be NULL
 * @param[in]		rngContext	Pointer to the RNG function context
 *
 */
BCTBX_PUBLIC void bctbx_DHMComputeSecret(bctbx_DHMContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext);

/**
 *
 * @brief Clean DHM context. Secret and key, if present, are erased from memory(set to 0)
 *
 * @param	context	The context to deallocate
 *
 */
BCTBX_PUBLIC void bctbx_DestroyDHMContext(bctbx_DHMContext_t *context);


/*****************************************************************************/
/***** Elliptic Curve Diffie-Hellman-Merkle key exchange                 *****/
/*****************************************************************************/
/**
 *
 * @brief return TRUE if the Elliptic Curve Cryptography is available
 */
BCTBX_PUBLIC int bctbx_crypto_have_ecc(void);

/**
 * @brief Context for the EC Diffie-Hellman-Merkle key exchange on curve 25519 and 448
 *	Use RFC7748 for base points values
 */
typedef struct bctbx_ECDHContext_struct {
	uint8_t algo; /**< Algorithm used for the key exchange mapped to an int: BCTBX_ECDH_X25519, BCTBX_ECDH_X448 */
	uint16_t pointCoordinateLength; /**< length in bytes of the point u-coordinate, can be 32 or 56 */
	uint8_t *secret; /**< the random secret (scalar) used to compute public key and shared secret */
	uint8_t secretLength; /**< in bytes, usually the same than pointCoordinateLength */
	uint8_t *sharedSecret; /**< the key exchanged scalar multiplation of MULT(Self Secret, MULT(Peer Secret, BasePoint)), u-coordinate */
	uint8_t *selfPublic; /**< this side of the public exchange: MULT(self secret, BasePoint), u-coordinate */
	uint8_t *peerPublic; /**< the other side of the public exchange: MULT(peer secret, BasePoint), u-coordinate */
	void *cryptoModuleData; /**< a context needed by the underlying crypto implementation - note if in use, most of the previous buffers could be store in it actually */
}bctbx_ECDHContext_t;

/**
 *
 * @brief Create a context for the ECDH key exchange
 *
 * @param[in] ECDHAlgo		The algorithm type(BCTBX_ECDH_X25519 or BCTBX_ECDH_X448)
 *
 * @return The initialised context for the ECDH calculation(must then be freed calling the destroyECDHContext function), NULL on error
 *
 */
BCTBX_PUBLIC bctbx_ECDHContext_t *bctbx_CreateECDHContext(const uint8_t ECDHAlgo);

/**
 *
 * @brief Generate the private secret scalar and compute the public key MULT(scalar, BasePoint)
 *
 * @param[in/out] 	context		ECDH context, will store the public value in ->selfPublic after this call, and secret in ->secret
 * @param[in] 		rngFunction	pointer to a random number generator used to create the secret
 * @param[in]		rngContext	pointer to the rng context if neeeded
 *
 */
BCTBX_PUBLIC void bctbx_ECDHCreateKeyPair(bctbx_ECDHContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext);

/**
 *
 * @brief	Set the given secret key in the ECDH context
 *
 * @param[in/out]	context		ECDH context, will store the given secret key if length is matching the pre-setted algo for this context
 * @param[in]		secret		The buffer holding the secret, is duplicated in the ECDH context
 * @param[in]		secretLength	Length of previous buffer, must match the algo type setted at context creation
 */
BCTBX_PUBLIC void bctbx_ECDHSetSecretKey(bctbx_ECDHContext_t *context, const uint8_t *secret, const size_t secretLength);

/**
 *
 * @brief	Set the given self public key in the ECDH context
 *		Warning: no check if it matches the private key value
 *
 * @param[in/out]	context			ECDH context, will store the given self public key if length is matching the pre-setted algo for this context
 * @param[in]		selfPublic		The buffer holding the self public key, is duplicated in the ECDH context
 * @param[in]		selfPublicLength	Length of previous buffer, must match the algo type setted at context creation
 */
BCTBX_PUBLIC void bctbx_ECDHSetSelfPublicKey(bctbx_ECDHContext_t *context, const uint8_t *selfPublic, const size_t selfPublicLength);

/**
 *
 * @brief	Set the given peer public key in the ECDH context
 *
 * @param[in/out]	context			ECDH context, will store the given peer public key if length is matching the pre-setted algo for this context
 * @param[in]		peerPublic		The buffer holding the peer public key, is duplicated in the ECDH context
 * @param[in]		peerPublicLength	Length of previous buffer, must match the algo type setted at context creation
 */
BCTBX_PUBLIC void bctbx_ECDHSetPeerPublicKey(bctbx_ECDHContext_t *context, const uint8_t *peerPublic, const size_t peerPublicLength);

/**
 *
 * @brief	Derive the public key from the secret setted in context and using preselected algo, following RFC7748
 *
 * @param[in/out]	context		The context holding algo setting and secret, used to store public key
 */
BCTBX_PUBLIC void bctbx_ECDHDerivePublicKey(bctbx_ECDHContext_t *context);

/**
 *
 * @brief Compute the shared secret MULT(secret, peer Public)
 * ->peerPublic, containing MULT(peerSecret, basePoint) must have been set before this call in context
 *
 * @param[in/out] 	context		Read the public values from context, export the key to context->sharedSecret
 * @param[in]		rngFunction	Pointer to a random number generation function, used for blinding countermeasure, may be NULL
 * @param[in]		rngContext	Pointer to the RNG function context
 *
 */
BCTBX_PUBLIC void bctbx_ECDHComputeSecret(bctbx_ECDHContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext);

/**
 *
 * @brief Clean ECDH context. Secret and key, if present, are erased from memory(set to 0)
 *
 * @param	context	The context to deallocate
 *
 */
BCTBX_PUBLIC void bctbx_DestroyECDHContext(bctbx_ECDHContext_t *context);

/*****************************************************************************/
/***** EdDSA: signature and verify using Elliptic curves                 *****/
/*****************************************************************************/
/**
 * @brief Context for the EdDSA using curves 25519 and 448
 */
typedef struct bctbx_EDDSAContext_struct {
	uint8_t algo; /**< Algorithm used for the key exchange mapped to an int: BCTBX_EDDSA_25519, BCTBX_EDDSA_448 */
	uint16_t pointCoordinateLength; /**< length in bytes of a serialised point coordinate, can be 32 or 57 */
	uint8_t *secretKey; /**< the random secret (scalar) used to compute public key and message signature, is the same length than a serialised point coordinate */
	uint8_t secretLength; /**< in bytes, usually the same than pointCoordinateLength */
	uint8_t *publicKey; /**< MULT(HASH(secretKey), BasePoint), serialised coordinate */
	void *cryptoModuleData; /**< a context needed by the underlying crypto implementation - note if in use, most of the previous buffers could be store in it actually */
}bctbx_EDDSAContext_t;

/**
 *
 * @brief Create a context for the EdDSA sign/verify
 *
 * @param[in] EDDSAAlgo		The algorithm type(BCTBX_EDDSA_25519 or BCTBX_EDDSA_448)
 *
 * @return The initialised context for the EDDSA calculation(must then be freed calling the destroyEDDSAContext function), NULL on error
 *
 */
BCTBX_PUBLIC bctbx_EDDSAContext_t *bctbx_CreateEDDSAContext(uint8_t EDDSAAlgo);

/**
 *
 * @brief Generate the private secret scalar and compute the public MULT(scalar, BasePoint)
 *
 * @param[in/out] 	context		EDDSA context, will store the public value in ->publicKey after this call, and secret in ->secretKey
 * @param[in] 		rngFunction	pointer to a random number generator used to create the secret
 * @param[in]		rngContext	pointer to the rng context if neeeded
 *
 */
BCTBX_PUBLIC void bctbx_EDDSACreateKeyPair(bctbx_EDDSAContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext);

/**
 *
 * @brief Using the private secret scalar already set in context, compute the public MULT(scalar, BasePoint)
 *
 * @param[in/out] 	context		EDDSA context, will store the public value in ->publicKey after this call, already have secret in ->secretKey
 *
 */
BCTBX_PUBLIC void bctbx_EDDSADerivePublicKey(bctbx_EDDSAContext_t *context);

/**
 *
 * @brief Clean ECDH context. Secret and key, if present, are erased from memory(set to 0)
 *
 * @param	context	The context to deallocate
 *
 */
BCTBX_PUBLIC void bctbx_DestroyEDDSAContext(bctbx_EDDSAContext_t *context);

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
BCTBX_PUBLIC void bctbx_EDDSA_sign(bctbx_EDDSAContext_t *context, const uint8_t *message, const size_t messageLength, const uint8_t *AssociatedData, const uint8_t associatedDataLength, uint8_t *signature, size_t *signatureLength);

/**
 *
 * @brief Set a public key in a EDDSA context to be used to verify messages signature
 *
 * @param[in/out]	context		EDDSA context storing the algorithm to use(ed448 or ed25519)
 * @param[in]		publicKey	The public to store in context
 * @param[in]		publicKeyLength	The length of previous buffer
 */
BCTBX_PUBLIC void bctbx_EDDSA_setPublicKey(bctbx_EDDSAContext_t *context, const uint8_t *publicKey, const size_t publicKeyLength);

/**
 *
 * @brief Set a private key in a EDDSA context to be used to sign message
 *
 * @param[in/out]	context		EDDSA context storing the algorithm to use(ed448 or ed25519)
 * @param[in]		secretKey	The secret to store in context
 * @param[in]		secretKeyLength	The length of previous buffer
 */
BCTBX_PUBLIC void bctbx_EDDSA_setSecretKey(bctbx_EDDSAContext_t *context, const uint8_t *secretKey, const size_t secretKeyLength);

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
BCTBX_PUBLIC int bctbx_EDDSA_verify(bctbx_EDDSAContext_t *context, const uint8_t *message, size_t messageLength, const uint8_t *associatedData, const uint8_t associatedDataLength, const uint8_t *signature, size_t signatureLength);


/**
 *
 * @brief Convert a EDDSA private key to a ECDH private key
 *      pass the EDDSA private key through the hash function used in EdDSA
 *
 * @param[in]	ed	Context holding the current private key to convert
 * @param[out]	x	Context to store the private key for x25519 key exchange
*/
BCTBX_PUBLIC void bctbx_EDDSA_ECDH_privateKeyConversion(const bctbx_EDDSAContext_t *ed, bctbx_ECDHContext_t *x);

#define BCTBX_ECDH_ISPEER	0
#define BCTBX_ECDH_ISSELF	1

/**
 *
 * @brief Convert a EDDSA public key to a ECDH public key
 * 	point conversion : montgomeryX = (edwardsY + 1)*inverse(1 - edwardsY) mod p
 *
 * @param[in]	ed	Context holding the current public key to convert
 * @param[out]	x	Context to store the public key for x25519 key exchange
 * @param[in]	isSelf	Flag to decide where to store the public key in context: BCTBX_ECDH_ISPEER or BCTBX_ECDH_ISPEER
*/
BCTBX_PUBLIC void bctbx_EDDSA_ECDH_publicKeyConversion(const bctbx_EDDSAContext_t *ed, bctbx_ECDHContext_t *x, uint8_t isSelf);

/*****************************************************************************/
/***** Hashing                                                           *****/
/*****************************************************************************/
/*
 * @brief SHA512 wrapper
 * @param[in]	input 		Input data buffer
 * @param[in]	inputLength	Input data length in bytes
 * @param[in]	hashLength	Length of output required in bytes, Output is truncated to the hashLength left bytes. 64 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
BCTBX_PUBLIC void bctbx_sha512(const uint8_t *input,
		size_t inputLength,
		uint8_t hashLength,
		uint8_t *output);

/*
 * @brief SHA384 wrapper
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length in bytes
 * @param[in]	hashLength	Length of output required in bytes, Output is truncated to the hashLength left bytes. 48 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
BCTBX_PUBLIC void bctbx_sha384(const uint8_t *input,
		size_t inputLength,
		uint8_t hashLength,
		uint8_t *output);
/**
 * @brief SHA256 wrapper
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length in bytes
 * @param[in]	hmacLength	Length of output required in bytes, SHA256 output is truncated to the hashLength left bytes. 32 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
BCTBX_PUBLIC void bctbx_sha256(const uint8_t *input,
		size_t inputLength,
		uint8_t hashLength,
		uint8_t *output);

/*
 * @brief HMAC-SHA512 wrapper
 * @param[in] 	key		HMAC secret key
 * @param[in] 	keyLength	HMAC key length
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length
 * @param[in]	hmacLength	Length of output required in bytes, HMAC output is truncated to the hmacLength left bytes. 64 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
BCTBX_PUBLIC void bctbx_hmacSha512(const uint8_t *key,
		size_t keyLength,
		const uint8_t *input,
		size_t inputLength,
		uint8_t hmacLength,
		uint8_t *output);

/**
 * @brief HMAC-SHA384 wrapper
 * @param[in] 	key		HMAC secret key
 * @param[in] 	keyLength	HMAC key length in bytes
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length in bytes
 * @param[in]	hmacLength	Length of output required in bytes, HMAC output is truncated to the hmacLength left bytes. 48 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
BCTBX_PUBLIC void bctbx_hmacSha384(const uint8_t *key,
		size_t keyLength,
		const uint8_t *input,
		size_t inputLength,
		uint8_t hmacLength,
		uint8_t *output);

/**
 * @brief HMAC-SHA256 wrapper
 * @param[in] 	key		HMAC secret key
 * @param[in] 	keyLength	HMAC key length in bytes
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length in bytes
 * @param[in]	hmacLength	Length of output required in bytes, HMAC output is truncated to the hmacLength left bytes. 32 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
BCTBX_PUBLIC void bctbx_hmacSha256(const uint8_t *key,
		size_t keyLength,
		const uint8_t *input,
		size_t inputLength,
		uint8_t hmacLength,
		uint8_t *output);

/**
 * @brief HMAC-SHA1 wrapper
 * @param[in] 	key			HMAC secret key
 * @param[in] 	keyLength	HMAC key length
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length
 * @param[in]	hmacLength	Length of output required in bytes, HMAC output is truncated to the hmacLength left bytes. 20 bytes maximum
 * @param[out]	output		Output data buffer
 *
 */
BCTBX_PUBLIC void bctbx_hmacSha1(const uint8_t *key,
		size_t keyLength,
		const uint8_t *input,
		size_t inputLength,
		uint8_t hmacLength,
		uint8_t *output);

/**
 * @brief MD5 wrapper
 * output = md5(input)
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length in bytes
 * @param[out]	output		Output data buffer.
 *
 */
BCTBX_PUBLIC void bctbx_md5(const uint8_t *input,
		size_t inputLength,
		uint8_t output[16]);


/*****************************************************************************/
/***** Encryption/Decryption                                             *****/
/*****************************************************************************/
typedef struct bctbx_aes_gcm_context_struct bctbx_aes_gcm_context_t;
/**
 * @Brief AES-GCM encrypt and tag buffer
 *
 * @param[in]	key							Encryption key
 * @param[in]	keyLength					Key buffer length, in bytes, must be 16,24 or 32
 * @param[in]	plainText					buffer to be encrypted
 * @param[in]	plainTextLength				Length in bytes of buffer to be encrypted
 * @param[in]	authenticatedData			Buffer holding additional data to be used in tag computation
 * @param[in]	authenticatedDataLength		Additional data length in bytes
 * @param[in]	initializationVector		Buffer holding the initialisation vector
 * @param[in]	initializationVectorLength	Initialisation vector length in bytes
 * @param[out]	tag							Buffer holding the generated tag
 * @param[in]	tagLength					Requested length for the generated tag
 * @param[out]	output						Buffer holding the output, shall be at least the length of plainText buffer
 *
 * @return 0 on success, crypto library error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_aes_gcm_encrypt_and_tag(const uint8_t *key, size_t keyLength,
		const uint8_t *plainText, size_t plainTextLength,
		const uint8_t *authenticatedData, size_t authenticatedDataLength,
		const uint8_t *initializationVector, size_t initializationVectorLength,
		uint8_t *tag, size_t tagLength,
		uint8_t *output);

/**
 * @Brief AES-GCM decrypt, compute authentication tag and compare it to the one provided
 *
 * @param[in]	key							Encryption key
 * @param[in]	keyLength					Key buffer length, in bytes, must be 16,24 or 32
 * @param[in]	cipherText					Buffer to be decrypted
 * @param[in]	cipherTextLength			Length in bytes of buffer to be decrypted
 * @param[in]	authenticatedData			Buffer holding additional data to be used in auth tag computation
 * @param[in]	authenticatedDataLength		Additional data length in bytes
 * @param[in]	initializationVector		Buffer holding the initialisation vector
 * @param[in]	initializationVectorLength	Initialisation vector length in bytes
 * @param[in]	tag							Buffer holding the authentication tag
 * @param[in]	tagLength					Length in bytes for the authentication tag
 * @param[out]	output						Buffer holding the output, shall be at least the length of cipherText buffer
 *
 * @return 0 on succes, BCTBX_ERROR_AUTHENTICATION_FAILED if tag doesn't match or crypto library error code
 */
BCTBX_PUBLIC int32_t bctbx_aes_gcm_decrypt_and_auth(const uint8_t *key, size_t keyLength,
		const uint8_t *cipherText, size_t cipherTextLength,
		const uint8_t *authenticatedData, size_t authenticatedDataLength,
		const uint8_t *initializationVector, size_t initializationVectorLength,
		const uint8_t *tag, size_t tagLength,
		uint8_t *output);

/**
 * @Brief create and initialise an AES-GCM encryption context
 *
 * @param[in]	key							encryption key
 * @param[in]	keyLength					key buffer length, in bytes, must be 16,24 or 32
 * @param[in]	authenticatedData			Buffer holding additional data to be used in tag computation
 * @param[in]	authenticatedDataLength		additional data length in bytes
 * @param[in]	initializationVector		Buffer holding the initialisation vector
 * @param[in]	initializationVectorLength	Initialisation vector length in bytes
 * @param[in]	mode						Operation mode : BCTBX_GCM_ENCRYPT or BCTBX_GCM_DECRYPT
 *
 * @return a pointer to the created context, to be freed using bctbx_aes_gcm_finish()
 */
BCTBX_PUBLIC bctbx_aes_gcm_context_t *bctbx_aes_gcm_context_new(const uint8_t *key, size_t keyLength,
		const uint8_t *authenticatedData, size_t authenticatedDataLength,
		const uint8_t *initializationVector, size_t initializationVectorLength,
		uint8_t mode);

/**
 * @Brief AES-GCM encrypt or decrypt a chunk of data
 *
 * @param[in/out]	context			a context already initialized using bctbx_aes_gcm_context_new
 * @param[in]		input			buffer holding the input data
 * @param[in]		inputLength		length of the input data
 * @param[out]		output			buffer to store the output data (same length as input one)
 *
 * @return 0 on success, crypto library error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_aes_gcm_process_chunk(bctbx_aes_gcm_context_t *context,
		const uint8_t *input, size_t inputLength,
		uint8_t *output);

/**
 * @Brief Conclude a AES-GCM encryption stream, generate tag if requested, free resources
 *
 * @param[in/out]	context			a context already initialized using bctbx_aes_gcm_context_new
 * @param[out]		tag				a buffer to hold the authentication tag. Can be NULL if tagLength is 0
 * @param[in]		tagLength		length of reqested authentication tag, max 16
 *
 * @return 0 on success, crypto library error code otherwise
 */
BCTBX_PUBLIC int32_t bctbx_aes_gcm_finish(bctbx_aes_gcm_context_t *context,
		uint8_t *tag, size_t tagLength);


/**
 * @brief Wrapper for AES-128 in CFB128 mode encryption
 * Both key and IV must be 16 bytes long
 *
 * @param[in]	key			encryption key, 128 bits long
 * @param[in]	IV			Initialisation vector, 128 bits long, is not modified by this function.
 * @param[in]	input		Input data buffer
 * @param[in]	inputLength	Input data length
 * @param[out]	output		Output data buffer
 *
 */
BCTBX_PUBLIC void bctbx_aes128CfbEncrypt(const uint8_t *key,
		const uint8_t *IV,
		const uint8_t *input,
		size_t inputLength,
		uint8_t *output);

/**
 * @brief Wrapper for AES-128 in CFB128 mode decryption
 * Both key and IV must be 16 bytes long
 *
 * @param[in]	key			decryption key, 128 bits long
 * @param[in]	IV			Initialisation vector, 128 bits long, is not modified by this function.
 * @param[in]	input		Input data buffer
 * @param[in]	inputLength	Input data length
 * @param[out]	output		Output data buffer
 *
 */
BCTBX_PUBLIC void bctbx_aes128CfbDecrypt(const uint8_t *key,
		const uint8_t *IV,
		const uint8_t *input,
		size_t inputLength,
		uint8_t *output);

/**
 * @brief Wrapper for AES-256 in CFB128 mode encryption
 * The key must be 32 bytes long and the IV must be 16 bytes long
 *
 * @param[in]	key			encryption key, 256 bits long
 * @param[in]	IV			Initialisation vector, 128 bits long, is not modified by this function.
 * @param[in]	input		Input data buffer
 * @param[in]	inputLength	Input data length
 * @param[out]	output		Output data buffer
 *
 */
BCTBX_PUBLIC void bctbx_aes256CfbEncrypt(const uint8_t *key,
		const uint8_t *IV,
		const uint8_t *input,
		size_t inputLength,
		uint8_t *output);

/**
 * @brief Wrapper for AES-256 in CFB128 mode decryption
 * The key must be 32 bytes long and the IV must be 16 bytes long
 *
 * @param[in]	key			decryption key, 256 bits long
 * @param[in]	IV			Initialisation vector, 128 bits long, is not modified by this function.
 * @param[in]	input		Input data buffer
 * @param[in]	inputLength	Input data length
 * @param[out]	output		Output data buffer
 *
 */
BCTBX_PUBLIC void bctbx_aes256CfbDecrypt(const uint8_t *key,
		const uint8_t *IV,
		const uint8_t *input,
		size_t inputLength,
		uint8_t *output);

/**
 * @brief encrypt the file in input buffer for linphone encrypted file transfer
 *
 * @param[in/out]	cryptoContext	a context already initialized using bctbx_aes_gcm_context_new
 * @param[in]		key		encryption key
 * @param[in]		length	buffer size
 * @param[in]		plain	buffer holding the input data
 * @param[out]		cipher	buffer to store the output data
 */
BCTBX_PUBLIC int bctbx_aes_gcm_encryptFile(void **cryptoContext, unsigned char *key, size_t length, char *plain, char *cipher);

/**
 * @brief decrypt the file in input buffer for linphone encrypted file transfer
 *
 * @param[in/out]	cryptoContext	a context already initialized using bctbx_aes_gcm_context_new
 * @param[in]		key		encryption key
 * @param[in]		length	buffer size
 * @param[out]		plain	buffer holding the output data
 * @param[int]		cipher	buffer to store the input data
 */
BCTBX_PUBLIC int bctbx_aes_gcm_decryptFile(void **cryptoContext, unsigned char *key, size_t length, char *plain, char *cipher);

/*****************************************************************************/
/***** Cleaning                                                          *****/
/*****************************************************************************/

/**
 * @brief force a buffer values to zero in a way that shall prevent the compiler from optimizing it out
 *
 * @param[in/out]	buffer	the buffer to be cleared
 * @param[in]		size	buffer size
 */
BCTBX_PUBLIC void bctbx_clean(void *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* BCTBX_CRYPTO_H */
