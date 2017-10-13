//
//  MCCertificateUtils.cc
//  mailcore2
//
//  Created by DINH Viêt Hoà on 7/25/13.
//  Copyright (c) 2013 MailCore. All rights reserved.
//

#include "MCCertificateUtils.h"

#if __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#else
#include <string>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#endif

#if defined(ANDROID) || defined(__ANDROID__)
#include <dirent.h>
#endif

#include "MCLog.h"

bool mailcore::checkCertificate(mailstream *stream, String *hostname)
{
#if __APPLE__
    bool result = false;
    CFStringRef hostnameCFString;
    SecPolicyRef policy;
    CFMutableArrayRef certificates;
    SecTrustRef trust = NULL;
    SecTrustResultType trustResult;
    OSStatus status;

    carray *cCerts = mailstream_get_certificate_chain(stream);
    if (cCerts == NULL)
    {
        fprintf(stderr, "warning: No certificate chain retrieved");
        goto err;
    }

    hostnameCFString = CFStringCreateWithCharacters(NULL, (const UniChar *)hostname->unicodeCharacters(),
                                                    hostname->length());
    policy = SecPolicyCreateSSL(true, hostnameCFString);
    certificates = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);

    for (unsigned int i = 0; i < carray_count(cCerts); i++)
    {
        MMAPString *str;
        str = (MMAPString *)carray_get(cCerts, i);
        CFDataRef data = CFDataCreate(NULL, (const UInt8 *)str->str, (CFIndex)str->len);
        SecCertificateRef cert = SecCertificateCreateWithData(NULL, data);
        CFArrayAppendValue(certificates, cert);
        CFRelease(data);
        CFRelease(cert);
    }

    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

    // The below API calls are not thread safe. We're making sure not to call the concurrently.
    pthread_mutex_lock(&lock);

    status = SecTrustCreateWithCertificates(certificates, policy, &trust);
    if (status != noErr)
    {
        pthread_mutex_unlock(&lock);
        goto free_certs;
    }

    status = SecTrustEvaluate(trust, &trustResult);
    if (status != noErr)
    {
        pthread_mutex_unlock(&lock);
        goto free_certs;
    }

    pthread_mutex_unlock(&lock);

    switch (trustResult)
    {
    case kSecTrustResultUnspecified:
    case kSecTrustResultProceed:
        // certificate chain is ok
        result = true;
        break;

    default:
        // certificate chain is invalid
        break;
    }

    CFRelease(trust);
free_certs:
    CFRelease(certificates);
    mailstream_certificate_chain_free(cCerts);
    CFRelease(policy);
    CFRelease(hostnameCFString);
err:
    return result;
#else
    bool result = false;
    X509_STORE *store = NULL;
    X509_STORE_CTX *storectx = NULL;
    STACK_OF(X509) *certificates = NULL;
#if defined(ANDROID) || defined(__ANDROID__)
    DIR *dir = NULL;
    struct dirent *ent = NULL;
    FILE *f = NULL;
#endif
    int status;

    std::string certificatePaths[] = {
        // Debian, Ubuntu, Arch: maintained by update-ca-certificates
        "/etc/ssl/certs/ca-certificates.crt",
        // Red Hat 5+, Fedora, Centos
        "/etc/pki/tls/certs/ca-bundle.crt",
        // Red Hat 4
        "/usr/share/ssl/certs/ca-bundle.crt",
        // FreeBSD (security/ca-root-nss package)
        "/usr/local/share/certs/ca-root-nss.crt",
        // FreeBSD (deprecated security/ca-root package, removed 2008)
        "/usr/local/share/certs/ca-root.crt",
        // FreeBSD (optional symlink)
        // OpenBSD
        "/etc/ssl/cert.pem",
        // OpenSUSE
        "/etc/ssl/ca-bundle.pem",
    };

    OpenSSL_add_all_algorithms();
    ERR_load_BIO_strings();
    ERR_load_crypto_strings();

    carray *cCerts = mailstream_get_certificate_chain(stream);
    if (cCerts == NULL)
    {
        fprintf(stderr, "warning: No certificate chain retrieved");
        goto err;
    }

    store = X509_STORE_new();
    if (store == NULL)
    {
        fprintf(stderr, "Error creating X509_STORE_CTX object");
        goto free_certs;
    }

#ifdef _MSC_VER
    HCERTSTORE systemStore = CertOpenSystemStore(NULL, L"ROOT");

    PCCERT_CONTEXT previousCert = NULL;
    while (1)
    {
        PCCERT_CONTEXT nextCert = CertEnumCertificatesInStore(systemStore, previousCert);
        if (nextCert == NULL)
        {
            break;
        }
        X509 *openSSLCert = d2i_X509(NULL, (const unsigned char **)&nextCert->pbCertEncoded, nextCert->cbCertEncoded);
        if (openSSLCert != NULL)
        {
            X509_STORE_add_cert(store, openSSLCert);
            X509_free(openSSLCert);
        }
        previousCert = nextCert;
    }
    CertCloseStore(systemStore, 0);
#elif defined(ANDROID) || defined(__ANDROID__)
    dir = opendir("/system/etc/security/cacerts");
    while (ent = readdir(dir))
    {
        if (ent->d_name[0] == '.')
        {
            continue;
        }
        char filename[1024];
        snprintf(filename, sizeof(filename), "/system/etc/security/cacerts/%s", ent->d_name);
        f = fopen(filename, "rb");
        if (f != NULL)
        {
            X509 *cert = PEM_read_X509(f, NULL, NULL, NULL);
            if (cert != NULL)
            {
                X509_STORE_add_cert(store, cert);
                X509_free(cert);
            }
            fclose(f);
        }
    }
    closedir(dir);
#endif

    status = X509_STORE_set_default_paths(store);
    if (status != 1)
    {
        fprintf(stderr, "Error loading the system-wide CA certificates");
    }

    for (const auto path : certificatePaths)
    {
        if (X509_STORE_load_locations(store, path.c_str(), NULL) == 1)
        { // loaded successfully
            break;
        }
    }

    certificates = sk_X509_new_null();
    for (unsigned int i = 0; i < carray_count(cCerts); i++)
    {
        MMAPString *str;
        str = (MMAPString *)carray_get(cCerts, i);
        if (str == NULL)
        {
            fprintf(stderr, "Could not read carray_get cert");
            goto free_certs;
        }
        BIO *bio = BIO_new_mem_buf((void *)str->str, str->len);
        X509 *certificate = d2i_X509_bio(bio, NULL);
        BIO_free(bio);
        if (!sk_X509_push(certificates, certificate))
        {
            fprintf(stderr, "Could not append certificate via sk_X509_push");
            goto free_certs;
        }
    }

    storectx = X509_STORE_CTX_new();
    if (storectx == NULL)
    {
        fprintf(stderr, "Could not call X509_STORE_CTX_new");
        goto free_certs;
    }

    status = X509_STORE_CTX_init(storectx, store, sk_X509_value(certificates, 0), certificates);
    if (status != 1)
    {
        fprintf(stderr, "Could not call X509_STORE_CTX_init");
        goto free_certs;
    }

    status = X509_verify_cert(storectx);
    if (status == 1)
    {
        result = true;
    }
    else
    {
        fprintf(stderr, "Verification failed:\n");
        fprintf(stderr, "X509_verify_cert_error_string:\n");
        fprintf(stderr, X509_verify_cert_error_string(storectx->error));
        /*  get the offending certificate causing the failure */
        X509 *error_cert = X509_STORE_CTX_get_current_cert(storectx);
        X509_NAME *certsubject = X509_NAME_new();
        certsubject = X509_get_subject_name(error_cert);
        fprintf(stderr, "X509_get_subject_name:\n");
        BIO *outbio = BIO_new_fp(stderr, BIO_NOCLOSE);
        X509_NAME_print_ex(outbio, certsubject, 0, XN_FLAG_MULTILINE);
        BIO_printf(outbio, "\n");
        BIO_free_all(outbio);
    }

free_certs:
    mailstream_certificate_chain_free(cCerts);
    if (certificates != NULL)
    {
        sk_X509_pop_free((STACK_OF(X509) *)certificates, X509_free);
    }
    if (storectx != NULL)
    {
        X509_STORE_CTX_free(storectx);
    }
    if (store != NULL)
    {
        X509_STORE_free(store);
    }
err:
    return result;
#endif
}
