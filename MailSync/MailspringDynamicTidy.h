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

// Doctype mode value (TidyDoctypeUser from TidyDoctypeModes enum)
// In libtidy 5.x: Html5=0, Omit=1, Auto=2, Strict=3, Loose=4, User=5
#define MSTidyDoctypeUser 5

// Initialize libtidy dynamic loading - called automatically at startup
void mailspring_tidy_init(void);

// Check if libtidy is available
int mailspring_tidy_available(void);

// Get error message if tidy loading failed (returns NULL if available)
const char* mailspring_tidy_error(void);

// Wrapper functions matching libtidy API
MSTidyDoc mailspring_tidyCreate(void);
void mailspring_tidyRelease(MSTidyDoc tdoc);
void mailspring_tidyBufInit(MSTidyBuffer* buf);
void mailspring_tidyBufFree(MSTidyBuffer* buf);
void mailspring_tidyBufAppend(MSTidyBuffer* buf, void* data, unsigned int size);
MSTidyBool mailspring_tidyOptSetBool(MSTidyDoc tdoc, unsigned int optId, MSTidyBool val);
MSTidyBool mailspring_tidyOptSetInt(MSTidyDoc tdoc, unsigned int optId, unsigned long val);
int mailspring_tidySetCharEncoding(MSTidyDoc tdoc, const char* encnam);
int mailspring_tidySetErrorBuffer(MSTidyDoc tdoc, MSTidyBuffer* errbuf);
int mailspring_tidyParseBuffer(MSTidyDoc tdoc, MSTidyBuffer* buf);
int mailspring_tidyCleanAndRepair(MSTidyDoc tdoc);
int mailspring_tidySaveBuffer(MSTidyDoc tdoc, MSTidyBuffer* buf);

// Getters for dynamically resolved option IDs (looked up by name at init)
unsigned int mailspring_tidyOptId_XhtmlOut(void);
unsigned int mailspring_tidyOptId_DoctypeMode(void);
unsigned int mailspring_tidyOptId_Mark(void);
unsigned int mailspring_tidyOptId_ForceOutput(void);
unsigned int mailspring_tidyOptId_ShowWarnings(void);
unsigned int mailspring_tidyOptId_ShowErrors(void);

#ifdef __cplusplus
}
#endif

#endif // __linux__

#endif // MAILSPRING_DYNAMIC_TIDY_H
