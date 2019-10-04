/*
crypto_mbedtls.c
Copyright (C) 2016  Belledonne Communications SARL

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

#include <stdlib.h>
#include <string.h>
#include "utils.h"

#include <mbedtls/ssl.h>
#include <mbedtls/timing.h>
#include <mbedtls/error.h>
#include <mbedtls/base64.h>
#include <mbedtls/version.h>
#include <mbedtls/pem.h>
#include <mbedtls/x509.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md5.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <mbedtls/gcm.h>

#if MBEDTLS_VERSION_NUMBER >= 0x02040000 // v2.4.0
#include <mbedtls/net_sockets.h>
#else
#include <mbedtls/net.h>
#endif

#include "bctoolbox/crypto.h"
#include "bctoolbox/logging.h"



static int bctbx_ssl_sendrecv_callback_return_remap(int32_t ret_code) {
	switch (ret_code) {
		case BCTBX_ERROR_NET_WANT_READ:
			return MBEDTLS_ERR_SSL_WANT_READ;
		case BCTBX_ERROR_NET_WANT_WRITE:
			return MBEDTLS_ERR_SSL_WANT_WRITE;
		case BCTBX_ERROR_NET_CONN_RESET:
			return MBEDTLS_ERR_NET_CONN_RESET;
		default:
			return (int)ret_code;
	}
}

void bctbx_strerror(int32_t error_code, char *buffer, size_t buffer_length) {
	if (error_code>0) {
		snprintf(buffer, buffer_length, "%s", "Invalid Error code");
		return ;
	}

	/* mbedtls error code are all negatived and bas smaller than 0x0000F000 */
	/* bctoolbox defined error codes are all in format -0x7XXXXXXX */
	if (-error_code<0x00010000) { /* it's a mbedtls error code */
		mbedtls_strerror(error_code, buffer, buffer_length);
		return;
	}

	snprintf(buffer, buffer_length, "%s [-0x%x]", "bctoolbox defined error code", -error_code);
	return;
}

int32_t bctbx_base64_encode(unsigned char *output, size_t *output_length, const unsigned char *input, size_t input_length) {
	size_t byte_written = 0;
	int ret = mbedtls_base64_encode(output, *output_length, &byte_written, input, input_length);
	*output_length = byte_written;
	if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
		return BCTBX_ERROR_OUTPUT_BUFFER_TOO_SMALL;
	}
	return ret;
}

int32_t bctbx_base64_decode(unsigned char *output, size_t *output_length, const unsigned char *input, size_t input_length) {
	size_t byte_written = 0;
	int ret = mbedtls_base64_decode(output, *output_length, &byte_written, input, input_length);
	*output_length = byte_written;
	if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
		return BCTBX_ERROR_OUTPUT_BUFFER_TOO_SMALL;
	}
	if (ret == MBEDTLS_ERR_BASE64_INVALID_CHARACTER) {
		return BCTBX_ERROR_INVALID_BASE64_INPUT;
	}

	return ret;
}

/*** Random Number Generation ***/
struct bctbx_rng_context_struct {
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
};

bctbx_rng_context_t *bctbx_rng_context_new(void) {
	bctbx_rng_context_t *ctx = bctbx_malloc0(sizeof(bctbx_rng_context_t));
	mbedtls_entropy_init(&(ctx->entropy));
	mbedtls_ctr_drbg_init(&(ctx->ctr_drbg));
	mbedtls_ctr_drbg_seed(&(ctx->ctr_drbg), mbedtls_entropy_func, &(ctx->entropy), NULL, 0);
	return ctx;
}

int32_t bctbx_rng_get(bctbx_rng_context_t *context, unsigned char*output, size_t output_length) {
	return mbedtls_ctr_drbg_random(&(context->ctr_drbg), output, output_length);
}

void bctbx_rng_context_free(bctbx_rng_context_t *context) {
	mbedtls_ctr_drbg_free(&(context->ctr_drbg));
	mbedtls_entropy_free(&(context->entropy));
	bctbx_free(context);
}

/*** signing key ***/
bctbx_signing_key_t *bctbx_signing_key_new(void) {
	mbedtls_pk_context *key = bctbx_malloc0(sizeof(mbedtls_pk_context));
	mbedtls_pk_init(key);
	return (bctbx_signing_key_t *)key;
}

void bctbx_signing_key_free(bctbx_signing_key_t *key) {
	mbedtls_pk_free((mbedtls_pk_context *)key);
	bctbx_free(key);
}

char *bctbx_signing_key_get_pem(bctbx_signing_key_t *key) {
	char *pem_key;
	if (key == NULL) return NULL;
	pem_key = (char *)bctbx_malloc0(4096);
	mbedtls_pk_write_key_pem( (mbedtls_pk_context *)key, (unsigned char *)pem_key, 4096);
	return pem_key;
}

int32_t bctbx_signing_key_parse(bctbx_signing_key_t *key, const char *buffer, size_t buffer_length, const unsigned char *password, size_t password_length) {
	int err;
	
	err=mbedtls_pk_parse_key((mbedtls_pk_context *)key, (const unsigned char *)buffer, buffer_length, password, password_length);
	
	if (err<0) {
		char tmp[128];
		mbedtls_strerror(err,tmp,sizeof(tmp));
		bctbx_error("cannot parse public key because [%s]",tmp);
		return BCTBX_ERROR_UNABLE_TO_PARSE_KEY;
	}
	return 0;
}

int32_t bctbx_signing_key_parse_file(bctbx_signing_key_t *key, const char *path, const char *password) {
	int err;
	
	err=mbedtls_pk_parse_keyfile((mbedtls_pk_context *)key, path, password);
	
	if (err<0) {
		char tmp[128];
		mbedtls_strerror(err,tmp,sizeof(tmp));
		bctbx_error("cannot parse public key because [%s]",tmp);
		return BCTBX_ERROR_UNABLE_TO_PARSE_KEY;
	}
	return 0;
}



/*** Certificate ***/
char *bctbx_x509_certificates_chain_get_pem(bctbx_x509_certificate_t *cert) {
	char *pem_certificate = NULL;
	size_t olen=0;

	pem_certificate = (char *)bctbx_malloc0(4096);
	mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n", "-----END CERTIFICATE-----\n", ((mbedtls_x509_crt *)cert)->raw.p, ((mbedtls_x509_crt *)cert)->raw.len, (unsigned char*)pem_certificate, 4096, &olen );
	return pem_certificate;
}


bctbx_x509_certificate_t *bctbx_x509_certificate_new(void) {
	mbedtls_x509_crt *cert = bctbx_malloc0(sizeof(mbedtls_x509_crt));
	mbedtls_x509_crt_init(cert);
	return (bctbx_x509_certificate_t *)cert;
}

void bctbx_x509_certificate_free(bctbx_x509_certificate_t *cert) {
	mbedtls_x509_crt_free((mbedtls_x509_crt *)cert);
	bctbx_free(cert);
}

int32_t bctbx_x509_certificate_get_info_string(char *buf, size_t size, const char *prefix, const bctbx_x509_certificate_t *cert) {
	return mbedtls_x509_crt_info(buf, size, prefix, (mbedtls_x509_crt *)cert);
}

int32_t bctbx_x509_certificate_parse_file(bctbx_x509_certificate_t *cert, const char *path) {
	return mbedtls_x509_crt_parse_file((mbedtls_x509_crt *)cert, path);
}

int32_t bctbx_x509_certificate_parse_path(bctbx_x509_certificate_t *cert, const char *path) {
	return mbedtls_x509_crt_parse_path((mbedtls_x509_crt *)cert, path);
}

int32_t bctbx_x509_certificate_parse(bctbx_x509_certificate_t *cert, const char *buffer, size_t buffer_length) {
	return mbedtls_x509_crt_parse((mbedtls_x509_crt *)cert, (const unsigned char *)buffer, buffer_length);
}

int32_t bctbx_x509_certificate_get_der_length(bctbx_x509_certificate_t *cert) {
	if (cert!=NULL) {
		return (int32_t)((mbedtls_x509_crt *)cert)->raw.len;
	}
	return 0;
}

int32_t bctbx_x509_certificate_get_der(bctbx_x509_certificate_t *cert, unsigned char *buffer, size_t buffer_length) {
	if (cert==NULL) {
		return BCTBX_ERROR_INVALID_CERTIFICATE;
	}
	if (((mbedtls_x509_crt *)cert)->raw.len>buffer_length-1) { /* check buffer size is ok, +1 for the NULL termination added at the end */
		return BCTBX_ERROR_OUTPUT_BUFFER_TOO_SMALL;
	}
	memcpy(buffer, ((mbedtls_x509_crt *)cert)->raw.p, ((mbedtls_x509_crt *)cert)->raw.len);
	buffer[((mbedtls_x509_crt *)cert)->raw.len] = '\0'; /* add a null termination char */

	return 0;
}

int32_t bctbx_x509_certificate_get_subject_dn(const bctbx_x509_certificate_t *cert, char *dn, size_t dn_length) {
	if (cert==NULL) {
		return BCTBX_ERROR_INVALID_CERTIFICATE;
	}

	return mbedtls_x509_dn_gets(dn, dn_length, &(((mbedtls_x509_crt *)cert)->subject));
}


bctbx_list_t *bctbx_x509_certificate_get_subjects(const bctbx_x509_certificate_t *cert){
	bctbx_list_t *ret = NULL;
	char subject[1024]={0};
	const mbedtls_x509_sequence *subjectAltNames = &((mbedtls_x509_crt *)cert)->subject_alt_names;
	
	for (; subjectAltNames != NULL; subjectAltNames = subjectAltNames->next){
		const mbedtls_asn1_buf *buf = &subjectAltNames->buf;
		if (buf->tag == ( MBEDTLS_ASN1_CONTEXT_SPECIFIC | 2 ) || buf->tag == ( MBEDTLS_ASN1_CONTEXT_SPECIFIC | 2 )){
			ret = bctbx_list_append(ret, bctbx_strndup((char*)buf->p, buf->len));
		}
	}
	
	if (bctbx_x509_certificate_get_subject_dn(cert, subject, sizeof(subject)-1) > 0){
		char *cn = strstr(subject, "CN=");
		if (cn){
			char *end;
			cn += 3;
			end = strchr(cn, ',');
			if (end) *end = '\0';
			ret = bctbx_list_append(ret, bctbx_strdup(cn));
		}
	}
	return ret;
}

int32_t bctbx_x509_certificate_generate_selfsigned(const char *subject, bctbx_x509_certificate_t *certificate, bctbx_signing_key_t *pkey, char * pem, size_t pem_length) {
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	int ret;
	mbedtls_mpi serial;
	mbedtls_x509write_cert crt;
	char file_buffer[8192];
	size_t file_buffer_len = 0;
	char formatted_subject[512];

	/* subject may be a sip URL or linphone-dtls-default-identity, add CN= before it to make a valid name */
	memcpy(formatted_subject, "CN=", 3);
	memcpy(formatted_subject+3, subject, strlen(subject)+1); /* +1 to get the \0 termination */

	mbedtls_entropy_init( &entropy );
	mbedtls_ctr_drbg_init(&ctr_drbg);
	if( ( ret = mbedtls_ctr_drbg_seed( &ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0 ) )   != 0 )
	{
		bctbx_error("Certificate generation can't init ctr_drbg: [-0x%x]", -ret);
		return BCTBX_ERROR_CERTIFICATE_GENERATION_FAIL;
	}

	/* generate 3072 bits RSA public/private key */
	if ( (ret = mbedtls_pk_setup( (mbedtls_pk_context *)pkey, mbedtls_pk_info_from_type( MBEDTLS_PK_RSA ) )) != 0) {
		bctbx_error("Certificate generation can't init pk_ctx: [-0x%x]", -ret);
		return BCTBX_ERROR_CERTIFICATE_GENERATION_FAIL;
	}

	if ( ( ret = mbedtls_rsa_gen_key( mbedtls_pk_rsa( *(mbedtls_pk_context *)pkey ), mbedtls_ctr_drbg_random, &ctr_drbg, 3072, 65537 ) ) != 0) {
		bctbx_error("Certificate generation can't generate rsa key: [-0x%x]", -ret);
		return BCTBX_ERROR_CERTIFICATE_GENERATION_FAIL;
	}

	/* if there is no pem pointer, don't save the key in pem format */
	if (pem!=NULL) {
		mbedtls_pk_write_key_pem((mbedtls_pk_context *)pkey, (unsigned char *)file_buffer, 4096);
		file_buffer_len = strlen(file_buffer);
	}

	/* generate the certificate */
	mbedtls_x509write_crt_init( &crt );
	mbedtls_x509write_crt_set_md_alg( &crt, MBEDTLS_MD_SHA256 );

	mbedtls_mpi_init( &serial );

	if ( (ret = mbedtls_mpi_read_string( &serial, 10, "1" ) ) != 0 ) {
		bctbx_error("Certificate generation can't read serial mpi: [-0x%x]", -ret);
		return BCTBX_ERROR_CERTIFICATE_GENERATION_FAIL;
	}

	mbedtls_x509write_crt_set_subject_key( &crt, (mbedtls_pk_context *)pkey);
	mbedtls_x509write_crt_set_issuer_key( &crt, (mbedtls_pk_context *)pkey);

	if ( (ret = mbedtls_x509write_crt_set_subject_name( &crt, formatted_subject) ) != 0) {
		bctbx_error("Certificate generation can't set subject name: [-0x%x]", -ret);
		return BCTBX_ERROR_CERTIFICATE_GENERATION_FAIL;
	}

	if ( (ret = mbedtls_x509write_crt_set_issuer_name( &crt, formatted_subject) ) != 0) {
		bctbx_error("Certificate generation can't set issuer name: -%x", -ret);
		return BCTBX_ERROR_CERTIFICATE_GENERATION_FAIL;
	}

	if ( (ret = mbedtls_x509write_crt_set_serial( &crt, &serial ) ) != 0) {
		bctbx_error("Certificate generation can't set serial: -%x", -ret);
		return BCTBX_ERROR_CERTIFICATE_GENERATION_FAIL;
	}
	mbedtls_mpi_free(&serial);

	if ( (ret = mbedtls_x509write_crt_set_validity( &crt, "20010101000000", "20300101000000" ) ) != 0) {
		bctbx_error("Certificate generation can't set validity: -%x", -ret);
		return BCTBX_ERROR_CERTIFICATE_GENERATION_FAIL;
	}

	/* store anyway certificate in pem format in a string even if we do not have file to write as we need it to get it in a x509_crt structure */
	if ( (ret = mbedtls_x509write_crt_pem( &crt, (unsigned char *)file_buffer+file_buffer_len, 4096, mbedtls_ctr_drbg_random, &ctr_drbg ) ) != 0) {
		bctbx_error("Certificate generation can't write crt pem: -%x", -ret);
		return BCTBX_ERROR_CERTIFICATE_WRITE_PEM;
	}

	mbedtls_x509write_crt_free(&crt);
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);

	/* copy the key+cert in pem format into the given buffer */
	if (pem != NULL) {
		if (strlen(file_buffer)+1>pem_length) {
			bctbx_error("Certificate generation can't copy the certificate to pem buffer: too short [%ld] but need [%ld] bytes", (long)pem_length, (long)strlen(file_buffer));
			return BCTBX_ERROR_OUTPUT_BUFFER_TOO_SMALL;
		}
		strncpy(pem, file_buffer, pem_length);
	}

	/* +1 on strlen as crt_parse in PEM format, length must include the final \0 */
	if ( (ret = mbedtls_x509_crt_parse((mbedtls_x509_crt *)certificate, (unsigned char *)file_buffer, strlen(file_buffer)+1) ) != 0) {
		bctbx_error("Certificate generation can't parse crt pem: -%x", -ret);
		return BCTBX_ERROR_CERTIFICATE_PARSE_PEM;
	}

	return 0;
}


int32_t bctbx_x509_certificate_get_signature_hash_function(const bctbx_x509_certificate_t *certificate, bctbx_md_type_t *hash_algorithm) {

	mbedtls_x509_crt *crt;
	if (certificate == NULL) return BCTBX_ERROR_INVALID_CERTIFICATE;

	crt = (mbedtls_x509_crt *)certificate;

	switch (crt->sig_md) {
		case MBEDTLS_MD_SHA1:
			*hash_algorithm = BCTBX_MD_SHA1;
		break;

		case MBEDTLS_MD_SHA224:
			*hash_algorithm = BCTBX_MD_SHA224;
		break;

		case MBEDTLS_MD_SHA256:
			*hash_algorithm = BCTBX_MD_SHA256;
		break;

		case MBEDTLS_MD_SHA384:
			*hash_algorithm = BCTBX_MD_SHA384;
		break;

		case MBEDTLS_MD_SHA512:
			*hash_algorithm = BCTBX_MD_SHA512;
		break;

		default:
			*hash_algorithm = BCTBX_MD_UNDEFINED;
			return BCTBX_ERROR_UNSUPPORTED_HASH_FUNCTION;
		break;
	}

	return 0;

}

/* maximum length of returned buffer will be 7(SHA-512 string)+3*hash_length(64)+null char = 200 bytes */
int32_t bctbx_x509_certificate_get_fingerprint(const bctbx_x509_certificate_t *certificate, char *fingerprint, size_t fingerprint_length, bctbx_md_type_t hash_algorithm) {
	unsigned char buffer[64]={0}; /* buffer is max length of returned hash, which is 64 in case we use sha-512 */
	size_t hash_length = 0;
	const char *hash_alg_string=NULL;
	size_t fingerprint_size = 0;
	mbedtls_x509_crt *crt;
	mbedtls_md_type_t hash_id;
	if (certificate == NULL) return BCTBX_ERROR_INVALID_CERTIFICATE;

	crt = (mbedtls_x509_crt *)certificate;

	/* if there is a specified hash algorithm, use it*/
	switch (hash_algorithm) {
		case BCTBX_MD_SHA1:
			hash_id = MBEDTLS_MD_SHA1;
			break;
		case BCTBX_MD_SHA224:
			hash_id = MBEDTLS_MD_SHA224;
			break;
		case BCTBX_MD_SHA256:
			hash_id = MBEDTLS_MD_SHA256;
			break;
		case BCTBX_MD_SHA384:
			hash_id = MBEDTLS_MD_SHA384;
			break;
		case BCTBX_MD_SHA512:
			hash_id = MBEDTLS_MD_SHA512;
			break;
		default: /* nothing specified, use the hash algo used in the certificate signature */
			hash_id = crt->sig_md;
			break;
	}

	/* fingerprint is a hash of the DER formated certificate (found in crt->raw.p) using the same hash function used by certificate signature */
	switch (hash_id) {
		case MBEDTLS_MD_SHA1:
			mbedtls_sha1(crt->raw.p, crt->raw.len, buffer);
			hash_length = 20;
			hash_alg_string="SHA-1";
		break;

		case MBEDTLS_MD_SHA224:
			mbedtls_sha256(crt->raw.p, crt->raw.len, buffer, 1); /* last argument is a boolean, indicate to output sha-224 and not sha-256 */
			hash_length = 28;
			hash_alg_string="SHA-224";
		break;

		case MBEDTLS_MD_SHA256:
			mbedtls_sha256(crt->raw.p, crt->raw.len, buffer, 0);
			hash_length = 32;
			hash_alg_string="SHA-256";
		break;

		case MBEDTLS_MD_SHA384:
			mbedtls_sha512(crt->raw.p, crt->raw.len, buffer, 1); /* last argument is a boolean, indicate to output sha-384 and not sha-512 */
			hash_length = 48;
			hash_alg_string="SHA-384";
		break;

		case MBEDTLS_MD_SHA512:
			mbedtls_sha512(crt->raw.p, crt->raw.len, buffer, 1); /* last argument is a boolean, indicate to output sha-384 and not sha-512 */
			hash_length = 64;
			hash_alg_string="SHA-512";
		break;

		default:
			return BCTBX_ERROR_UNSUPPORTED_HASH_FUNCTION;
		break;
	}

	if (hash_length>0) {
		size_t i;
		size_t fingerprint_index = strlen(hash_alg_string);
		char prefix=' ';

		fingerprint_size=fingerprint_index+3*hash_length+1;
		/* fingerprint will be : hash_alg_string+' '+HEX : separated values: length is strlen(hash_alg_string)+3*hash_lenght + 1 for null termination */
		if (fingerprint_length<fingerprint_size) {
			return BCTBX_ERROR_OUTPUT_BUFFER_TOO_SMALL;
		}

		snprintf(fingerprint, fingerprint_size, "%s", hash_alg_string);
		for (i=0; i<hash_length; i++, fingerprint_index+=3) {
			snprintf((char*)fingerprint+fingerprint_index, fingerprint_size-fingerprint_index, "%c%02X", prefix,buffer[i]);
			prefix=':';
		}
		*(fingerprint+fingerprint_index) = '\0';
	}

	return (int32_t)fingerprint_size;
}

#define BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH 256
int32_t bctbx_x509_certificate_flags_to_string(char *buffer, size_t buffer_size, uint32_t flags) {
	size_t i=0;
	char outputString[BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH];

	if (flags & MBEDTLS_X509_BADCERT_EXPIRED)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "expired ");
	if (flags & MBEDTLS_X509_BADCERT_REVOKED)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "revoked ");
	if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "CN-mismatch ");
	if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "not-trusted ");
	if (flags & MBEDTLS_X509_BADCERT_MISSING)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "missing ");
	if (flags & MBEDTLS_X509_BADCERT_SKIP_VERIFY)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "skip-verify ");
	if (flags & MBEDTLS_X509_BADCERT_OTHER)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "other-reason ");
	if (flags & MBEDTLS_X509_BADCERT_FUTURE)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "future-validity ");
	if (flags & MBEDTLS_X509_BADCERT_KEY_USAGE)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "keyUsage-mismatch");
	if (flags & MBEDTLS_X509_BADCERT_EXT_KEY_USAGE)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "extendedKeyUsage-mismatch ");
	if (flags & MBEDTLS_X509_BADCERT_NS_CERT_TYPE)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "nsCertType-mismatch ");
	if (flags & MBEDTLS_X509_BADCERT_BAD_MD)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "unacceptable-hash ");
	if (flags & MBEDTLS_X509_BADCERT_BAD_PK)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "unacceptable-PK-alg ");
	if (flags & MBEDTLS_X509_BADCERT_BAD_KEY)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "unacceptable-key ");

	if (flags & MBEDTLS_X509_BADCRL_NOT_TRUSTED)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "crl-not-trusted ");
	if (flags & MBEDTLS_X509_BADCRL_EXPIRED)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "crl-expired ");

	if (flags & MBEDTLS_X509_BADCRL_FUTURE)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "crl-future ");
	if (flags & MBEDTLS_X509_BADCRL_BAD_MD)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "crl-unacceptable-hash ");
	if (flags & MBEDTLS_X509_BADCRL_BAD_PK)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "crl-unacceptable-PK-alg ");
	if (flags & MBEDTLS_X509_BADCRL_BAD_KEY)
		i+=snprintf(outputString+i, BCTBX_MAX_CERTIFICATE_FLAGS_STRING_LENGTH-i, "crl-unacceptable-key ");

	outputString[i] = '\0'; /* null terminate the string */

	if (i+1>buffer_size) {
		return BCTBX_ERROR_OUTPUT_BUFFER_TOO_SMALL;
	}

	strncpy(buffer, outputString, buffer_size);

	return 0;
}

int32_t bctbx_x509_certificate_set_flag(uint32_t *flags, uint32_t flags_to_set) {
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_EXPIRED)
		*flags |= MBEDTLS_X509_BADCERT_EXPIRED;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_REVOKED)
		*flags |= MBEDTLS_X509_BADCERT_REVOKED;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_CN_MISMATCH)
		*flags |= MBEDTLS_X509_BADCERT_CN_MISMATCH;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_NOT_TRUSTED)
		*flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_MISSING)
		*flags |= MBEDTLS_X509_BADCERT_MISSING;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_SKIP_VERIFY)
		*flags |= MBEDTLS_X509_BADCERT_SKIP_VERIFY;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_OTHER)
		*flags |= MBEDTLS_X509_BADCERT_OTHER;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_FUTURE)
		*flags |= MBEDTLS_X509_BADCERT_FUTURE;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_KEY_USAGE)
		*flags |= MBEDTLS_X509_BADCERT_KEY_USAGE;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_EXT_KEY_USAGE)
		*flags |= MBEDTLS_X509_BADCERT_EXT_KEY_USAGE;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_NS_CERT_TYPE)
		*flags |= MBEDTLS_X509_BADCERT_NS_CERT_TYPE;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_MD)
		*flags |= MBEDTLS_X509_BADCERT_BAD_MD;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_PK)
		*flags |= MBEDTLS_X509_BADCERT_BAD_PK;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_KEY)
		*flags |= MBEDTLS_X509_BADCERT_BAD_KEY;

	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCRL_NOT_TRUSTED)
		*flags |= MBEDTLS_X509_BADCRL_NOT_TRUSTED;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCRL_EXPIRED)
		*flags |= MBEDTLS_X509_BADCRL_EXPIRED;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCRL_FUTURE)
		*flags |= MBEDTLS_X509_BADCRL_FUTURE;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_MD)
		*flags |= MBEDTLS_X509_BADCRL_BAD_MD;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_PK)
		*flags |= MBEDTLS_X509_BADCRL_BAD_PK;
	if (flags_to_set & BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_KEY)
		*flags |= MBEDTLS_X509_BADCRL_BAD_KEY;

	return 0;
}

uint32_t bctbx_x509_certificate_remap_flag(uint32_t flags) {
	uint32_t ret = 0;
	if (flags & MBEDTLS_X509_BADCERT_EXPIRED)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_EXPIRED;
	if (flags & MBEDTLS_X509_BADCERT_REVOKED)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_REVOKED;
	if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_CN_MISMATCH;
	if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_NOT_TRUSTED;
	if (flags & MBEDTLS_X509_BADCERT_MISSING)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_MISSING;
	if (flags & MBEDTLS_X509_BADCERT_SKIP_VERIFY)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_SKIP_VERIFY;
	if (flags & MBEDTLS_X509_BADCERT_FUTURE)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_FUTURE;
	if (flags & MBEDTLS_X509_BADCERT_OTHER)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_OTHER;
	if (flags & MBEDTLS_X509_BADCERT_KEY_USAGE)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_KEY_USAGE;
	if (flags & MBEDTLS_X509_BADCERT_EXT_KEY_USAGE)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_EXT_KEY_USAGE;
	if (flags & MBEDTLS_X509_BADCERT_NS_CERT_TYPE)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_NS_CERT_TYPE;
	if (flags & MBEDTLS_X509_BADCERT_BAD_MD)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_MD;
	if (flags & MBEDTLS_X509_BADCERT_BAD_PK)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_PK;
	if (flags & MBEDTLS_X509_BADCERT_BAD_KEY)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_KEY;

	if (flags & MBEDTLS_X509_BADCRL_NOT_TRUSTED)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCRL_NOT_TRUSTED;
	if (flags & MBEDTLS_X509_BADCRL_EXPIRED)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCRL_EXPIRED;
	if (flags & MBEDTLS_X509_BADCRL_FUTURE)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCRL_FUTURE;
	if (flags & MBEDTLS_X509_BADCRL_BAD_MD)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_MD;
	if (flags & MBEDTLS_X509_BADCRL_BAD_PK)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_PK;
	if (flags & MBEDTLS_X509_BADCRL_BAD_KEY)
		ret |= BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_KEY;

	return ret;
}

int32_t bctbx_x509_certificate_unset_flag(uint32_t *flags, uint32_t flags_to_unset) {
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_EXPIRED)
		*flags &= ~MBEDTLS_X509_BADCERT_EXPIRED;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_REVOKED)
		*flags &= ~MBEDTLS_X509_BADCERT_REVOKED;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_CN_MISMATCH)
		*flags &= ~MBEDTLS_X509_BADCERT_CN_MISMATCH;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_NOT_TRUSTED)
		*flags &= ~MBEDTLS_X509_BADCERT_NOT_TRUSTED;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_MISSING)
		*flags &= ~MBEDTLS_X509_BADCERT_MISSING;

	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_SKIP_VERIFY)
		*flags &= ~MBEDTLS_X509_BADCERT_SKIP_VERIFY;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_OTHER)
		*flags &= ~MBEDTLS_X509_BADCERT_OTHER;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_FUTURE)
		*flags &= ~MBEDTLS_X509_BADCERT_FUTURE;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_KEY_USAGE)
		*flags &= ~MBEDTLS_X509_BADCERT_KEY_USAGE;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_EXT_KEY_USAGE)
		*flags &= ~MBEDTLS_X509_BADCERT_EXT_KEY_USAGE;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_NS_CERT_TYPE)
		*flags &= ~MBEDTLS_X509_BADCERT_NS_CERT_TYPE;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_MD)
		*flags &= ~MBEDTLS_X509_BADCERT_BAD_MD;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_PK)
		*flags &= ~MBEDTLS_X509_BADCERT_BAD_PK;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCERT_BAD_KEY)
		*flags &= ~MBEDTLS_X509_BADCERT_BAD_KEY;


	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCRL_NOT_TRUSTED)
		*flags &= ~MBEDTLS_X509_BADCRL_NOT_TRUSTED;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCRL_EXPIRED)
		*flags &= ~MBEDTLS_X509_BADCRL_EXPIRED;

	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCRL_FUTURE)
		*flags &= ~MBEDTLS_X509_BADCRL_FUTURE;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_MD)
		*flags &= ~MBEDTLS_X509_BADCRL_BAD_MD;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_PK)
		*flags &= ~MBEDTLS_X509_BADCRL_BAD_PK;
	if (flags_to_unset & BCTBX_CERTIFICATE_VERIFY_BADCRL_BAD_KEY)
		*flags &= ~MBEDTLS_X509_BADCRL_BAD_KEY;

	return 0;
}

/*** Diffie-Hellman-Merkle ***/
/* initialise de DHM context according to requested algorithm */
bctbx_DHMContext_t *bctbx_CreateDHMContext(uint8_t DHMAlgo, uint8_t secretLength)
{
	mbedtls_dhm_context *mbedtlsDhmContext;

	/* create the context */
	bctbx_DHMContext_t *context = (bctbx_DHMContext_t *)malloc(sizeof(bctbx_DHMContext_t));
	memset (context, 0, sizeof(bctbx_DHMContext_t));

	/* create the mbedtls context for DHM */
	mbedtlsDhmContext=(mbedtls_dhm_context *)malloc(sizeof(mbedtls_dhm_context));
	memset(mbedtlsDhmContext, 0, sizeof(mbedtls_dhm_context));
	context->cryptoModuleData=(void *)mbedtlsDhmContext;

	/* initialise pointer to NULL to ensure safe call to free() when destroying context */
	context->secret = NULL;
	context->self = NULL;
	context->key = NULL;
	context->peer = NULL;

	/* set parameters in the context */
	context->algo=DHMAlgo;
	context->secretLength = secretLength;
	switch (DHMAlgo) {
		case BCTBX_DHM_2048:
			/* set P and G in the mbedtls context */
			if ((mbedtls_mpi_read_string(&(mbedtlsDhmContext->P), 16, MBEDTLS_DHM_RFC3526_MODP_2048_P) != 0) ||
			(mbedtls_mpi_read_string(&(mbedtlsDhmContext->G), 16, MBEDTLS_DHM_RFC3526_MODP_2048_G) != 0)) {
				return NULL;
			}
			context->primeLength=256;
			mbedtlsDhmContext->len=256;
			break;
		case BCTBX_DHM_3072:
			/* set P and G in the mbedtls context */
			if ((mbedtls_mpi_read_string(&(mbedtlsDhmContext->P), 16, MBEDTLS_DHM_RFC3526_MODP_3072_P) != 0) ||
			(mbedtls_mpi_read_string(&(mbedtlsDhmContext->G), 16, MBEDTLS_DHM_RFC3526_MODP_3072_G) != 0)) {
				return NULL;
			}
			context->primeLength=384;
			mbedtlsDhmContext->len=384;
			break;
		default:
			free(context);
			return NULL;
			break;
	}

	return context;
}

/* generate the random secret and compute the public value */
void bctbx_DHMCreatePublic(bctbx_DHMContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext) {
	/* get the mbedtls context */
	mbedtls_dhm_context *mbedtlsContext = (mbedtls_dhm_context *)context->cryptoModuleData;

	/* allocate output buffer */
	context->self = (uint8_t *)malloc(context->primeLength*sizeof(uint8_t));

	mbedtls_dhm_make_public(mbedtlsContext, context->secretLength, context->self, context->primeLength, (int (*)(void *, unsigned char *, size_t))rngFunction, rngContext);

}

/* compute secret - the ->peer field of context must have been set before calling this function */
void bctbx_DHMComputeSecret(bctbx_DHMContext_t *context, int (*rngFunction)(void *, uint8_t *, size_t), void *rngContext) {
	size_t keyLength;
	uint8_t sharedSecretBuffer[384]; /* longest shared secret available in these mode */

	/* import the peer public value G^Y mod P in the mbedtls dhm context */
	mbedtls_dhm_read_public((mbedtls_dhm_context *)(context->cryptoModuleData), context->peer, context->primeLength);

	/* compute the secret key */
	keyLength = context->primeLength; /* undocumented but this value seems to be in/out, so we must set it to the expected key length */
	context->key = (uint8_t *)malloc(keyLength*sizeof(uint8_t)); /* allocate and reset the key buffer */
	memset(context->key,0, keyLength);
	mbedtls_dhm_calc_secret((mbedtls_dhm_context *)(context->cryptoModuleData), sharedSecretBuffer, 384, &keyLength, (int (*)(void *, unsigned char *, size_t))rngFunction, rngContext);
	/* now copy the resulting secret in the correct place in buffer(result may actually miss some front zero bytes, real length of output is now in keyLength but we want primeLength bytes) */
	memcpy(context->key+(context->primeLength-keyLength), sharedSecretBuffer, keyLength);
	memset(sharedSecretBuffer, 0, 384); /* purge secret from temporary buffer */
}

/* clean DHM context */
void bctbx_DestroyDHMContext(bctbx_DHMContext_t *context) {
	if (context!= NULL) {
		/* key and secret must be erased from memory and not just freed */
		if (context->secret != NULL) {
			bctbx_clean(context->secret, context->secretLength);
			free(context->secret);
		}
		free(context->self);
		if (context->key != NULL) {
			bctbx_clean(context->key, context->primeLength);
			free(context->key);
		}
		free(context->peer);

		mbedtls_dhm_free((mbedtls_dhm_context *)context->cryptoModuleData);
		free(context->cryptoModuleData);

		free(context);
	}
}

/*** SSL Client ***/
/*
 * Default profile used to configure ssl_context, allow 1024 bits keys(while mbedtls default is 2048)
 */
const mbedtls_x509_crt_profile bctbx_x509_crt_profile_default =
{
    /* Hashes from SHA-1 and above */
    MBEDTLS_X509_ID_FLAG( MBEDTLS_MD_SHA1 ) |
    MBEDTLS_X509_ID_FLAG( MBEDTLS_MD_RIPEMD160 ) |
    MBEDTLS_X509_ID_FLAG( MBEDTLS_MD_SHA224 ) |
    MBEDTLS_X509_ID_FLAG( MBEDTLS_MD_SHA256 ) |
    MBEDTLS_X509_ID_FLAG( MBEDTLS_MD_SHA384 ) |
    MBEDTLS_X509_ID_FLAG( MBEDTLS_MD_SHA512 ),
    0xFFFFFFF, /* Any PK alg    */
    0xFFFFFFF, /* Any curve     */
    1024,
};

/** context **/
struct bctbx_ssl_context_struct {
	mbedtls_ssl_context ssl_ctx;
	int(*callback_cli_cert_function)(void *, bctbx_ssl_context_t *, unsigned char *, size_t); /**< pointer to the callback called to update client certificate during handshake
													callback params are user_data, ssl_context, certificate distinguished name, name length */
	void *callback_cli_cert_data; /**< data passed to the client cert callback */
	int(*callback_send_function)(void *, const unsigned char *, size_t); /* callbacks args are: callback data, data buffer to be send, size of data buffer */
	int(*callback_recv_function)(void *, unsigned char *, size_t); /* args: callback data, data buffer to be read, size of data buffer */
	void *callback_sendrecv_data; /**< data passed to send/recv callbacks */
	mbedtls_timing_delay_context timer; /**< a timer is requested for DTLS */
};

bctbx_ssl_context_t *bctbx_ssl_context_new(void) {
	bctbx_ssl_context_t *ssl_ctx = bctbx_malloc0(sizeof(bctbx_ssl_context_t));
	mbedtls_ssl_init(&(ssl_ctx->ssl_ctx));
	ssl_ctx->callback_cli_cert_function = NULL;
	ssl_ctx->callback_cli_cert_data = NULL;
	ssl_ctx->callback_send_function = NULL;
	ssl_ctx->callback_recv_function = NULL;
	ssl_ctx->callback_sendrecv_data = NULL;
	return ssl_ctx;
}

void bctbx_ssl_context_free(bctbx_ssl_context_t *ssl_ctx) {
	mbedtls_ssl_free(&(ssl_ctx->ssl_ctx));
	bctbx_free(ssl_ctx);
}

int32_t bctbx_ssl_close_notify(bctbx_ssl_context_t *ssl_ctx) {
	return mbedtls_ssl_close_notify(&(ssl_ctx->ssl_ctx));
}

int32_t bctbx_ssl_session_reset(bctbx_ssl_context_t *ssl_ctx) {
	return mbedtls_ssl_session_reset(&(ssl_ctx->ssl_ctx));
}

int32_t bctbx_ssl_write(bctbx_ssl_context_t *ssl_ctx, const unsigned char *buf, size_t buf_length) {
	int ret = mbedtls_ssl_write(&(ssl_ctx->ssl_ctx), buf, buf_length);
	/* remap some output code */
	if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
		ret = BCTBX_ERROR_NET_WANT_WRITE;
	}
	return ret;
}

int32_t bctbx_ssl_read(bctbx_ssl_context_t *ssl_ctx, unsigned char *buf, size_t buf_length) {
	int ret = mbedtls_ssl_read(&(ssl_ctx->ssl_ctx), buf, buf_length);
	/* remap some output code */
	if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
		ret = BCTBX_ERROR_SSL_PEER_CLOSE_NOTIFY;
	}
	if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
		ret = BCTBX_ERROR_NET_WANT_READ;
	}
	return ret;
}

int32_t bctbx_ssl_handshake(bctbx_ssl_context_t *ssl_ctx) {

	int ret = 0;
	while( ssl_ctx->ssl_ctx.state != MBEDTLS_SSL_HANDSHAKE_OVER )
	{
		ret = mbedtls_ssl_handshake_step(&(ssl_ctx->ssl_ctx));
		if( ret != 0 ) {
			break;
		}

		/* insert the callback function for client certificate request */
		if (ssl_ctx->callback_cli_cert_function != NULL) { /* check we have a callback function */
			/* when in state SSL_CLIENT_CERTIFICATE - which means, next call to ssl_handshake_step will send the client certificate to server -
			 * and the client_auth flag is set - which means the server requested a client certificate - */
			if (ssl_ctx->ssl_ctx.state == MBEDTLS_SSL_CLIENT_CERTIFICATE && ssl_ctx->ssl_ctx.client_auth > 0) {
				/* TODO: retrieve certificate dn during handshake from server certificate request
				 * for now the dn params in the callback are set to NULL and 0(dn string length) */
				if (ssl_ctx->callback_cli_cert_function(ssl_ctx->callback_cli_cert_data, ssl_ctx, NULL, 0)!=0) {
					if((ret=mbedtls_ssl_send_alert_message(&(ssl_ctx->ssl_ctx), MBEDTLS_SSL_ALERT_LEVEL_FATAL, MBEDTLS_SSL_ALERT_MSG_HANDSHAKE_FAILURE)) != 0 )
						return( ret );
				}
			}
		}

	}

	/* remap some output codes */
	if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
		ret = BCTBX_ERROR_NET_WANT_READ;
	} else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
		ret = BCTBX_ERROR_NET_WANT_WRITE;
	}

	return(ret);
}

int32_t bctbx_ssl_set_hs_own_cert(bctbx_ssl_context_t *ssl_ctx, bctbx_x509_certificate_t *cert, bctbx_signing_key_t *key) {
	/* WARNING Dirty-Dirty trick : in mbedtls ssl_set_hs_own_cert shall be caller on server side only for SNI, there is no real equivalent on client side yet */
	/* If we are server call the regular function */
	if (ssl_ctx->ssl_ctx.conf->endpoint == MBEDTLS_SSL_IS_SERVER ) {
		return mbedtls_ssl_set_hs_own_cert(&(ssl_ctx->ssl_ctx) , (mbedtls_x509_crt *)cert , (mbedtls_pk_context *)key);
	} else { /* if we are client, call the conf function on the function in use inside the ssl_context, dirty but it works for now */
		return mbedtls_ssl_conf_own_cert((mbedtls_ssl_config *)ssl_ctx->ssl_ctx.conf , (mbedtls_x509_crt *)cert , (mbedtls_pk_context *)key);
	}
}

int bctbx_ssl_send_callback(void *data, const unsigned char *buffer, size_t buffer_length) {
	int ret = 0;
	/* data is the ssl_context which contains the actual callback and data */
	bctbx_ssl_context_t *ssl_ctx = (bctbx_ssl_context_t *)data;

	ret = ssl_ctx->callback_send_function(ssl_ctx->callback_sendrecv_data, buffer, buffer_length);

	return bctbx_ssl_sendrecv_callback_return_remap(ret);
}

int bctbx_ssl_recv_callback(void *data, unsigned char *buffer, size_t buffer_length) {
	int ret = 0;
	/* data is the ssl_context which contains the actual callback and data */
	bctbx_ssl_context_t *ssl_ctx = (bctbx_ssl_context_t *)data;

	ret = ssl_ctx->callback_recv_function(ssl_ctx->callback_sendrecv_data, buffer, buffer_length);

	return bctbx_ssl_sendrecv_callback_return_remap(ret);
}

void bctbx_ssl_set_io_callbacks(bctbx_ssl_context_t *ssl_ctx, void *callback_data,
		int(*callback_send_function)(void *, const unsigned char *, size_t), /* callbacks args are: callback data, data buffer to be send, size of data buffer */
		int(*callback_recv_function)(void *, unsigned char *, size_t)){ /* args: callback data, data buffer to be read, size of data buffer */

	if (ssl_ctx==NULL) {
		return;
	}

	ssl_ctx->callback_send_function = callback_send_function;
	ssl_ctx->callback_recv_function = callback_recv_function;
	ssl_ctx->callback_sendrecv_data = callback_data;

	mbedtls_ssl_set_bio(&(ssl_ctx->ssl_ctx), ssl_ctx, bctbx_ssl_send_callback, bctbx_ssl_recv_callback, NULL);
}

const bctbx_x509_certificate_t *bctbx_ssl_get_peer_certificate(bctbx_ssl_context_t *ssl_ctx) {
	return (const bctbx_x509_certificate_t *)mbedtls_ssl_get_peer_cert(&(ssl_ctx->ssl_ctx));
}

const char *bctbx_ssl_get_ciphersuite(bctbx_ssl_context_t *ssl_ctx){
	return mbedtls_ssl_get_ciphersuite(&(ssl_ctx->ssl_ctx));
}

int bctbx_ssl_get_ciphersuite_id(const char *ciphersuite){
	return mbedtls_ssl_get_ciphersuite_id(ciphersuite);
}

const char *bctbx_ssl_get_version(bctbx_ssl_context_t *ssl_ctx){
	return mbedtls_ssl_get_version(&(ssl_ctx->ssl_ctx));
}

int32_t bctbx_ssl_set_hostname(bctbx_ssl_context_t *ssl_ctx, const char *hostname){
	return mbedtls_ssl_set_hostname(&(ssl_ctx->ssl_ctx), hostname);
}

/** DTLS SRTP functions **/
#ifdef HAVE_DTLS_SRTP
uint8_t bctbx_dtls_srtp_supported(void) {
	return 1;
}

void bctbx_ssl_set_mtu(bctbx_ssl_context_t *ssl_ctx, uint16_t mtu) {
	// remove the record expansion to the given MTU
	mbedtls_ssl_set_mtu(&(ssl_ctx->ssl_ctx), mtu - mbedtls_ssl_get_record_expansion(&(ssl_ctx->ssl_ctx)));
}

static bctbx_dtls_srtp_profile_t bctbx_srtp_profile_mbedtls2bctoolbox(mbedtls_ssl_srtp_profile mbedtls_profile) {
	switch (mbedtls_profile) {
		case MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_80:
			return BCTBX_SRTP_AES128_CM_HMAC_SHA1_80;
		case MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_32:
			return BCTBX_SRTP_AES128_CM_HMAC_SHA1_32;
		case MBEDTLS_SRTP_NULL_HMAC_SHA1_80:
			return BCTBX_SRTP_NULL_HMAC_SHA1_80;
		case MBEDTLS_SRTP_NULL_HMAC_SHA1_32:
			return BCTBX_SRTP_NULL_HMAC_SHA1_32;
		default:
			return BCTBX_SRTP_UNDEFINED;
	}
}

static mbedtls_ssl_srtp_profile bctbx_srtp_profile_bctoolbox2mbedtls(bctbx_dtls_srtp_profile_t bctbx_profile) {
	switch (bctbx_profile) {
		case BCTBX_SRTP_AES128_CM_HMAC_SHA1_80:
			return MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_80;
		case BCTBX_SRTP_AES128_CM_HMAC_SHA1_32:
			return MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_32;
		case BCTBX_SRTP_NULL_HMAC_SHA1_80:
			return MBEDTLS_SRTP_NULL_HMAC_SHA1_80;
		case BCTBX_SRTP_NULL_HMAC_SHA1_32:
			return MBEDTLS_SRTP_NULL_HMAC_SHA1_32;
		default:
			return MBEDTLS_SRTP_UNSET_PROFILE;
	}
}

bctbx_dtls_srtp_profile_t bctbx_ssl_get_dtls_srtp_protection_profile(bctbx_ssl_context_t *ssl_ctx) {
	if (ssl_ctx==NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONTEXT;
	}

	return bctbx_srtp_profile_mbedtls2bctoolbox(mbedtls_ssl_get_dtls_srtp_protection_profile(&(ssl_ctx->ssl_ctx)));
};


int32_t bctbx_ssl_get_dtls_srtp_key_material(bctbx_ssl_context_t *ssl_ctx, char *output, size_t *output_length) {
	int ret = 0;
	if (ssl_ctx==NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONTEXT;
	}

	ret = mbedtls_ssl_get_dtls_srtp_key_material(&(ssl_ctx->ssl_ctx), (unsigned char *)output, *output_length, output_length);

	/* remap the output error code */
	if (ret == MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL) {
		return BCTBX_ERROR_OUTPUT_BUFFER_TOO_SMALL;
	}

	return 0;
}
#else /* HAVE_DTLS_SRTP */
/* dummy DTLS api when not available */
uint8_t bctbx_dtls_srtp_supported(void) {
	return 0;
}

void bctbx_ssl_set_mtu(bctbx_ssl_context_t *ssl_ctx, uint16_t mtu) { }

bctbx_dtls_srtp_profile_t bctbx_ssl_get_dtls_srtp_protection_profile(bctbx_ssl_context_t *ssl_ctx) {
	return BCTBX_SRTP_UNDEFINED;
}

int32_t bctbx_ssl_get_dtls_srtp_key_material(bctbx_ssl_context_t *ssl_ctx, char *output, size_t *output_length) {
	*output_length = 0;
	return BCTBX_ERROR_UNAVAILABLE_FUNCTION;
}
#endif /* HAVE_DTLS_SRTP */

/** DTLS SRTP functions **/

/** config **/
struct bctbx_ssl_config_struct {
	mbedtls_ssl_config *ssl_config; /**< actual config structure */
	uint8_t ssl_config_externally_provided; /**< a flag, on when the ssl_config was provided by callers and not created threw the new function */
	int(*callback_cli_cert_function)(void *, bctbx_ssl_context_t *, unsigned char *, size_t); /**< pointer to the callback called to update client certificate during handshake
													callback params are user_data, ssl_context, certificate distinguished name, name length */
	void *callback_cli_cert_data; /**< data passed to the client cert callback */
};

bctbx_ssl_config_t *bctbx_ssl_config_new(void) {
	bctbx_ssl_config_t *ssl_config = bctbx_malloc0(sizeof(bctbx_ssl_config_t));
	/* allocate and init anyway a ssl_config, it may be then crashed by an externally provided one */
	ssl_config->ssl_config = bctbx_malloc0(sizeof(mbedtls_ssl_config));
	ssl_config->ssl_config_externally_provided = 0;
	mbedtls_ssl_config_init(ssl_config->ssl_config);

	ssl_config->callback_cli_cert_function = NULL;
	ssl_config->callback_cli_cert_data = NULL;

	return ssl_config;
}

bctbx_type_implementation_t bctbx_ssl_get_implementation_type(void) {
	return BCTBX_MBEDTLS;
}

int32_t bctbx_ssl_config_set_crypto_library_config(bctbx_ssl_config_t *ssl_config, void *internal_config) {
	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}

	/* free already existing structure */
	if (ssl_config->ssl_config != NULL && ssl_config->ssl_config_externally_provided==0) {
		mbedtls_ssl_config_free(ssl_config->ssl_config);
		bctbx_free(ssl_config->ssl_config);
	}

	/* set the given pointer as the ssl_config context */
	ssl_config->ssl_config = (mbedtls_ssl_config *)internal_config;

	/* set the flag in order to not free the mbedtls config when freing bctbx_ssl_config */
	ssl_config->ssl_config_externally_provided = 1;

	return 0;
}

void bctbx_ssl_config_free(bctbx_ssl_config_t *ssl_config) {
	if (ssl_config == NULL) {
		return;
	}

	/* free mbedtls_ssl_config only when created internally */
	if (ssl_config->ssl_config_externally_provided==0) {
		mbedtls_ssl_config_free(ssl_config->ssl_config);
		bctbx_free(ssl_config->ssl_config);
	}

	bctbx_free(ssl_config);
}

int32_t bctbx_ssl_config_defaults(bctbx_ssl_config_t *ssl_config, int endpoint, int transport) {
	int mbedtls_endpoint, mbedtls_transport, ret;

	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}

	/* remap input arguments */
	switch (endpoint) {
		case BCTBX_SSL_IS_CLIENT:
			mbedtls_endpoint = MBEDTLS_SSL_IS_CLIENT;
			break;
		case BCTBX_SSL_IS_SERVER:
			mbedtls_endpoint = MBEDTLS_SSL_IS_SERVER;
			break;
		default:
			return BCTBX_ERROR_INVALID_INPUT_DATA;
	}

	switch (transport) {
		case BCTBX_SSL_TRANSPORT_STREAM:
			mbedtls_transport = MBEDTLS_SSL_TRANSPORT_STREAM;
			break;
		case BCTBX_SSL_TRANSPORT_DATAGRAM:
			mbedtls_transport = MBEDTLS_SSL_TRANSPORT_DATAGRAM;
			break;
		default:
			return BCTBX_ERROR_INVALID_INPUT_DATA;
	}

	ret = mbedtls_ssl_config_defaults(ssl_config->ssl_config, mbedtls_endpoint, mbedtls_transport, MBEDTLS_SSL_PRESET_DEFAULT);

	if (ret <0) {
		return ret;
	}

	/* Set the default x509 security profile used for verification of all certificate in chain */
	mbedtls_ssl_conf_cert_profile(ssl_config->ssl_config, &bctbx_x509_crt_profile_default);

	return ret;
}

int32_t bctbx_ssl_config_set_endpoint(bctbx_ssl_config_t *ssl_config, int endpoint) {
	int mbedtls_endpoint;
	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}

	/* remap input arguments */
	switch (endpoint) {
		case BCTBX_SSL_IS_CLIENT:
			mbedtls_endpoint = MBEDTLS_SSL_IS_CLIENT;
			break;
		case BCTBX_SSL_IS_SERVER:
			mbedtls_endpoint = MBEDTLS_SSL_IS_SERVER;
			break;
		default:
			return BCTBX_ERROR_INVALID_INPUT_DATA;
	}

	mbedtls_ssl_conf_endpoint(ssl_config->ssl_config, mbedtls_endpoint);

	return 0;
}

int32_t bctbx_ssl_config_set_transport (bctbx_ssl_config_t *ssl_config, int transport) {
	int mbedtls_transport;
	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}

	/* remap input arguments */
	switch (transport) {
		case BCTBX_SSL_TRANSPORT_STREAM:
			mbedtls_transport = MBEDTLS_SSL_TRANSPORT_STREAM;
			break;
		case BCTBX_SSL_TRANSPORT_DATAGRAM:
			mbedtls_transport = MBEDTLS_SSL_TRANSPORT_DATAGRAM;
			break;
		default:
			return BCTBX_ERROR_INVALID_INPUT_DATA;
	}

	mbedtls_ssl_conf_transport(ssl_config->ssl_config, mbedtls_transport);

	return 0;
}

int32_t bctbx_ssl_config_set_ciphersuites(bctbx_ssl_config_t *ssl_config, const int *ciphersuites) {
	if (ssl_config == NULL) {
	 return BCTBX_ERROR_INVALID_SSL_CONFIG;
	 }
	/* remap input arguments */
	if (ciphersuites == NULL) {
		return BCTBX_ERROR_INVALID_INPUT_DATA;
	}

	mbedtls_ssl_conf_ciphersuites(ssl_config->ssl_config, ciphersuites);

	return 0;
}

void *bctbx_ssl_config_get_private_config(bctbx_ssl_config_t *ssl_config) {
	return (void *)ssl_config->ssl_config;
}

int32_t bctbx_ssl_config_set_authmode(bctbx_ssl_config_t *ssl_config, int authmode) {
	int mbedtls_authmode;
	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}

	/* remap input arguments */
	switch (authmode) {
		case BCTBX_SSL_VERIFY_NONE:
			mbedtls_authmode = MBEDTLS_SSL_VERIFY_NONE;
			break;
		case BCTBX_SSL_VERIFY_OPTIONAL:
			mbedtls_authmode = MBEDTLS_SSL_VERIFY_OPTIONAL;
			break;
		case BCTBX_SSL_VERIFY_REQUIRED:
			mbedtls_authmode = MBEDTLS_SSL_VERIFY_REQUIRED;
			break;
		default:
			return BCTBX_ERROR_INVALID_SSL_AUTHMODE;
			break;
	}

	mbedtls_ssl_conf_authmode(ssl_config->ssl_config, mbedtls_authmode);

	return 0;
}

int32_t bctbx_ssl_config_set_rng(bctbx_ssl_config_t *ssl_config, int(*rng_function)(void *, unsigned char *, size_t), void *rng_context) {
	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}

	mbedtls_ssl_conf_rng(ssl_config->ssl_config, rng_function, rng_context);

	return 0;
}

int32_t bctbx_ssl_config_set_callback_verify(bctbx_ssl_config_t *ssl_config, int(*callback_function)(void *, bctbx_x509_certificate_t *, int, uint32_t *), void *callback_data) {
	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}

	mbedtls_ssl_conf_verify(ssl_config->ssl_config, (int(*)(void *, mbedtls_x509_crt*, int, uint32_t *))callback_function, callback_data);

	return 0;
}

int32_t bctbx_ssl_config_set_callback_cli_cert(bctbx_ssl_config_t *ssl_config, int(*callback_function)(void *, bctbx_ssl_context_t *, unsigned char *, size_t), void *callback_data) {
	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}
	ssl_config->callback_cli_cert_function = callback_function;
	ssl_config->callback_cli_cert_data = callback_data;

	return 0;
}

int32_t bctbx_ssl_config_set_ca_chain(bctbx_ssl_config_t *ssl_config, bctbx_x509_certificate_t *ca_chain) {
	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}
	/* ca_crl (arg 3) is always set to null, add the functionnality if needed */
	mbedtls_ssl_conf_ca_chain(ssl_config->ssl_config, (mbedtls_x509_crt *)ca_chain, NULL);

	return 0;
}

int32_t bctbx_ssl_config_set_own_cert(bctbx_ssl_config_t *ssl_config, bctbx_x509_certificate_t *cert, bctbx_signing_key_t *key) {
	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}
	return mbedtls_ssl_conf_own_cert(ssl_config->ssl_config, (mbedtls_x509_crt *)cert, (mbedtls_pk_context *)key);
}


/** DTLS SRTP functions **/
#ifdef HAVE_DTLS_SRTP
int32_t bctbx_ssl_config_set_dtls_srtp_protection_profiles(bctbx_ssl_config_t *ssl_config, const bctbx_dtls_srtp_profile_t *profiles, size_t profiles_number) {
	size_t i;
	mbedtls_ssl_srtp_profile dtls_srtp_mbedtls_profiles[4];

	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}

	/* convert the profiles array into a mbedtls profiles array */
	for (i=0; i<profiles_number && i<4; i++) { /* 4 profiles defined max */
		dtls_srtp_mbedtls_profiles[i] = bctbx_srtp_profile_bctoolbox2mbedtls(profiles[i]);
	}
	for (;i<4; i++) { /* make sure to have harmless values in the rest of the array */
		dtls_srtp_mbedtls_profiles[i] = MBEDTLS_SRTP_UNSET_PROFILE;
	}

	return mbedtls_ssl_conf_dtls_srtp_protection_profiles(ssl_config->ssl_config, dtls_srtp_mbedtls_profiles, profiles_number);
}

#else /* HAVE_DTLS_SRTP */
int32_t bctbx_ssl_config_set_dtls_srtp_protection_profiles(bctbx_ssl_config_t *ssl_config, const bctbx_dtls_srtp_profile_t *profiles, size_t profiles_number) {
	return BCTBX_ERROR_UNAVAILABLE_FUNCTION;
}
#endif /* HAVE_DTLS_SRTP */
/** DTLS SRTP functions **/

int32_t bctbx_ssl_context_setup(bctbx_ssl_context_t *ssl_ctx, bctbx_ssl_config_t *ssl_config) {
	int ret;
	/* Check validity of context and config */
	if (ssl_config == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONFIG;
	}

	if (ssl_ctx == NULL) {
		return BCTBX_ERROR_INVALID_SSL_CONTEXT;
	}

	/* apply all valids settings to the ssl_context */
	if (ssl_config->callback_cli_cert_function != NULL) {
		ssl_ctx->callback_cli_cert_function = ssl_config->callback_cli_cert_function;
		ssl_ctx->callback_cli_cert_data = ssl_config->callback_cli_cert_data;
	}

#ifdef HAVE_DTLS_SRTP
	/* We do not use DTLS SRTP cookie, so we must set to NULL the callbacks. Cookies are used to prevent DoS attack but our server is on only when during a brief period so we do not need this */
	mbedtls_ssl_conf_dtls_cookies(ssl_config->ssl_config, NULL, NULL, NULL);
#endif /* HAVE_DTLS_SRTP */

	ret = mbedtls_ssl_setup(&(ssl_ctx->ssl_ctx), ssl_config->ssl_config);
	if (ret != 0) return ret;

	/* for DTLS handshake, we must set a timer callback */
	mbedtls_ssl_set_timer_cb(&(ssl_ctx->ssl_ctx), &(ssl_ctx->timer), mbedtls_timing_set_delay, mbedtls_timing_get_delay);

	return ret;
}

/*****************************************************************************/
/***** Hashing                                                           *****/
/*****************************************************************************/
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
void bctbx_hmacSha512(const uint8_t *key,
		size_t keyLength,
		const uint8_t *input,
		size_t inputLength,
		uint8_t hmacLength,
		uint8_t *output)
{
	uint8_t hmacOutput[64];
	mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA512), key, keyLength, input, inputLength, hmacOutput);

	/* check output length, can't be>64 */
	if (hmacLength>64) {
		memcpy(output, hmacOutput, 64);
	} else {
		memcpy(output, hmacOutput, hmacLength);
	}
}

/*
 * @brief HMAC-SHA384 wrapper
 * @param[in] 	key		HMAC secret key
 * @param[in] 	keyLength	HMAC key length
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length
 * @param[in]	hmacLength	Length of output required in bytes, HMAC output is truncated to the hmacLength left bytes. 48 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
void bctbx_hmacSha384(const uint8_t *key,
		size_t keyLength,
		const uint8_t *input,
		size_t inputLength,
		uint8_t hmacLength,
		uint8_t *output)
{
	uint8_t hmacOutput[48];
	mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA384), key, keyLength, input, inputLength, hmacOutput);

	/* check output length, can't be>48 */
	if (hmacLength>48) {
		memcpy(output, hmacOutput, 48);
	} else {
		memcpy(output, hmacOutput, hmacLength);
	}
}

/*
 * brief HMAC-SHA256 wrapper
 * @param[in] 	key			HMAC secret key
 * @param[in] 	keyLength	HMAC key length
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length
 * @param[in]	hmacLength	Length of output required in bytes, HMAC output is truncated to the hmacLength left bytes. 32 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
void bctbx_hmacSha256(const uint8_t *key,
		size_t keyLength,
		const uint8_t *input,
		size_t inputLength,
		uint8_t hmacLength,
		uint8_t *output)
{
	uint8_t hmacOutput[32];
	mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, keyLength, input, inputLength, hmacOutput);

	/* check output length, can't be>32 */
	if (hmacLength>32) {
		memcpy(output, hmacOutput, 32);
	} else {
		memcpy(output, hmacOutput, hmacLength);
	}
}

/*
 * @brief SHA512 wrapper
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length in bytes
 * @param[in]	hashLength	Length of output required in bytes, Output is truncated to the hashLength left bytes. 64 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
void bctbx_sha512(const uint8_t *input,
		size_t inputLength,
		uint8_t hashLength,
		uint8_t *output)
{
	uint8_t hashOutput[64];
	mbedtls_sha512(input, inputLength, hashOutput, 0); /* last param to zero to select SHA512 and not SHA384 */

	/* check output length, can't be>64 */
	if (hashLength>64) {
		memcpy(output, hashOutput, 64);
	} else {
		memcpy(output, hashOutput, hashLength);
	}
}

/*
 * @brief SHA384 wrapper
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length in bytes
 * @param[in]	hashLength	Length of output required in bytes, Output is truncated to the hashLength left bytes. 48 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
void bctbx_sha384(const uint8_t *input,
		size_t inputLength,
		uint8_t hashLength,
		uint8_t *output)
{
	uint8_t hashOutput[48];
	mbedtls_sha512(input, inputLength, hashOutput, 1); /* last param to one to select SHA384 and not SHA512 */

	/* check output length, can't be>48 */
	if (hashLength>48) {
		memcpy(output, hashOutput, 48);
	} else {
		memcpy(output, hashOutput, hashLength);
	}
}

/*
 * @brief SHA256 wrapper
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length in bytes
 * @param[in]	hashLength	Length of output required in bytes, Output is truncated to the hashLength left bytes. 32 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
void bctbx_sha256(const uint8_t *input,
		size_t inputLength,
		uint8_t hashLength,
		uint8_t *output)
{
	uint8_t hashOutput[32];
	mbedtls_sha256(input, inputLength, hashOutput, 0); /* last param to zero to select SHA256 and not SHA224 */

	/* check output length, can't be>32 */
	if (hashLength>32) {
		memcpy(output, hashOutput, 32);
	} else {
		memcpy(output, hashOutput, hashLength);
	}
}

/*
 * @brief HMAC-SHA1 wrapper
 * @param[in] 	key			HMAC secret key
 * @param[in] 	keyLength	HMAC key length
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length
 * @param[in]	hmacLength	Length of output required in bytes, HMAC output is truncated to the hmacLength left bytes. 20 bytes maximum
 * @param[out]	output		Output data buffer.
 *
 */
void bctbx_hmacSha1(const uint8_t *key,
		size_t keyLength,
		const uint8_t *input,
		size_t inputLength,
		uint8_t hmacLength,
		uint8_t *output)
{
	uint8_t hmacOutput[20];

	mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), key, keyLength, input, inputLength, hmacOutput);

	/* check output length, can't be>20 */
	if (hmacLength>20) {
		memcpy(output, hmacOutput, 20);
	} else {
		memcpy(output, hmacOutput, hmacLength);
	}
}

/**
 * @brief MD5 wrapper
 * output = md5(input)
 * @param[in]	input 		Input data buffer
 * @param[in]   inputLength	Input data length in bytes
 * @param[out]	output		Output data buffer.
 *
 */
void bctbx_md5(const uint8_t *input,
		size_t inputLength,
		uint8_t output[16])
{
	mbedtls_md5(input, inputLength, output);
}

/*****************************************************************************/
/***** Encryption/Decryption                                             *****/
/*****************************************************************************/
/***** GCM *****/
/**
 * @Brief AES-GCM encrypt and tag buffer
 *
 * @param[in]	key							Encryption key
 * @param[in]	keyLength					Key buffer length, in bytes, must be 16,24 or 32
 * @param[in]	plainText					Buffer to be encrypted
 * @param[in]	plainTextLength				Length in bytes of buffer to be encrypted
 * @param[in]	authenticatedData			Buffer holding additional data to be used in tag computation
 * @param[in]	authenticatedDataLength		Additional data length in bytes
 * @param[in]	initializationVector		Buffer holding the initialisation vector
 * @param[in]	initializationVectorLength	Initialisation vector length in bytes
 * @param[out]	tag							Buffer holding the generated tag
 * @param[in]	tagLength					Requested length for the generated tag
 * @param[out]	output						Buffer holding the output, shall be at least the length of plainText buffer
 */
int32_t bctbx_aes_gcm_encrypt_and_tag(const uint8_t *key, size_t keyLength,
		const uint8_t *plainText, size_t plainTextLength,
		const uint8_t *authenticatedData, size_t authenticatedDataLength,
		const uint8_t *initializationVector, size_t initializationVectorLength,
		uint8_t *tag, size_t tagLength,
		uint8_t *output) {
	mbedtls_gcm_context gcmContext;
	int ret;

	mbedtls_gcm_init(&gcmContext);
	ret = mbedtls_gcm_setkey(&gcmContext, MBEDTLS_CIPHER_ID_AES, key, (unsigned int)keyLength*8);
	if (ret != 0) return ret;

	ret = mbedtls_gcm_crypt_and_tag(&gcmContext, MBEDTLS_GCM_ENCRYPT, plainTextLength, initializationVector, initializationVectorLength, authenticatedData, authenticatedDataLength, plainText, output, tagLength, tag);
	mbedtls_gcm_free(&gcmContext);

	return ret;
}

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
 * @return 0 on succes, BCTBX_ERROR_AUTHENTICATION_FAILED if tag doesn't match or mbedtls error code
 */
int32_t bctbx_aes_gcm_decrypt_and_auth(const uint8_t *key, size_t keyLength,
		const uint8_t *cipherText, size_t cipherTextLength,
		const uint8_t *authenticatedData, size_t authenticatedDataLength,
		const uint8_t *initializationVector, size_t initializationVectorLength,
		const uint8_t *tag, size_t tagLength,
		uint8_t *output) {
	mbedtls_gcm_context gcmContext;
	int ret;

	mbedtls_gcm_init(&gcmContext);
	ret = mbedtls_gcm_setkey(&gcmContext, MBEDTLS_CIPHER_ID_AES, key, (unsigned int)keyLength*8);
	if (ret != 0) return ret;

	ret = mbedtls_gcm_auth_decrypt(&gcmContext, cipherTextLength, initializationVector, initializationVectorLength, authenticatedData, authenticatedDataLength, tag, tagLength, cipherText, output);
	mbedtls_gcm_free(&gcmContext);

	if (ret == MBEDTLS_ERR_GCM_AUTH_FAILED) {
		return BCTBX_ERROR_AUTHENTICATION_FAILED;
	}

	return ret;
}


/**
 * @Brief create and initialise an AES-GCM encryption context
 *
 * @param[in]	key							encryption key
 * @param[in]	keyLength					key buffer length, in bytes, must be 16,24 or 32
 * @param[in]	authenticatedData			Buffer holding additional data to be used in tag computation (can be NULL)
 * @param[in]	authenticatedDataLength		additional data length in bytes (can be 0)
 * @param[in]	initializationVector		Buffer holding the initialisation vector
 * @param[in]	initializationVectorLength	Initialisation vector length in bytes
 * @param[in]	mode						Operation mode : BCTBX_GCM_ENCRYPT or BCTBX_GCM_DECRYPT
 *
 * @return 0 on success, crypto library error code otherwise
 */
bctbx_aes_gcm_context_t *bctbx_aes_gcm_context_new(const uint8_t *key, size_t keyLength,
		const uint8_t *authenticatedData, size_t authenticatedDataLength,
		const uint8_t *initializationVector, size_t initializationVectorLength,
		uint8_t mode) {

	int ret = 0;
	int mbedtls_mode;
	mbedtls_gcm_context *ctx = NULL;

	if (mode == BCTBX_GCM_ENCRYPT) {
		mbedtls_mode = MBEDTLS_GCM_ENCRYPT;
	} else if ( mode == BCTBX_GCM_DECRYPT) {
		mbedtls_mode = MBEDTLS_GCM_DECRYPT;
	} else {
		return NULL;
	}

	ctx = bctbx_malloc0(sizeof(mbedtls_gcm_context));
	mbedtls_gcm_init(ctx);
	ret = mbedtls_gcm_setkey(ctx, MBEDTLS_CIPHER_ID_AES, key, (unsigned int)keyLength*8);
	if (ret != 0) {
		bctbx_free(ctx);
		return NULL;
	}

	ret = mbedtls_gcm_starts(ctx, mbedtls_mode, initializationVector, initializationVectorLength, authenticatedData, authenticatedDataLength);
	if (ret != 0) {
		bctbx_free(ctx);
		return NULL;
	}

	return (bctbx_aes_gcm_context_t *)ctx;
}

/**
 * @Brief AES-GCM encrypt or decrypt a chunk of data
 *
 * @param[in/out]	context			a context already initialized using bctbx_aes_gcm_context_new
 * @param[in]		input			buffer holding the input data
 * @param[in]		inputLength		lenght of the input data
 * @param[out]		output			buffer to store the output data (same length as input one)
 *
 * @return 0 on success, crypto library error code otherwise
 */
int32_t bctbx_aes_gcm_process_chunk(bctbx_aes_gcm_context_t *context,
		const uint8_t *input, size_t inputLength,
		uint8_t *output) {
	return mbedtls_gcm_update((mbedtls_gcm_context *)context, inputLength, input, output);
}

/**
 * @Brief Conclude a AES-GCM encryption stream, generate tag if requested, free resources
 *
 * @param[in/out]	context			a context already initialized using bctbx_aes_gcm_context_new
 * @param[out]		tag				a buffer to hold the authentication tag. Can be NULL if tagLength is 0
 * @param[in]		tagLength		length of reqested authentication tag, max 16
 *
 * @return 0 on success, crypto library error code otherwise
 */
int32_t bctbx_aes_gcm_finish(bctbx_aes_gcm_context_t *context,
		uint8_t *tag, size_t tagLength) {
	int ret;

	ret = mbedtls_gcm_finish((mbedtls_gcm_context *)context, tag, tagLength);
	mbedtls_gcm_free((mbedtls_gcm_context *)context);

	bctbx_free(context);

	return ret;
}

/*
 * @brief Wrapper for AES-128 in CFB128 mode encryption
 * Both key and IV must be 16 bytes long, IV is not updated
 *
 * @param[in]	key			encryption key, 128 bits long
 * @param[in]	IV			Initialisation vector, 128 bits long, is not modified by this function.
 * @param[in]	input		Input data buffer
 * @param[in]	inputLength	Input data length
 * @param[out]	output		Output data buffer
 *
 */
void bctbx_aes128CfbEncrypt(const uint8_t key[16],
		const uint8_t IV[16],
		const uint8_t *input,
		size_t inputLength,
		uint8_t *output)
{
	uint8_t IVbuffer[16];
	size_t iv_offset=0; /* is not used by us but needed and updated by mbedtls */
	mbedtls_aes_context context;
	memset (&context, 0, sizeof(mbedtls_aes_context));

	/* make a local copy of IV which is modified by the mbedtls AES-CFB function */
	memcpy(IVbuffer, IV, 16*sizeof(uint8_t));

	/* initialise the aes context and key */
	mbedtls_aes_setkey_enc(&context, key, 128);

	/* encrypt */
	mbedtls_aes_crypt_cfb128 (&context, MBEDTLS_AES_ENCRYPT, inputLength, &iv_offset, IVbuffer, input, output);
}

/*
 * @brief Wrapper for AES-128 in CFB128 mode decryption
 * Both key and IV must be 16 bytes long, IV is not updated
 *
 * @param[in]	key			decryption key, 128 bits long
 * @param[in]	IV			Initialisation vector, 128 bits long, is not modified by this function.
 * @param[in]	input		Input data buffer
 * @param[in]	inputLength	Input data length
 * @param[out]	output		Output data buffer
 *
 */
void bctbx_aes128CfbDecrypt(const uint8_t key[16],
		const uint8_t IV[16],
		const uint8_t *input,
		size_t inputLength,
		uint8_t *output)
{
	uint8_t IVbuffer[16];
	size_t iv_offset=0; /* is not used by us but needed and updated by mbedtls */
	mbedtls_aes_context context;
	memset (&context, 0, sizeof(mbedtls_aes_context));

	/* make a local copy of IV which is modified by the mbedtls AES-CFB function */
	memcpy(IVbuffer, IV, 16*sizeof(uint8_t));

	/* initialise the aes context and key - use the aes_setkey_enc function as requested by the documentation of aes_crypt_cfb128 function */
	mbedtls_aes_setkey_enc(&context, key, 128);

	/* encrypt */
	mbedtls_aes_crypt_cfb128 (&context, MBEDTLS_AES_DECRYPT, inputLength, &iv_offset, IVbuffer, input, output);
}

/*
 * @brief Wrapper for AES-256 in CFB128 mode encryption
 * The key must be 32 bytes long and the IV must be 16 bytes long, IV is not updated
 *
 * @param[in]	key			encryption key, 256 bits long
 * @param[in]	IV			Initialisation vector, 128 bits long, is not modified by this function.
 * @param[in]	input		Input data buffer
 * @param[in]	inputLength	Input data length
 * @param[out]	output		Output data buffer
 *
 */
void bctbx_aes256CfbEncrypt(const uint8_t key[32],
		const uint8_t IV[16],
		const uint8_t *input,
		size_t inputLength,
		uint8_t *output)
{
	uint8_t IVbuffer[16];
	size_t iv_offset=0;
	mbedtls_aes_context context;

	memcpy(IVbuffer, IV, 16*sizeof(uint8_t));
	memset (&context, 0, sizeof(mbedtls_aes_context));
	mbedtls_aes_setkey_enc(&context, key, 256);

	/* encrypt */
	mbedtls_aes_crypt_cfb128 (&context, MBEDTLS_AES_ENCRYPT, inputLength, &iv_offset, IVbuffer, input, output);
}

/*
 * @brief Wrapper for AES-256 in CFB128 mode decryption
 * The key must be 32 bytes long and the IV must be 16 bytes long, IV is not updated
 *
 * @param[in]	key			decryption key, 256 bits long
 * @param[in]	IV			Initialisation vector, 128 bits long, is not modified by this function.
 * @param[in]	input		Input data buffer
 * @param[in]	inputLength	Input data length
 * @param[out]	output		Output data buffer
 *
 */
void bctbx_aes256CfbDecrypt(const uint8_t key[32],
		const uint8_t IV[16],
		const uint8_t *input,
		size_t inputLength,
		uint8_t *output)
{
	uint8_t IVbuffer[16];
	size_t iv_offset=0;
	mbedtls_aes_context context;

	memcpy(IVbuffer, IV, 16*sizeof(uint8_t));
	memset (&context, 0, sizeof(mbedtls_aes_context));
	mbedtls_aes_setkey_enc(&context, key, 256);

	/* decrypt */
	mbedtls_aes_crypt_cfb128 (&context, MBEDTLS_AES_DECRYPT, inputLength, &iv_offset, IVbuffer, input, output);
}
