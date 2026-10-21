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
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
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
        bool blockedKey = false;
        DWORD policyError = 0;
        DWORD chainErrorStatus = 0;
        std::vector<std::string> elements; // "subject (error 0x..)" for each certificate in the chain
    };

    /*
     SHA-256 of the SubjectPublicKeyInfo of each compromised eMudhra intermediate, from the
     disclosure's impacted-intermediates.json (https://emudhra.we-have-a-problem.com/). Each key
     is also cross-signed by Sectigo's AAA Certificate Services, so matching the key covers both.
     Windows only rejects these once disallowedcert.stl has synced or a CRL fetch succeeds; CI
     runners on 2026-10-21 had neither list entry, and an attacker who can intercept the mail
     connection can also block the CRL fetch that soft-fail revocation depends on.
    */
    static const char * const compromisedKeySHA256[] = {
        "EBED6475C2C2D80FCB199F3FE8DD6E6B3CE60B77EB15D4E42C3DEC0C6F5B5105", // EM DV TLS CA - G3B
        "43BFFA19D6EF651451F6841E019931E96F6F2EAEFF28AB4B6F1CE4576DE2717B", // EM OV TLS CA - G3B
        "E0254C7FCDC265E2F0F8A428DABB75AB4AE039B4872D62F5EE57F0877D5A8A41", // EM EV TLS CA - G3B
        "49DD353DC2B919A3770645117D0E60C16115E7D1BC259EEFE734B00EA434FE5F", // EM DV TLS CA - G3C
        "63ECEFEE431D49523BC1136C4E72D0206A477086CAF606CD5CF025686C109CF8", // EM OV TLS CA - G3C
        "5C0660A2B164910D8FF589C18E14552E9E0EC237DCEFE2CB736D41A4811F81A0", // EM EV TLS CA - G3C
        "EC4FFA1AD0AC88081314B7AAFFEF6A91F97CD57291BB5BA686C5415F5EB4F6B6", // EM OV CS CA - G3B
        "92C063D67E8F14DB71D288E11266FFDAF69CBF115F7BAB14F3BC2F68D25E0E30", // EM OV CS CA - G3C
    };

    static inline bool hasCompromisedKey(PCCERT_CONTEXT cert)
    {
        BYTE * spki = NULL;
        DWORD spkiLength = 0;
        if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, &cert->pCertInfo->SubjectPublicKeyInfo,
                                 CRYPT_ENCODE_ALLOC_FLAG, NULL, &spki, &spkiLength)) {
            return false;
        }
        BYTE hash[32];
        DWORD hashLength = sizeof(hash);
        BOOL hashed = CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, NULL, spki, spkiLength, hash, &hashLength);
        LocalFree(spki);
        if (!hashed || hashLength != sizeof(hash)) {
            return false;
        }
        char hex[65];
        for (DWORD i = 0; i < hashLength; i++) {
            snprintf(hex + i * 2, 3, "%02X", hash[i]);
        }
        for (const char * blocked : compromisedKeySHA256) {
            if (strcmp(hex, blocked) == 0) {
                return true;
            }
        }
        return false;
    }

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
        for (DWORD c = 0; c < chain->cChain; c++) {
            PCERT_SIMPLE_CHAIN simple = chain->rgpChain[c];
            for (DWORD i = 0; i < simple->cElement; i++) {
                PCCERT_CONTEXT cert = simple->rgpElement[i]->pCertContext;
                bool blocked = hasCompromisedKey(cert);
                result.blockedKey = result.blockedKey || blocked;
                char name[512] = {0};
                char element[600];
                CertGetNameStringA(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, name, sizeof(name));
                snprintf(element, sizeof(element), "%s (error 0x%08lx)%s", name,
                         (unsigned long)simple->rgpElement[i]->TrustStatus.dwErrorStatus,
                         blocked ? " compromised key" : "");
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

        result.trusted = result.policyError == 0 && !result.blockedKey &&
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
