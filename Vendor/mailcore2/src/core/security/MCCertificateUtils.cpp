//
//  MCCertificateUtils.cc
//  mailcore2
//
//  Created by DINH Viêt Hoà on 7/25/13.
//  Copyright (c) 2013 MailCore. All rights reserved.
//

#ifdef _MSC_VER
// Exposes CERT_CHAIN_PARA.dwUrlRetrievalTimeout; must precede the first <wincrypt.h>.
#define CERT_CHAIN_PARA_HAS_EXTRA_FIELDS
#endif

#include "MCCertificateUtils.h"

#if __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(_MSC_VER)
#include <windows.h>
#include <wincrypt.h>
#ifndef SECURITY_FLAG_IGNORE_REVOCATION
#define SECURITY_FLAG_IGNORE_REVOCATION 0x00000080 // <wininet.h>
#endif
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
#include <openssl/x509v3.h>
#include <openssl/stack.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#endif

#if defined(ANDROID) || defined(__ANDROID__)
#include <dirent.h>
#endif

#include "MCLog.h"

#ifdef _MSC_VER
/*
 Copying the ROOT store into OpenSSL misses everything Windows uses to distrust a CA: the
 Disallowed store, the auto-updated disallowedcert.stl, and revocation. Microsoft distrusted the
 compromised eMudhra intermediates (https://emudhra.we-have-a-problem.com/) through
 disallowedcert.stl and the CAs revoked them by CRL, which only the chain engine sees. It also
 sees enterprise and group policy roots and downloads Microsoft program roots that aren't cached
 yet. The SSL policy checks the hostname, as SecPolicyCreateSSL does on macOS.

 Revocation is soft-fail: when no CRL or OCSP answer is available (offline, captive portal, a
 private CA or TLS-inspecting antivirus without CRL URLs) the chain is still accepted, but a
 chain that is revoked or explicitly distrusted is always rejected.
*/
static bool checkCertificateWithCryptoAPI(carray *cCerts, mailcore::String *hostname)
{
    bool result = false;
    HCERTSTORE peerStore = NULL;
    PCCERT_CONTEXT leaf = NULL;
    PCCERT_CHAIN_CONTEXT chain = NULL;

    if (carray_count(cCerts) == 0 || hostname == NULL || hostname->length() == 0) {
        MCLog("warning: Empty certificate chain or hostname");
        return false;
    }

    // The certificates the server sent are only candidates for building the path. Trust still
    // has to come from the system stores.
    peerStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
    if (peerStore == NULL) {
        return false;
    }
    for (unsigned int i = 0; i < carray_count(cCerts); i++) {
        MMAPString *str = (MMAPString *)carray_get(cCerts, i);
        BOOL added = CertAddEncodedCertificateToStore(peerStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                      (const BYTE *)str->str, (DWORD)str->len,
                                                      CERT_STORE_ADD_ALWAYS, i == 0 ? &leaf : NULL);
        if (!added && i == 0) {
            MCLog("Could not decode the server certificate (0x%08lx)", GetLastError());
            goto done;
        }
    }

    {
        LPSTR serverAuth[] = {(LPSTR)szOID_PKIX_KP_SERVER_AUTH};
        CERT_CHAIN_PARA chainPara;
        ZeroMemory(&chainPara, sizeof(chainPara));
        chainPara.cbSize = sizeof(chainPara);
        chainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        chainPara.RequestedUsage.Usage.cUsageIdentifier = 1;
        chainPara.RequestedUsage.Usage.rgpszUsageIdentifier = serverAuth;
        // Bounds the total CRL/OCSP/AIA fetch time on networks that blackhole those URLs.
        chainPara.dwUrlRetrievalTimeout = 15000;

        DWORD flags = CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | CERT_CHAIN_REVOCATION_ACCUMULATIVE_TIMEOUT;
        if (!CertGetCertificateChain(NULL, leaf, NULL, peerStore, &chainPara, flags, NULL, &chain)) {
            MCLog("CertGetCertificateChain failed (0x%08lx)", GetLastError());
            goto done;
        }
    }

    {
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA sslPara;
        ZeroMemory(&sslPara, sizeof(sslPara));
        sslPara.cbSize = sizeof(sslPara);
        sslPara.dwAuthType = AUTHTYPE_SERVER;
        sslPara.pwszServerName = (WCHAR *)hostname->unicodeCharacters();
        sslPara.fdwChecks = SECURITY_FLAG_IGNORE_REVOCATION;

        CERT_CHAIN_POLICY_PARA policyPara;
        ZeroMemory(&policyPara, sizeof(policyPara));
        policyPara.cbSize = sizeof(policyPara);
        policyPara.dwFlags = CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS;
        policyPara.pvExtraPolicyPara = &sslPara;

        CERT_CHAIN_POLICY_STATUS policyStatus;
        ZeroMemory(&policyStatus, sizeof(policyStatus));
        policyStatus.cbSize = sizeof(policyStatus);
        if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policyPara, &policyStatus)) {
            MCLog("CertVerifyCertificateChainPolicy failed (0x%08lx)", GetLastError());
            goto done;
        }

        DWORD neverIgnored = CERT_TRUST_IS_REVOKED | CERT_TRUST_IS_EXPLICIT_DISTRUST;
        result = policyStatus.dwError == 0 && (chain->TrustStatus.dwErrorStatus & neverIgnored) == 0;
        if (!result) {
            MCLog("Verification failed: policy error 0x%08lx, chain status 0x%08lx",
                  policyStatus.dwError, chain->TrustStatus.dwErrorStatus);
            PCERT_SIMPLE_CHAIN simple = chain->rgpChain[0];
            for (DWORD i = 0; i < simple->cElement; i++) {
                char name[512] = {0};
                CertGetNameStringA(simple->rgpElement[i]->pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, name, sizeof(name));
                MCLog("  %s (error 0x%08lx)", name, simple->rgpElement[i]->TrustStatus.dwErrorStatus);
            }
        }
    }

done:
    if (chain != NULL) CertFreeCertificateChain(chain);
    if (leaf != NULL) CertFreeCertificateContext(leaf);
    CertCloseStore(peerStore, 0);
    return result;
}
#endif

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
#elif defined(_MSC_VER)
    carray *cCerts = mailstream_get_certificate_chain(stream);
    if (cCerts == NULL)
    {
        MCLog("warning: No certificate chain retrieved");
        return false;
    }
    bool result = checkCertificateWithCryptoAPI(cCerts, hostname);
    mailstream_certificate_chain_free(cCerts);
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

    carray *cCerts = mailstream_get_certificate_chain(stream);
    if (cCerts == NULL)
    {
        MCLog("warning: No certificate chain retrieved");
        goto err;
    }

    store = X509_STORE_new();
    if (store == NULL)
    {
        MCLog("Error creating X509_STORE_CTX object");
        goto free_certs;
    }

#if defined(ANDROID) || defined(__ANDROID__)
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

    /* Clear the OpenSSL error queue. The operations above (X509_STORE_set_default_paths
     * and X509_STORE_load_locations) may push errors to the queue when they fail to
     * find certificate files at various paths. On Windows, none of the Unix-style paths
     * exist, so errors accumulate. If we don't clear them, subsequent SSL operations
     * (like SSL_read) will report these stale "BIO routines::no such file" errors
     * instead of actual errors. */
    ERR_clear_error();

    certificates = sk_X509_new_null();
    for (unsigned int i = 0; i < carray_count(cCerts); i++)
    {
        MMAPString *str;
        str = (MMAPString *)carray_get(cCerts, i);
        if (str == NULL)
        {
            MCLog("Could not read carray_get cert");
            goto free_certs;
        }
        BIO *bio = BIO_new_mem_buf((void *)str->str, str->len);
        X509 *certificate = d2i_X509_bio(bio, NULL);
        BIO_free(bio);
        if (certificate == NULL) {
            goto free_certs;
        }
        if (!sk_X509_push(certificates, certificate))
        {
            MCLog("Could not append certificate via sk_X509_push");
            goto free_certs;
        }
    }

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

    /* X509_verify_cert checks only the chain unless it is given a name to match against the
     * leaf's subjectAltNames, which SecPolicyCreateSSL and the CryptoAPI SSL policy always do.
     * An IP literal has to match an IP SAN, so it is set as an address rather than a name. */
    if (hostname == NULL || hostname->length() == 0)
    {
        MCLog("warning: No hostname to verify the certificate against");
        goto free_certs;
    }
    {
        X509_VERIFY_PARAM *param = X509_STORE_CTX_get0_param(storectx);
        const char *host = hostname->UTF8Characters();
        X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (X509_VERIFY_PARAM_set1_ip_asc(param, host) != 1 &&
            X509_VERIFY_PARAM_set1_host(param, host, 0) != 1)
        {
            MCLog("Could not set the hostname to verify the certificate against");
            goto free_certs;
        }
    }

    status = X509_verify_cert(storectx);
    if (status == 1)
    {
        result = true;
    }
    else
    {
        int errcode = X509_STORE_CTX_get_error(storectx);
        /* The error string is not a literal, so it has to be an argument rather
         * than the format itself - MCLog is a printf and would interpret a "%"
         * in it as a conversion. */
        MCLog("Verification failed: %s (error %d at depth %d)",
              X509_verify_cert_error_string(errcode),
              errcode,
              X509_STORE_CTX_get_error_depth(storectx));

        /*  get the offending certificate causing the failure */
        X509 *error_cert = X509_STORE_CTX_get_current_cert(storectx);
        if (error_cert != NULL) {
            /* X509_NAME_oneline() into a local buffer rather than
             * X509_NAME_print_ex() into BIO_new_fp(stderr): the BIO wrote past
             * MCLog, so the name of the certificate that actually failed never
             * reached the connection log the client shows the user, and never
             * reached the log file during a sync. It truncates safely and
             * NUL-terminates when a name is longer than the buffer, and answers
             * "NO X509_NAME" rather than NULL if the name is missing. */
            char name[512];
            MCLog("Offending certificate subject: %s",
                  X509_NAME_oneline(X509_get_subject_name(error_cert), name, sizeof(name)));
            MCLog("Offending certificate issuer: %s",
                  X509_NAME_oneline(X509_get_issuer_name(error_cert), name, sizeof(name)));
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
