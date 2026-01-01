//
//  HTMLCleaner.cpp
//  mailcore2
//
//  Created by DINH Viêt Hoà on 2/3/13.
//  Copyright (c) 2013 MailCore. All rights reserved.
//

#include "MCHTMLCleaner.h"

#include "MCString.h"
#include "MCData.h"

#if defined(ANDROID) || defined(__ANDROID__)
typedef unsigned long ulong;
#endif

// On Linux, we use runtime dynamic loading to support different libtidy
// sonames across distributions (Debian uses libtidy.so.5deb1, Fedora uses
// libtidy.so.5 or libtidy.so.58, etc.)
#if defined(__linux__)
#define USE_DYNAMIC_TIDY_LOADING 1
#include <dlfcn.h>
#include <cstdio>

// Forward declarations for tidy types - we only need opaque pointers
typedef void* TidyDoc;
typedef struct _TidyBuffer {
    void* allocator;
    unsigned char* bp;
    unsigned int size;
    unsigned int allocated;
    unsigned int next;
} TidyBuffer;

// Tidy option and boolean types
typedef unsigned int TidyOptionId;
typedef enum { no = 0, yes = 1 } Bool;

// Tidy option IDs we use (from tidyenum.h)
#define TidyXhtmlOut 115
#define TidyDoctypeMode 92
#define TidyDoctypeUser 249
#define TidyMark 153
#define TidyForceOutput 162
#define TidyShowWarnings 106
#define TidyShowErrors 163

// Function pointer types for libtidy functions
typedef TidyDoc (*tidyCreate_t)(void);
typedef void (*tidyRelease_t)(TidyDoc);
typedef void (*tidyBufInit_t)(TidyBuffer*);
typedef void (*tidyBufFree_t)(TidyBuffer*);
typedef void (*tidyBufAppend_t)(TidyBuffer*, void*, unsigned int);
typedef Bool (*tidyOptSetBool_t)(TidyDoc, TidyOptionId, Bool);
typedef Bool (*tidyOptSetInt_t)(TidyDoc, TidyOptionId, unsigned long);
typedef int (*tidySetCharEncoding_t)(TidyDoc, const char*);
typedef int (*tidySetErrorBuffer_t)(TidyDoc, TidyBuffer*);
typedef int (*tidyParseBuffer_t)(TidyDoc, TidyBuffer*);
typedef int (*tidyCleanAndRepair_t)(TidyDoc);
typedef int (*tidySaveBuffer_t)(TidyDoc, TidyBuffer*);

// Global function pointers - loaded once at startup
static void* s_tidyLib = nullptr;
static bool s_tidyLoadAttempted = false;
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

static bool loadTidyLibrary() {
    if (s_tidyLoadAttempted) {
        return s_tidyLib != nullptr;
    }
    s_tidyLoadAttempted = true;

    // Try different library names used by various Linux distributions
    const char* libraryNames[] = {
        "libtidy.so.5",      // Fedora, Arch, generic
        "libtidy.so.58",     // Some newer versions
        "libtidy.so.5deb1",  // Debian/Ubuntu
        "libtidy.so.6",      // Future versions
        "libtidy.so",        // Fallback (development symlink)
        nullptr
    };

    for (int i = 0; libraryNames[i] != nullptr; i++) {
        s_tidyLib = dlopen(libraryNames[i], RTLD_NOW | RTLD_LOCAL);
        if (s_tidyLib != nullptr) {
            break;
        }
    }

    if (s_tidyLib == nullptr) {
        fprintf(stderr, "Warning: Could not load libtidy. HTML cleaning will be unavailable.\n");
        fprintf(stderr, "Install libtidy on your system (e.g., 'apt install libtidy-dev' or 'dnf install libtidy-devel')\n");
        return false;
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
        fprintf(stderr, "Warning: libtidy loaded but missing required functions.\n");
        dlclose(s_tidyLib);
        s_tidyLib = nullptr;
        return false;
    }

    return true;
}

#else
// Non-Linux platforms: use direct linking
#include <tidy.h>
#include <buffio.h>
#endif

#include "MCUtils.h"
#include "MCLog.h"

#if __APPLE__
#include <TargetConditionals.h>
#endif

using namespace mailcore;

String * HTMLCleaner::cleanHTML(String * input)
{
#if defined(USE_DYNAMIC_TIDY_LOADING)
    // Linux: use dynamically loaded functions
    if (!loadTidyLibrary()) {
        // If tidy isn't available, return input unchanged
        return input;
    }

    TidyBuffer output;
    TidyBuffer errbuf;
    TidyBuffer docbuf;
    int rc;

    TidyDoc tdoc = s_tidyCreate();
    s_tidyBufInit(&output);
    s_tidyBufInit(&errbuf);
    s_tidyBufInit(&docbuf);

    Data * data = input->dataUsingEncoding("utf-8");
    s_tidyBufAppend(&docbuf, data->bytes(), data->length());

    s_tidyOptSetBool(tdoc, TidyXhtmlOut, yes);
    s_tidyOptSetInt(tdoc, TidyDoctypeMode, TidyDoctypeUser);

    s_tidyOptSetBool(tdoc, TidyMark, no);
    s_tidySetCharEncoding(tdoc, "utf8");
    s_tidyOptSetBool(tdoc, TidyForceOutput, yes);
    s_tidyOptSetBool(tdoc, TidyShowWarnings, no);
    s_tidyOptSetInt(tdoc, TidyShowErrors, 0);
    rc = s_tidySetErrorBuffer(tdoc, &errbuf);
    rc = s_tidyParseBuffer(tdoc, &docbuf);
    rc = s_tidyCleanAndRepair(tdoc);
    rc = s_tidySaveBuffer(tdoc, &output);

    String * result = String::stringWithUTF8Characters((const char *) output.bp);

    s_tidyBufFree(&docbuf);
    s_tidyBufFree(&output);
    s_tidyBufFree(&errbuf);
    s_tidyRelease(tdoc);

    return result;

#else
    // Non-Linux: use direct linking (macOS, Windows, etc.)
    TidyBuffer output;
    TidyBuffer errbuf;
    TidyBuffer docbuf;
    int rc;

    TidyDoc tdoc = tidyCreate();
    tidyBufInit(&output);
    tidyBufInit(&errbuf);
    tidyBufInit(&docbuf);

    Data * data = input->dataUsingEncoding("utf-8");
    tidyBufAppend(&docbuf, data->bytes(), data->length());

#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
    // This option is not available on the Mac.
    tidyOptSetBool(tdoc, TidyDropEmptyElems, no);
#endif
    tidyOptSetBool(tdoc, TidyXhtmlOut, yes);
    tidyOptSetInt(tdoc, TidyDoctypeMode, TidyDoctypeUser);

    tidyOptSetBool(tdoc, TidyMark, no);
    tidySetCharEncoding(tdoc, "utf8");
    tidyOptSetBool(tdoc, TidyForceOutput, yes);
    //tidyOptSetValue(tdoc, TidyErrFile, "/dev/null");
    //tidyOptSetValue(tdoc, TidyOutFile, "/dev/null");
    tidyOptSetBool(tdoc, TidyShowWarnings, no);
    tidyOptSetInt(tdoc, TidyShowErrors, 0);
    rc = tidySetErrorBuffer(tdoc, &errbuf);
    if ((rc > 1) || (rc < 0)) {
        //fprintf(stderr, "error tidySetErrorBuffer: %i\n", rc);
        //fprintf(stderr, "1:%s", errbuf.bp);
        //return NULL;
    }
    rc = tidyParseBuffer(tdoc, &docbuf);
    //MCLog("%s", MCUTF8(input));
    if ((rc > 1) || (rc < 0)) {
        //fprintf(stderr, "error tidyParseBuffer: %i\n", rc);
        //fprintf(stderr, "1:%s", errbuf.bp);
        //return NULL;
    }
    rc = tidyCleanAndRepair(tdoc);
    if ((rc > 1) || (rc < 0)) {
        //fprintf(stderr, "error tidyCleanAndRepair: %i\n", rc);
        //fprintf(stderr, "1:%s", errbuf.bp);
        //return NULL;
    }
    rc = tidySaveBuffer(tdoc, &output);
    if ((rc > 1) || (rc < 0)) {
        //fprintf(stderr, "error tidySaveBuffer: %i\n", rc);
        //fprintf(stderr, "1:%s", errbuf.bp);
    }

    String * result = String::stringWithUTF8Characters((const char *) output.bp);

    tidyBufFree(&docbuf);
    tidyBufFree(&output);
    tidyBufFree(&errbuf);
    tidyRelease(tdoc);

    return result;
#endif
}
