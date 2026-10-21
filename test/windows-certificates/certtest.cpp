// certtest: runs mailcore's Windows certificate verification (MCCertificateUtilsWin32.h, the
// code mailsync ships) against PEM chains, and inspects the ROOT and Disallowed stores for the
// compromised eMudhra keys (https://emudhra.we-have-a-problem.com/).
//
//   certtest manifest <file> [none|soft|hard]   lines: "<pass|fail> <hostname> <chain.pem>"
//   certtest chain <chain.pem> <hostname> [mode]
//   certtest stores
#include "../../Vendor/mailcore2/src/core/security/MCCertificateUtilsWin32.h"
#include <cstring>

using namespace mailcore;
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::vector<std::string> readPemChain(const std::string & path)
{
    std::vector<std::string> out;
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string all = ss.str();
    const std::string B = "-----BEGIN CERTIFICATE-----", E = "-----END CERTIFICATE-----";
    size_t pos = 0;
    while ((pos = all.find(B, pos)) != std::string::npos) {
        size_t end = all.find(E, pos);
        if (end == std::string::npos) break;
        std::string block = all.substr(pos, end + E.size() - pos);
        DWORD len = 0;
        if (CryptStringToBinaryA(block.c_str(), (DWORD)block.size(), CRYPT_STRING_BASE64HEADER, NULL, &len, NULL, NULL)) {
            std::string der(len, '\0');
            CryptStringToBinaryA(block.c_str(), (DWORD)block.size(), CRYPT_STRING_BASE64HEADER, (BYTE *)&der[0], &len, NULL, NULL);
            der.resize(len);
            out.push_back(der);
        }
        pos = end + E.size();
    }
    return out;
}

static Win32RevocationMode parseMode(const char * s)
{
    if (s && !strcmp(s, "none")) return Win32RevocationNone;
    if (s && !strcmp(s, "hard")) return Win32RevocationHardFail;
    return Win32RevocationSoftFail;
}

static bool runOne(const std::string & expect, const std::string & host, const std::string & pem, Win32RevocationMode mode)
{
    std::vector<std::string> chain = readPemChain(pem);
    std::wstring whost(host.begin(), host.end());
    DWORD t0 = GetTickCount();
    Win32ChainVerification r = verifyServerChainWithCryptoAPI(chain, whost, mode);
    DWORD ms = GetTickCount() - t0;
    bool wantPass = (expect == "pass");
    bool ok = (r.trusted == wantPass) && !r.apiFailure;
    printf("%-10s expect=%-4s got=%-6s policyErr=0x%08lx chainErr=0x%08lx certs=%zu %lums  %s\n",
           ok ? "OK" : "UNEXPECTED", expect.c_str(), r.apiFailure ? "APIERR" : (r.trusted ? "pass" : "fail"),
           (unsigned long)r.policyError, (unsigned long)r.chainErrorStatus, chain.size(), (unsigned long)ms, host.c_str());
    for (auto & e : r.elements) printf("             %s\n", e.c_str());
    return ok;
}

// MD5 of the subject public key bits: the identifier disallowedcert.stl uses for these entries.
static const char * kCompromisedKeyMD5[] = {
    "322B20AB51220AB310C1F6FC4C9542B0", "EF7C45137A6331CAF3E91E23B869C3A4", "16511532F29CEA7D19D159489596F846",
    "C2C31C8403ECD07AF6496BA45CF56D30", "E6A18EF905AAC20D2A22859AC037051E", "9B7EFC3E3EA358D3BDB5C8871A5042F0",
    "4A4104C46363F4F5696B405521BE2372", "655225193DB1CDA8A47A65C92FC1CA90",
};
// SHA-1 thumbprints of roots the compromised intermediates chain to.
static const struct { const char * sha1; const char * name; } kRoots[] = {
    {"39D3236E4A609AB6190CBFA1AD02B3CC61DFC2C3", "emSign Root TLS CA - G3"},
    {"D1EB23A46D17D68FD92564C2F1F1601764D8E349", "AAA Certificate Services"},
    {"3043FA4FF257DCA0C380EE2E58EA78B23FE6BBC1", "emSign ECC Root CA - G3"},
};

static std::string hex(const BYTE * b, DWORD n)
{
    std::string s;
    char t[3];
    for (DWORD i = 0; i < n; i++) {
        snprintf(t, sizeof(t), "%02X", b[i]);
        s += t;
    }
    return s;
}

static std::vector<BYTE> unhex(const char * h)
{
    std::vector<BYTE> v;
    for (size_t i = 0; h[i] && h[i + 1]; i += 2) {
        char t[3] = {h[i], h[i + 1], 0};
        v.push_back((BYTE)strtoul(t, NULL, 16));
    }
    return v;
}

static void scanStores()
{
    HCERTSTORE root = CertOpenSystemStoreW(0, L"ROOT");
    int n = 0;
    PCCERT_CONTEXT c = NULL;
    while ((c = CertEnumCertificatesInStore(root, c)) != NULL) {
        n++;
        BYTE h[20];
        DWORD hl = sizeof(h);
        if (CertGetCertificateContextProperty(c, CERT_SHA1_HASH_PROP_ID, h, &hl)) {
            std::string hs = hex(h, hl);
            for (auto & r : kRoots)
                if (hs == r.sha1) printf("ROOT store contains: %s\n", r.name);
        }
    }
    printf("ROOT store (what mailsync currently copies into OpenSSL): %d certs\n", n);
    CertCloseStore(root, 0);

    HCERTSTORE dis = CertOpenSystemStoreW(0, L"Disallowed");
    n = 0;
    int hits = 0;
    c = NULL;
    while ((c = CertEnumCertificatesInStore(dis, c)) != NULL) {
        n++;
        BYTE h[16];
        DWORD hl = sizeof(h);
        if (CertGetCertificateContextProperty(c, CERT_SUBJECT_PUBLIC_KEY_MD5_HASH_PROP_ID, h, &hl)) {
            std::string hs = hex(h, hl);
            for (auto k : kCompromisedKeyMD5)
                if (hs == k) hits++;
        }
    }
    int ctls = 0;
    PCCTL_CONTEXT ctl = NULL;
    while ((ctl = CertEnumCTLsInStore(dis, ctl)) != NULL) ctls++;
    printf("Disallowed store: %d certificates enumerable, %d of them eMudhra-compromised keys; %d CTLs enumerable\n", n, hits, ctls);
    CertCloseStore(dis, 0);

    // The auto-updated disallowedcert.stl lives here, not as certificates in the Disallowed store.
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\SystemCertificates\\AuthRoot\\AutoUpdate", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type = 0, size = 0;
        if (RegQueryValueExW(key, L"DisallowedCertEncodedCtl", NULL, &type, NULL, &size) == ERROR_SUCCESS) {
            std::vector<BYTE> blob(size);
            RegQueryValueExW(key, L"DisallowedCertEncodedCtl", NULL, &type, blob.data(), &size);
            int found = 0;
            for (auto k : kCompromisedKeyMD5) {
                std::vector<BYTE> needle = unhex(k);
                for (size_t i = 0; i + needle.size() <= blob.size(); i++)
                    if (!memcmp(&blob[i], needle.data(), needle.size())) {
                        found++;
                        break;
                    }
            }
            printf("AutoUpdate\\DisallowedCertEncodedCtl: %lu bytes, contains %d/8 eMudhra key hashes\n", (unsigned long)size, found);
        } else {
            printf("AutoUpdate\\DisallowedCertEncodedCtl: absent\n");
        }
        FILETIME ft;
        size = sizeof(ft);
        if (RegQueryValueExW(key, L"DisallowedCertLastSyncTime", NULL, &type, (BYTE *)&ft, &size) == ERROR_SUCCESS) {
            SYSTEMTIME st;
            FileTimeToSystemTime(&ft, &st);
            printf("DisallowedCertLastSyncTime: %04d-%02d-%02d %02d:%02d UTC\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        }
        RegCloseKey(key);
    } else {
        printf("AutoUpdate registry key absent\n");
    }
}

int main(int argc, char ** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc >= 2 && !strcmp(argv[1], "stores")) {
        scanStores();
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "chain")) {
        return runOne("pass", argv[3], argv[2], parseMode(argc > 4 ? argv[4] : NULL)) ? 0 : 1;
    }
    if (argc >= 3 && !strcmp(argv[1], "manifest")) {
        Win32RevocationMode mode = parseMode(argc > 3 ? argv[3] : NULL);
        std::ifstream f(argv[2]);
        std::string line;
        int bad = 0, total = 0;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string expect, host, pem;
            ls >> expect >> host >> pem;
            total++;
            if (!runOne(expect, host, pem, mode)) bad++;
        }
        printf("%d/%d cases as expected\n", total - bad, total);
        return bad ? 1 : 0;
    }
    fprintf(stderr, "usage: certtest manifest <file> [none|soft|hard] | chain <pem> <host> [mode] | stores\n");
    return 2;
}
