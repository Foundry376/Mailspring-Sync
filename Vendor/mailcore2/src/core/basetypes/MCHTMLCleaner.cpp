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

// On Linux, use Mailspring's dynamic tidy loader to support different
// libtidy sonames across distributions. On other platforms, link directly.
#if defined(__linux__)
#include "MailspringDynamicTidy.h"
#else
#include <tidy.h>
// In tidy-html5 5.8.0+, buffio.h was renamed to tidybuffio.h
#if defined(_WIN32)
#include <tidybuffio.h>
#else
#include <buffio.h>
#endif
#endif

#include "MCUtils.h"
#include "MCLog.h"

#if __APPLE__
#include <TargetConditionals.h>
#endif

using namespace mailcore;

String * HTMLCleaner::cleanHTML(String * input)
{
#if defined(__linux__)
    // Linux: use Mailspring's dynamic tidy wrapper
    // SECURITY: We must not return unsanitized HTML. If tidy fails, return empty string.
    if (!mailspring_tidy_available()) {
        const char* err = mailspring_tidy_error();
        MCLog("HTMLCleaner: libtidy not available - %s", err ? err : "unknown error");
        return String::string();
    }

    MSTidyBuffer output = {0};
    MSTidyBuffer errbuf = {0};
    MSTidyBuffer docbuf = {0};

    MSTidyDoc tdoc = mailspring_tidyCreate();
    if (tdoc == NULL) {
        MCLog("HTMLCleaner: tidyCreate failed (out of memory?)");
        return String::string();
    }

    mailspring_tidyBufInit(&output);
    mailspring_tidyBufInit(&errbuf);
    mailspring_tidyBufInit(&docbuf);

    Data * data = input->dataUsingEncoding("utf-8");
    mailspring_tidyBufAppend(&docbuf, data->bytes(), data->length());

    // Use dynamically resolved option IDs for compatibility across libtidy versions
    mailspring_tidyOptSetBool(tdoc, mailspring_tidyOptId_XhtmlOut(), MSTidyYes);
    mailspring_tidyOptSetInt(tdoc, mailspring_tidyOptId_DoctypeMode(), MSTidyDoctypeUser);
    mailspring_tidyOptSetBool(tdoc, mailspring_tidyOptId_Mark(), MSTidyNo);
    mailspring_tidySetCharEncoding(tdoc, "utf8");
    mailspring_tidyOptSetBool(tdoc, mailspring_tidyOptId_ForceOutput(), MSTidyYes);
    mailspring_tidyOptSetBool(tdoc, mailspring_tidyOptId_ShowWarnings(), MSTidyNo);
    mailspring_tidyOptSetInt(tdoc, mailspring_tidyOptId_ShowErrors(), 0);
    mailspring_tidySetErrorBuffer(tdoc, &errbuf);

    int parseResult = mailspring_tidyParseBuffer(tdoc, &docbuf);
    int cleanResult = mailspring_tidyCleanAndRepair(tdoc);
    int saveResult = mailspring_tidySaveBuffer(tdoc, &output);

    // Check for severe errors (< 0 means errno-style failure)
    if (parseResult < 0 || cleanResult < 0 || saveResult < 0) {
        MCLog("HTMLCleaner: tidy processing failed (parse=%d, clean=%d, save=%d)",
              parseResult, cleanResult, saveResult);
        mailspring_tidyBufFree(&docbuf);
        mailspring_tidyBufFree(&output);
        mailspring_tidyBufFree(&errbuf);
        mailspring_tidyRelease(tdoc);
        return String::string();
    }

    String * result = NULL;
    if (output.bp != NULL) {
        result = String::stringWithUTF8Characters((const char *) output.bp);
    }

    mailspring_tidyBufFree(&docbuf);
    mailspring_tidyBufFree(&output);
    mailspring_tidyBufFree(&errbuf);
    mailspring_tidyRelease(tdoc);

    if (result == NULL) {
        MCLog("HTMLCleaner: tidy produced no output");
        return String::string();
    }

    return result;

#else
    // Non-Linux (Mac/Windows): use direct tidy linking
    // SECURITY: We must not return unsanitized HTML. If tidy fails, return empty string.
    TidyBuffer output = {0};
    TidyBuffer errbuf = {0};
    TidyBuffer docbuf = {0};

    TidyDoc tdoc = tidyCreate();
    if (tdoc == NULL) {
        MCLog("HTMLCleaner: tidyCreate failed (out of memory?)");
        return String::string();
    }

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
    tidyOptSetBool(tdoc, TidyShowWarnings, no);
    tidyOptSetInt(tdoc, TidyShowErrors, 0);
    tidySetErrorBuffer(tdoc, &errbuf);

    int parseResult = tidyParseBuffer(tdoc, &docbuf);
    int cleanResult = tidyCleanAndRepair(tdoc);
    int saveResult = tidySaveBuffer(tdoc, &output);

    // Check for severe errors (< 0 means errno-style failure)
    if (parseResult < 0 || cleanResult < 0 || saveResult < 0) {
        MCLog("HTMLCleaner: tidy processing failed (parse=%d, clean=%d, save=%d)",
              parseResult, cleanResult, saveResult);
        tidyBufFree(&docbuf);
        tidyBufFree(&output);
        tidyBufFree(&errbuf);
        tidyRelease(tdoc);
        return String::string();
    }

    String * result = NULL;
    if (output.bp != NULL) {
        result = String::stringWithUTF8Characters((const char *) output.bp);
    }

    tidyBufFree(&docbuf);
    tidyBufFree(&output);
    tidyBufFree(&errbuf);
    tidyRelease(tdoc);

    if (result == NULL) {
        MCLog("HTMLCleaner: tidy produced no output");
        return String::string();
    }

    return result;
#endif
}
