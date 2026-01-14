//
//  MailspringDynamicTidy.cpp
//  Mailspring-Sync
//
//  Dynamic loading wrapper for libtidy on Linux.
//  This allows the same binary to work across different Linux distributions
//  that use different sonames for libtidy (Debian: libtidy.so.5deb1,
//  Fedora: libtidy.so.5, etc.)
//

#include "MailspringDynamicTidy.h"

#if defined(__linux__)

#include <dlfcn.h>
#include <cstdio>

// Function pointer types for libtidy functions
typedef void* (*tidyCreate_t)(void);
typedef void (*tidyRelease_t)(void*);
typedef void (*tidyBufInit_t)(MSTidyBuffer*);
typedef void (*tidyBufFree_t)(MSTidyBuffer*);
typedef void (*tidyBufAppend_t)(MSTidyBuffer*, void*, unsigned int);
typedef MSTidyBool (*tidyOptSetBool_t)(void*, unsigned int, MSTidyBool);
typedef MSTidyBool (*tidyOptSetInt_t)(void*, unsigned int, unsigned long);
typedef int (*tidySetCharEncoding_t)(void*, const char*);
typedef int (*tidySetErrorBuffer_t)(void*, MSTidyBuffer*);
typedef int (*tidyParseBuffer_t)(void*, MSTidyBuffer*);
typedef int (*tidyCleanAndRepair_t)(void*);
typedef int (*tidySaveBuffer_t)(void*, MSTidyBuffer*);

// Global state
static void* s_tidyLib = nullptr;
static char s_tidyError[512] = {0};
static tidyCreate_t s_tidyCreate = nullptr;
static tidyRelease_t s_tidyRelease = nullptr;
static tidyBufInit_t s_tidyBufInit = nullptr;
static tidyBufFree_t s_tidyBufFree = nullptr;
static tidyBufAppend_t s_tidyBufAppend = nullptr;
static tidyOptSetBool_t s_tidyOptSetBool = nullptr;
static tidyOptSetInt_t s_tidyOptSetInt = nullptr;
static tidySetCharEncoding_t s_tidySetCharEncoding = nullptr;
static tidySetErrorBuffer_t s_tidySetErrorBuffer = nullptr;
static tidyParseBuffer_t s_tidyParseBuffer = nullptr;
static tidyCleanAndRepair_t s_tidyCleanAndRepair = nullptr;
static tidySaveBuffer_t s_tidySaveBuffer = nullptr;

void mailspring_tidy_init(void) {
    if (s_tidyLib != nullptr) {
        return; // Already initialized
    }

    // Try different library names used by various Linux distributions
    const char* libraryNames[] = {
        "libtidy.so.5",      // Fedora, Arch, generic
        "libtidy.so.58",     // Some newer versions
        "libtidy.so.5deb1",  // Debian/Ubuntu older
        "libtidy.so.60",     // Ubuntu 24.04 (libtidy 5.8.0 uses soname 60)
        "libtidy.so.6",      // Future versions
        "libtidy.so",        // Fallback (development symlink)
        nullptr
    };

    // Clear any previous error
    dlerror();

    char lastError[256] = {0};
    for (int i = 0; libraryNames[i] != nullptr; i++) {
        s_tidyLib = dlopen(libraryNames[i], RTLD_NOW | RTLD_LOCAL);
        if (s_tidyLib != nullptr) {
            break;
        }
        // Store the last dlerror for diagnostics
        const char* err = dlerror();
        if (err != nullptr) {
            snprintf(lastError, sizeof(lastError), "%s", err);
        }
    }

    if (s_tidyLib == nullptr) {
        snprintf(s_tidyError, sizeof(s_tidyError),
            "Could not load libtidy. Tried: libtidy.so.5, libtidy.so.58, libtidy.so.5deb1, "
            "libtidy.so.60, libtidy.so.6, libtidy.so. Last error: %s", lastError);
        fprintf(stderr, "Error: %s\n", s_tidyError);
        fprintf(stderr, "Install libtidy on your system (e.g., 'apt install libtidy-dev' or 'dnf install libtidy-devel')\n");
        return;
    }

    // Load all required function pointers
    s_tidyCreate = (tidyCreate_t)dlsym(s_tidyLib, "tidyCreate");
    s_tidyRelease = (tidyRelease_t)dlsym(s_tidyLib, "tidyRelease");
    s_tidyBufInit = (tidyBufInit_t)dlsym(s_tidyLib, "tidyBufInit");
    s_tidyBufFree = (tidyBufFree_t)dlsym(s_tidyLib, "tidyBufFree");
    s_tidyBufAppend = (tidyBufAppend_t)dlsym(s_tidyLib, "tidyBufAppend");
    s_tidyOptSetBool = (tidyOptSetBool_t)dlsym(s_tidyLib, "tidyOptSetBool");
    s_tidyOptSetInt = (tidyOptSetInt_t)dlsym(s_tidyLib, "tidyOptSetInt");
    s_tidySetCharEncoding = (tidySetCharEncoding_t)dlsym(s_tidyLib, "tidySetCharEncoding");
    s_tidySetErrorBuffer = (tidySetErrorBuffer_t)dlsym(s_tidyLib, "tidySetErrorBuffer");
    s_tidyParseBuffer = (tidyParseBuffer_t)dlsym(s_tidyLib, "tidyParseBuffer");
    s_tidyCleanAndRepair = (tidyCleanAndRepair_t)dlsym(s_tidyLib, "tidyCleanAndRepair");
    s_tidySaveBuffer = (tidySaveBuffer_t)dlsym(s_tidyLib, "tidySaveBuffer");

    // Verify all functions were loaded
    if (!s_tidyCreate || !s_tidyRelease || !s_tidyBufInit || !s_tidyBufFree ||
        !s_tidyBufAppend || !s_tidyOptSetBool || !s_tidyOptSetInt ||
        !s_tidySetCharEncoding || !s_tidySetErrorBuffer || !s_tidyParseBuffer ||
        !s_tidyCleanAndRepair || !s_tidySaveBuffer) {
        snprintf(s_tidyError, sizeof(s_tidyError),
            "libtidy loaded but missing required functions (incompatible version?)");
        fprintf(stderr, "Error: %s\n", s_tidyError);
        dlclose(s_tidyLib);
        s_tidyLib = nullptr;
        return;
    }

    // Success - clear any error
    s_tidyError[0] = '\0';
}

// GCC/Clang constructor attribute: runs automatically at program startup
__attribute__((constructor))
static void initTidyLibrary() {
    mailspring_tidy_init();
}

int mailspring_tidy_available(void) {
    return s_tidyLib != nullptr;
}

const char* mailspring_tidy_error(void) {
    if (s_tidyLib != nullptr) {
        return nullptr; // No error, tidy is available
    }
    return s_tidyError[0] != '\0' ? s_tidyError : "libtidy not initialized";
}

// Wrapper implementations
MSTidyDoc mailspring_tidyCreate(void) {
    return s_tidyCreate ? s_tidyCreate() : nullptr;
}

void mailspring_tidyRelease(MSTidyDoc tdoc) {
    if (s_tidyRelease) s_tidyRelease(tdoc);
}

void mailspring_tidyBufInit(MSTidyBuffer* buf) {
    if (s_tidyBufInit) s_tidyBufInit(buf);
}

void mailspring_tidyBufFree(MSTidyBuffer* buf) {
    if (s_tidyBufFree) s_tidyBufFree(buf);
}

void mailspring_tidyBufAppend(MSTidyBuffer* buf, void* data, unsigned int size) {
    if (s_tidyBufAppend) s_tidyBufAppend(buf, data, size);
}

MSTidyBool mailspring_tidyOptSetBool(MSTidyDoc tdoc, MSTidyOptionId optId, MSTidyBool val) {
    return s_tidyOptSetBool ? s_tidyOptSetBool(tdoc, optId, val) : MSTidyNo;
}

MSTidyBool mailspring_tidyOptSetInt(MSTidyDoc tdoc, MSTidyOptionId optId, unsigned long val) {
    return s_tidyOptSetInt ? s_tidyOptSetInt(tdoc, optId, val) : MSTidyNo;
}

int mailspring_tidySetCharEncoding(MSTidyDoc tdoc, const char* encnam) {
    return s_tidySetCharEncoding ? s_tidySetCharEncoding(tdoc, encnam) : -1;
}

int mailspring_tidySetErrorBuffer(MSTidyDoc tdoc, MSTidyBuffer* errbuf) {
    return s_tidySetErrorBuffer ? s_tidySetErrorBuffer(tdoc, errbuf) : -1;
}

int mailspring_tidyParseBuffer(MSTidyDoc tdoc, MSTidyBuffer* buf) {
    return s_tidyParseBuffer ? s_tidyParseBuffer(tdoc, buf) : -1;
}

int mailspring_tidyCleanAndRepair(MSTidyDoc tdoc) {
    return s_tidyCleanAndRepair ? s_tidyCleanAndRepair(tdoc) : -1;
}

int mailspring_tidySaveBuffer(MSTidyDoc tdoc, MSTidyBuffer* buf) {
    return s_tidySaveBuffer ? s_tidySaveBuffer(tdoc, buf) : -1;
}

#endif // __linux__
