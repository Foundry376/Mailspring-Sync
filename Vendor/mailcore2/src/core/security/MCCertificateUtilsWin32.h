//
//  MCCertificateUtilsWin32.h
//  mailcore2
//
//  Server certificate verification with the Windows chain engine. Header-only and free of
//  mailcore types so test/windows-certificates can compile exactly this code standalone.
//

#ifndef MAILCORE_MCCERTIFICATEUTILSWIN32_H
#define MAILCORE_MCCERTIFICATEUTILSWIN32_H

#ifdef _WIN32

// Exposes CERT_CHAIN_PARA.dwUrlRetrievalTimeout. It must precede the first <wincrypt.h> in the
// translation unit, so MCCertificateUtils.cpp also defines it before its other includes.
#ifndef CERT_CHAIN_PARA_HAS_EXTRA_FIELDS
#define CERT_CHAIN_PARA_HAS_EXTRA_FIELDS
#endif
#include <windows.h>
#include <wincrypt.h>
#include <cstdio>
#include <string>
#include <vector>

#ifndef SECURITY_FLAG_IGNORE_REVOCATION
#define SECURITY_FLAG_IGNORE_REVOCATION 0x00000080 // <wininet.h>
#endif

namespace mailcore {

    enum Win32RevocationMode {
        Win32RevocationNone,     // diagnostics only
        Win32RevocationSoftFail, // what mailsync uses
        Win32RevocationHardFail, // diagnostics only
    };

    struct Win32ChainVerification {
        bool trusted = false;
        bool apiFailure = false;
        DWORD policyError = 0;
        DWORD chainErrorStatus = 0;
        std::vector<std::string> elements; // "subject (error 0x..)" for each certificate in the chain
    };

    /*
     Copying the ROOT store into OpenSSL misses everything Windows uses to distrust a CA: the
     Disallowed store, the auto-updated disallowedcert.stl, and revocation. Microsoft blocked
     the compromised eMudhra intermediates (https://emudhra.we-have-a-problem.com/) through
     disallowedcert.stl and the CAs revoked them by CRL, so only the chain engine rejects them.
     It also sees enterprise and group policy roots and downloads Microsoft program roots that
     aren't cached yet. The SSL policy checks the hostname, as SecPolicyCreateSSL does on macOS.

     Revocation is soft-fail: when no CRL or OCSP answer is available (offline, captive portal,
     a private CA or TLS-inspecting antivirus without CRL URLs) the chain is still accepted,
     but a chain that is revoked or explicitly distrusted is always rejected.
    */
    static inline Win32ChainVerification verifyServerChainWithCryptoAPI(const std::vector<std::string> & derChain,
                                                                        const std::wstring & hostname,
                                                                        Win32RevocationMode revocation = Win32RevocationSoftFail)
    {
        Win32ChainVerification result;
        HCERTSTORE peerStore = NULL;
        PCCERT_CONTEXT leaf = NULL;
        PCCERT_CHAIN_CONTEXT chain = NULL;

        if (derChain.empty() || hostname.empty()) {
            result.apiFailure = true;
            return result;
        }

        // The certificates the server sent are only candidates for building the path. Trust
        // still has to come from the system stores.
        peerStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
        if (peerStore == NULL) {
            result.apiFailure = true;
            return result;
        }
        for (size_t i = 0; i < derChain.size(); i++) {
            BOOL added = CertAddEncodedCertificateToStore(peerStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                          (const BYTE *)derChain[i].data(), (DWORD)derChain[i].size(),
                                                          CERT_STORE_ADD_ALWAYS, i == 0 ? &leaf : NULL);
            if (!added && i == 0) {
                result.apiFailure = true;
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

            DWORD flags = 0;
            if (revocation != Win32RevocationNone) {
                flags = CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | CERT_CHAIN_REVOCATION_ACCUMULATIVE_TIMEOUT;
            }
            if (!CertGetCertificateChain(NULL, leaf, NULL, peerStore, &chainPara, flags, NULL, &chain)) {
                result.apiFailure = true;
                goto done;
            }
        }

        result.chainErrorStatus = chain->TrustStatus.dwErrorStatus;
        if (chain->cChain > 0) {
            PCERT_SIMPLE_CHAIN simple = chain->rgpChain[0];
            for (DWORD i = 0; i < simple->cElement; i++) {
                char name[512] = {0};
                char element[600];
                CertGetNameStringA(simple->rgpElement[i]->pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, name, sizeof(name));
                snprintf(element, sizeof(element), "%s (error 0x%08lx)", name,
                         (unsigned long)simple->rgpElement[i]->TrustStatus.dwErrorStatus);
                result.elements.push_back(element);
            }
        }

        {
            SSL_EXTRA_CERT_CHAIN_POLICY_PARA sslPara;
            ZeroMemory(&sslPara, sizeof(sslPara));
            sslPara.cbSize = sizeof(sslPara);
            sslPara.dwAuthType = AUTHTYPE_SERVER;
            sslPara.pwszServerName = (WCHAR *)hostname.c_str();

            CERT_CHAIN_POLICY_PARA policyPara;
            ZeroMemory(&policyPara, sizeof(policyPara));
            policyPara.cbSize = sizeof(policyPara);
            policyPara.pvExtraPolicyPara = &sslPara;
            if (revocation != Win32RevocationHardFail) {
                sslPara.fdwChecks = SECURITY_FLAG_IGNORE_REVOCATION;
                policyPara.dwFlags = CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS;
            }

            CERT_CHAIN_POLICY_STATUS policyStatus;
            ZeroMemory(&policyStatus, sizeof(policyStatus));
            policyStatus.cbSize = sizeof(policyStatus);
            if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policyPara, &policyStatus)) {
                result.apiFailure = true;
                goto done;
            }
            result.policyError = policyStatus.dwError;
        }

        result.trusted = result.policyError == 0 &&
            (result.chainErrorStatus & (CERT_TRUST_IS_REVOKED | CERT_TRUST_IS_EXPLICIT_DISTRUST)) == 0;

    done:
        if (chain != NULL) CertFreeCertificateChain(chain);
        if (leaf != NULL) CertFreeCertificateContext(leaf);
        CertCloseStore(peerStore, 0);
        return result;
    }

}

#endif

#endif
