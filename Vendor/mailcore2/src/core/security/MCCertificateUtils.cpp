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
#include <vector>
#include <cstdlib>
#include <cstring>
#include <openssl/opensslv.h>
#include <openssl/bio.h>
#include <openssl/ossl_typ.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/stack.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#endif

#if defined(ANDROID) || defined(__ANDROID__)
#include <dirent.h>
#endif

#include "MCLog.h"

bool mailcore::checkCertificate(mailstream *stream, String *hostname)
{
    fprintf(stderr, "WindowsDebug: checkCertificate called, stream=%p\n", (void*)stream);
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
        MCLog("warning: No certificate chain retrieved");
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

    // Build list of paths to check
    std::vector<std::string> certificatePaths;

#if !defined(_MSC_VER) && !defined(ANDROID) && !defined(__ANDROID__)
    // First, check the SSL_CERT_FILE environment variable (standard override)
    const char* sslCertFile = getenv("SSL_CERT_FILE");
    if (sslCertFile != nullptr && strlen(sslCertFile) > 0) {
        certificatePaths.push_back(std::string(sslCertFile));
    }
#endif
    
    // Standard system paths
    // Debian, Ubuntu, Arch: maintained by update-ca-certificates
    certificatePaths.push_back("/etc/ssl/certs/ca-certificates.crt");
    // Red Hat 5+, Fedora, Centos
    certificatePaths.push_back("/etc/pki/tls/certs/ca-bundle.crt");
    // Red Hat 4
    certificatePaths.push_back("/usr/share/ssl/certs/ca-bundle.crt");
    // FreeBSD (security/ca-root-nss package)
    certificatePaths.push_back("/usr/local/share/certs/ca-root-nss.crt");
    // FreeBSD (deprecated security/ca-root package, removed 2008)
    certificatePaths.push_back("/usr/local/share/certs/ca-root.crt");
    // OpenBSD
    certificatePaths.push_back("/etc/ssl/cert.pem");
    // OpenSUSE
    certificatePaths.push_back("/etc/ssl/ca-bundle.pem");

    MCLog("OpenSSL version: %s", OpenSSL_version(0));

    fprintf(stderr, "WindowsDebug: Calling mailstream_get_certificate_chain\n");
    carray *cCerts = mailstream_get_certificate_chain(stream);
    if (cCerts == NULL)
    {
        fprintf(stderr, "WindowsDebug: ERROR - mailstream_get_certificate_chain returned NULL\n");
        MCLog("warning: No certificate chain retrieved");
        goto err;
    }
    fprintf(stderr, "WindowsDebug: Got certificate chain with %d certificates\n", carray_count(cCerts));

    store = X509_STORE_new();
    if (store == NULL)
    {
        MCLog("Error creating X509_STORE_CTX object");
        goto free_certs;
    }

#ifdef _MSC_VER
    fprintf(stderr, "WindowsDebug: Opening Windows ROOT certificate store\n");
    HCERTSTORE systemStore = CertOpenSystemStore(NULL, L"ROOT");
    if (systemStore == NULL) {
        fprintf(stderr, "WindowsDebug: ERROR - CertOpenSystemStore returned NULL, GetLastError=%lu\n", GetLastError());
    } else {
        fprintf(stderr, "WindowsDebug: Successfully opened Windows ROOT certificate store\n");
    }

    PCCERT_CONTEXT previousCert = NULL;
    int certCount = 0;
    int certLoadedCount = 0;
    int certFailedCount = 0;
    while (1)
    {
        PCCERT_CONTEXT nextCert = CertEnumCertificatesInStore(systemStore, previousCert);
        if (nextCert == NULL)
        {
            break;
        }
        certCount++;

        // Important: d2i_X509 advances the pointer, so we need to use a copy
        const unsigned char *certData = nextCert->pbCertEncoded;
        X509 *openSSLCert = d2i_X509(NULL, &certData, nextCert->cbCertEncoded);
        if (openSSLCert != NULL)
        {
            int addResult = X509_STORE_add_cert(store, openSSLCert);
            if (addResult == 1) {
                certLoadedCount++;
            } else {
                // Note: X509_STORE_add_cert returns 0 for duplicates, which is not an error
                unsigned long err = ERR_peek_last_error();
                if (ERR_GET_REASON(err) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
                    certLoadedCount++; // Count duplicates as loaded
                } else {
                    certFailedCount++;
                }
            }
            X509_free(openSSLCert);
        }
        else
        {
            certFailedCount++;
            unsigned long err = ERR_get_error();
            char errBuf[256];
            ERR_error_string_n(err, errBuf, sizeof(errBuf));
            if (certCount <= 5) { // Only log first 5 failures to avoid spam
                fprintf(stderr, "WindowsDebug: d2i_X509 failed for cert #%d: %s\n", certCount, errBuf);
            }
        }
        previousCert = nextCert;
    }
    fprintf(stderr, "WindowsDebug: Windows cert store: enumerated %d certs, loaded %d, failed %d\n",
            certCount, certLoadedCount, certFailedCount);
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
        MCLog("Error loading the system-wide CA certificates");
    }

    for (const auto& path : certificatePaths)
    {
        size_t len = path.length();
        bool isFile = (len > 4 && path.substr(len - 4) == ".crt") ||
                      (len > 4 && path.substr(len - 4) == ".pem");
        int loaded = isFile
            ? X509_STORE_load_locations(store, path.c_str(), NULL)
            : X509_STORE_load_locations(store, NULL, path.c_str());
        if (loaded == 1) {
            break;
        }
    }

    fprintf(stderr, "WindowsDebug: Parsing server certificate chain\n");
    certificates = sk_X509_new_null();
    for (unsigned int i = 0; i < carray_count(cCerts); i++)
    {
        MMAPString *str;
        str = (MMAPString *)carray_get(cCerts, i);
        if (str == NULL)
        {
            fprintf(stderr, "WindowsDebug: ERROR - carray_get returned NULL for cert %d\n", i);
            MCLog("Could not read carray_get cert");
            goto free_certs;
        }
        fprintf(stderr, "WindowsDebug: Parsing server cert %d, length=%zu\n", i, str->len);
        BIO *bio = BIO_new_mem_buf((void *)str->str, str->len);
        X509 *certificate = d2i_X509_bio(bio, NULL);
        BIO_free(bio);
        if (certificate == NULL) {
            unsigned long err = ERR_get_error();
            char errBuf[256];
            ERR_error_string_n(err, errBuf, sizeof(errBuf));
            fprintf(stderr, "WindowsDebug: ERROR - d2i_X509_bio failed for server cert %d: %s\n", i, errBuf);
            goto free_certs;
        }
        if (!sk_X509_push(certificates, certificate))
        {
            fprintf(stderr, "WindowsDebug: ERROR - sk_X509_push failed for cert %d\n", i);
            MCLog("Could not append certificate via sk_X509_push");
            goto free_certs;
        }
        fprintf(stderr, "WindowsDebug: Successfully parsed server cert %d\n", i);
    }
    fprintf(stderr, "WindowsDebug: Successfully parsed all %d server certificates\n", carray_count(cCerts));

    storectx = X509_STORE_CTX_new();
    if (storectx == NULL)
    {
        MCLog("Could not call X509_STORE_CTX_new");
        goto free_certs;
    }

    status = X509_STORE_CTX_init(storectx, store, sk_X509_value(certificates, 0), certificates);
    if (status != 1)
    {
        MCLog("Could not call X509_STORE_CTX_init");
        goto free_certs;
    }

    fprintf(stderr, "WindowsDebug: Calling X509_verify_cert\n");
    status = X509_verify_cert(storectx);
    fprintf(stderr, "WindowsDebug: X509_verify_cert returned %d\n", status);
    if (status == 1)
    {
        fprintf(stderr, "WindowsDebug: Certificate verification SUCCEEDED\n");
        result = true;
    }
    else
    {
        int errcode = X509_STORE_CTX_get_error(storectx);
        int errDepth = X509_STORE_CTX_get_error_depth(storectx);
        const char *errString = X509_verify_cert_error_string(errcode);
        fprintf(stderr, "WindowsDebug: Certificate verification FAILED: error=%d (%s), depth=%d\n",
                errcode, errString ? errString : "unknown", errDepth);
        MCLog("Verification failed:\n");
        MCLog("X509_verify_cert_error_string:\n");
        MCLog(X509_verify_cert_error_string(errcode));
        /*  get the offending certificate causing the failure */
        X509 *error_cert = X509_STORE_CTX_get_current_cert(storectx);
        if (error_cert != NULL) {
            X509_NAME *certsubject = X509_get_subject_name(error_cert);
            MCLog("X509_get_subject_name:\n");
            BIO *outbio = BIO_new_fp(stderr, BIO_NOCLOSE);
            X509_NAME_print_ex(outbio, certsubject, 0, XN_FLAG_MULTILINE);
            BIO_printf(outbio, "\n");
            BIO_free_all(outbio);
        } else {
            fprintf(stderr, "WindowsDebug: No error certificate available\n");
        }
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
