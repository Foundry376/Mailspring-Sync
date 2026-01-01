//
//  MailspringDynamicTidy.h
//  Mailspring-Sync
//
//  Dynamic loading wrapper for libtidy on Linux.
//  This allows the same binary to work across different Linux distributions
//  that use different sonames for libtidy (Debian: libtidy.so.5deb1,
//  Fedora: libtidy.so.5, etc.)
//

#ifndef MAILSPRING_DYNAMIC_TIDY_H
#define MAILSPRING_DYNAMIC_TIDY_H

#if defined(__linux__)

#ifdef __cplusplus
extern "C" {
#endif

// Opaque tidy types - we only need pointers
typedef void* MSTidyDoc;
typedef struct _MSTidyBuffer {
    void* allocator;
    unsigned char* bp;
    unsigned int size;
    unsigned int allocated;
    unsigned int next;
} MSTidyBuffer;

// Boolean type matching libtidy
typedef enum { MSTidyNo = 0, MSTidyYes = 1 } MSTidyBool;

// Option IDs we use (from tidyenum.h)
typedef enum {
    MSTidyXhtmlOut = 115,
    MSTidyDoctypeMode = 92,
    MSTidyMark = 153,
    MSTidyForceOutput = 162,
    MSTidyShowWarnings = 106,
    MSTidyShowErrors = 163
} MSTidyOptionId;

// Doctype mode value passed to MSTidyDoctypeMode option
#define MSTidyDoctypeUser 249

// Initialize libtidy dynamic loading - called automatically at startup
void mailspring_tidy_init(void);

// Check if libtidy is available
int mailspring_tidy_available(void);

// Wrapper functions matching libtidy API
MSTidyDoc mailspring_tidyCreate(void);
void mailspring_tidyRelease(MSTidyDoc tdoc);
void mailspring_tidyBufInit(MSTidyBuffer* buf);
void mailspring_tidyBufFree(MSTidyBuffer* buf);
void mailspring_tidyBufAppend(MSTidyBuffer* buf, void* data, unsigned int size);
MSTidyBool mailspring_tidyOptSetBool(MSTidyDoc tdoc, MSTidyOptionId optId, MSTidyBool val);
MSTidyBool mailspring_tidyOptSetInt(MSTidyDoc tdoc, MSTidyOptionId optId, unsigned long val);
int mailspring_tidySetCharEncoding(MSTidyDoc tdoc, const char* encnam);
int mailspring_tidySetErrorBuffer(MSTidyDoc tdoc, MSTidyBuffer* errbuf);
int mailspring_tidyParseBuffer(MSTidyDoc tdoc, MSTidyBuffer* buf);
int mailspring_tidyCleanAndRepair(MSTidyDoc tdoc);
int mailspring_tidySaveBuffer(MSTidyDoc tdoc, MSTidyBuffer* buf);

#ifdef __cplusplus
}
#endif

#endif // __linux__

#endif // MAILSPRING_DYNAMIC_TIDY_H
