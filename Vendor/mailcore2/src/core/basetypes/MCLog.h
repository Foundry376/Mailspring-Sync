#ifndef MAILCORE_MCLOG_H

#define MAILCORE_MCLOG_H

#include <stdio.h>
#include <MailCore/MCUtils.h>

#define MCLog(...) MCLogFn(NULL, __FILE__, __LINE__, 0, __VA_ARGS__)
#define MCLogStack(...) MCLogFn(NULL, __FILE__, __LINE__, 1, __VA_ARGS__)

MAILCORE_EXPORT
extern int MCLogEnabled;

MAILCORE_EXPORT
extern void (*MCLogFn)(const char *, const char *, unsigned int, int, const char *, ...);

#ifndef __printflike
#define __printflike(a,b)
#endif

#ifdef __cplusplus
extern "C" {
#endif
    MAILCORE_EXPORT
    void MCLogInternal(const char * user,
                       const char * filename,
                       unsigned int line,
                       int dumpStack,
                       const char * format, ...) __printflike(5, 6);

// NOTE: BG EDITS HERE TO REDIRECT MCLOG TO SPDLOG

#ifdef __cplusplus
}
#endif

#endif
