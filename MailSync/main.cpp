//
//  main.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include <atomic>
#include <iostream>
#include <string>
#include <time.h>
#include <signal.h>
#define SPDLOG_WCHAR_FILENAMES true
#define _TIMESPEC_DEFINED true
#include <pthread.h>
#include <sqlite3.h>

#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include <StanfordCPPLib/exceptions.h>
#include <curl/curl.h>
#include "json.hpp"
#include "spdlog/spdlog.h"
#include "optionparser.h"
#include "exceptions.h"

#include "Account.hpp"
#include "Identity.hpp"
#include "MailUtils.hpp"
#include "MailStore.hpp"
#include "DeltaStream.hpp"
#include "SyncWorker.hpp"
#include "MetadataWorker.hpp"
#include "MetadataExpirationWorker.hpp"
#include "DAVWorker.hpp"
#include "GoogleContactsWorker.hpp"
#include "SyncException.hpp"
#include "Task.hpp"
#include "TaskProcessor.hpp"
#include "NetworkRequestUtils.hpp"
#include "ThreadUtils.h"
#include "constants.h"
#include "SPDLogExtensions.hpp"

#if defined(__linux__)
#include "MailspringDynamicTidy.h"
#endif

using namespace nlohmann;
using option::Option;
using option::Descriptor;
using option::Parser;
using option::Stats;
using option::ArgStatus;

shared_ptr<SyncWorker> bgWorker = nullptr;
shared_ptr<SyncWorker> fgWorker = nullptr;
shared_ptr<DAVWorker> davWorker = nullptr;
shared_ptr<GoogleContactsWorker> contactsWorker = nullptr;

shared_ptr<MetadataWorker> metadataWorker = nullptr;
shared_ptr<MetadataExpirationWorker> metadataExpirationWorker = nullptr;

std::atomic<bool> bgWorkerShouldMarkAll{true};

std::thread * fgThread = nullptr;
std::thread * bgThread = nullptr;
std::thread * calContactsThread = nullptr;
std::thread * metadataThread = nullptr;
std::thread * metadataExpirationThread = nullptr;


class AccumulatorLogger : public ConnectionLogger {
public:
    string accumulated = "";
    
    void log(string str) {
        accumulated = accumulated + str;
    }

    void log(void * sender, ConnectionLogType logType, Data * buffer) {
        if (buffer) {
            accumulated = accumulated + buffer->stringWithCharset("UTF-8")->UTF8Characters();
        }
    }
};

AccumulatorLogger alogger {};

void MCLogToAccumulatorLog(const char * user,
    const char * filename,
    unsigned int line,
    int dumpStack,
    const char * format, ...)
{
    va_list argp;
    va_start(argp, format);
    char buffer [5000];
    if (vsnprintf(buffer, 4999, format, argp) > 0) {
        alogger.log(buffer);
        alogger.log("\r\n");
    }

    va_end(argp);
}

struct CArg: public option::Arg
{
    static ArgStatus Required(const Option& option, bool)
    {
        return option.arg == 0 ? option::ARG_ILLEGAL : option::ARG_OK;
    }
    static ArgStatus Optional(const Option& option, bool)
    {
        return option.arg == 0 ? option::ARG_IGNORE : option::ARG_OK;
    }
    static ArgStatus Empty(const Option& option, bool)
    {
        return (option.arg == 0 || option.arg[0] == 0) ? option::ARG_OK : option::ARG_IGNORE;
    }
};

// Important do not change these without updating result code 2 check below
#define USAGE_STRING "USAGE: CONFIG_DIR_PATH=/path IDENTITY_SERVER=https://id.getmailspring.com mailsync [options]\n\nOptions:"
#define USAGE_IDENTITY "  --identity, -i  \tRequired: Mailspring Identity JSON with credentials."

enum  optionIndex { UNKNOWN, HELP, IDENTITY, ACCOUNT, MODE, ORPHAN, VERBOSE };
const option::Descriptor usage[] =
{
    {UNKNOWN, 0,"" , "",        CArg::None,      USAGE_STRING },
    {HELP,    0,"" , "help",    CArg::None,      "  --help  \tPrint usage and exit." },
    {IDENTITY,0,"a", "identity",CArg::Optional,  USAGE_IDENTITY },
    {ACCOUNT, 0,"a", "account", CArg::Optional,  "  --account, -a  \tRequired: Account JSON with credentials." },
    {MODE,    0,"m", "mode",    CArg::Required,  "  --mode, -m  \tRequired: sync, test, reset, calendar, migrate, or install-check." },
    {ORPHAN,  0,"o", "orphan",  CArg::None,      "  --orphan, -o  \tOptional: allow the process to run without a parent bound to stdin." },
    {VERBOSE, 0,"v", "verbose", CArg::None,      "  --verbose, -v  \tOptional: log all IMAP and SMTP traffic for debugging purposes." },
    {0,0,0,0,0,0}
};

void runForegroundSyncWorker() {
    while(true) {
        try {
            fgWorker->configure();
            fgWorker->idleCycleIteration();
            SharedDeltaStream()->endConnectionError(fgWorker->account->id());
        } catch (SyncException & ex) {
            exceptions::logCurrentExceptionWithStackTrace();
            if (!ex.isRetryable()) {
                abort();
            }
            if (ex.isOffline()) {
                SharedDeltaStream()->beginConnectionError(fgWorker->account->id());
            }
            spdlog::get("logger")->info("--sleeping");
            MailUtils::sleepWorkerUntilWakeOrSec(120);
        } catch (...) {
            exceptions::logCurrentExceptionWithStackTrace();
            abort();
        }
    }
}

void runBackgroundSyncWorker() {
    bool started = false;
    
    // wait a few seconds before launching. This avoids database locking caused by many
    // sync workers all trying to open several sqlite references at once.
    MailUtils::sleepWorkerUntilWakeOrSec(bgWorker->account->startDelay());

    while(true) {
        try {
            bgWorker->configure();
            
            // mark any existing folders as busy so the UI shows us syncing mail until
            // the sync worker gets through its first iteration.
            if (!started || bgWorkerShouldMarkAll) {
                bgWorker->markAllFoldersBusy();
                bgWorkerShouldMarkAll = false;
            }

            if (!started) {
                bgWorker->syncFoldersAndLabels();
                SharedDeltaStream()->endConnectionError(bgWorker->account->id());

                // start the "foreground" idle worker after we've completed a single
                // pass through all the folders. This ensures we have the folder list
                // and the uidnext / highestmodseq etc are populated.
                if (!fgThread) {
                    fgThread = new std::thread([&]() {
                        SetThreadName("foreground");
                        fgWorker = make_shared<SyncWorker>(bgWorker->account);
                        runForegroundSyncWorker();
                    });
                }

                started = true;
            }
            // run in a hard loop until it returns false, indicating continuation
            // is not necessary. Then sync and sleep for a bit. Interval can be long
            // because we're idling in another thread.
            bool moreToSync = true;
            while(moreToSync) {
                moreToSync = bgWorker->syncNow();
            }
            SharedDeltaStream()->endConnectionError(bgWorker->account->id());

        } catch (SyncException & ex) {
            exceptions::logCurrentExceptionWithStackTrace();
            if (!ex.isRetryable()) {
                abort();
            }
            if (ex.isOffline()) {
                SharedDeltaStream()->beginConnectionError(bgWorker->account->id());
            }
            spdlog::get("logger")->info("--sleeping");
        } catch (...) {
            exceptions::logCurrentExceptionWithStackTrace();
            abort();
        }
        MailUtils::sleepWorkerUntilWakeOrSec(120);
    }
}

void runCalContactsSyncWorker() {
    // wait a few seconds before launching calendar / contact sync. This gives
    // mailsync a minute to kick off and avoids database locking caused by many
    // sync workers all trying to open several sqlite references at once.
    std::this_thread::sleep_for(std::chrono::seconds(15 + davWorker->account->startDelay()));

    // BG Note: This process does not use MailUtils::sleepWorkerUntilWakeOrSec(), which means
    // cal + contact sync runs every 15 minutes regardless of how often you slam on the Sync Mail
    // icon. I am trying to narrow down why we are hitting the Google Calendar + People API limits
    // so quickly (in almost exactly 8 hours after the 2AM reset each day).

    while(true) {
        try {
            if (contactsWorker) {
                contactsWorker->run();
            }
            davWorker->run();
        } catch (SyncException & ex) {
            exceptions::logCurrentExceptionWithStackTrace();

            // Currently we do not allow calendar and contact sync to terminate mailsync (sending the
            // account into an error state.) If we hit something unrecoverable (like a CardDAV server
            // with an invalid SSL cert, we back off aggressively for 50min + 10min and then try again
            // indefinitely. It may never succeed, but if we don't kill the sync worker we must retry
            // eventually (in case the SSL cert error is caused by a boingo hotspot or something.)
            // or when a "wake" is triggered by the user!

            if (ex.debuginfo.find("dailyLimitExceeded") != string::npos) {
                spdlog::get("logger")->info("Suspending sync for 4 hours - Google app API request quota hit.");
                std::this_thread::sleep_for(std::chrono::hours(4));

            } else if (ex.key.find("Code: 403") != string::npos ||
                       ex.key.find("Code: 401") != string::npos ||
                       ex.debuginfo.find("invalid_grant") != string::npos) {
                spdlog::get("logger")->info("Stopping sync - unable to authenticate.");
                return;

            } else if (!ex.isRetryable()) {
                spdlog::get("logger")->error("Suspending sync for 90min - unlikely a retry would resolve this error.");
                std::this_thread::sleep_for(std::chrono::minutes(90));
                // abort();
            }
        } catch (...) {
            exceptions::logCurrentExceptionWithStackTrace();
            spdlog::get("logger")->info("Stopping cal/contacts sync. In the future this will kill the mailsync process.");
            return;
            // abort();
        }
        std::this_thread::sleep_for(std::chrono::minutes(45));
    }
}


int runTestAuth(shared_ptr<Account> account) {
    AutoreleasePool pool;

    // Enable very detailed mailcore logging and redirect the messages to our accumulator log
    MCLogEnabled = 1;
    MCLogFn = MCLogToAccumulatorLog;

    // NOTE: This method returns the account upon success but the client is not
    // reading the result. This function cannot mutate the account object.
    
    IMAPSession session;
    SMTPSession smtp;
    Array * folders;
    ErrorCode err = ErrorNone;
    Address * from = Address::addressWithMailbox(AS_MCSTR(account->emailAddress()));
    string errorService = "imap";
    string containerFolderPath = account->containerFolder();
    string mainPrefix = "";
    
    
    // imap
    alogger.log("----------IMAP----------\n");
    MailUtils::configureSessionForAccount(session, account);
    session.setConnectionLogger(&alogger);
    session.connect(&err);
    if (err != ErrorNone) {
        goto done;
    }
    folders = session.fetchAllFolders(&err);
    if (err != ErrorNone) {
        goto done;
    }
    
    mainPrefix = MailUtils::namespacePrefixOrBlank(&session);
    
    err = ErrorInvalidAccount;
    for (unsigned int i = 0; i < folders->count(); i ++) {
        // Gmail accounts must have "All Mail" enabled, IMAP accounts must have an Inbox.
        // Ensure we can find at least one folder matching the requirement.
        string role = MailUtils::roleForFolder(containerFolderPath, mainPrefix, (IMAPFolder *)folders->objectAtIndex(i));
        if (account->IMAPHost() == "imap.gmail.com") {
            if (role == "all") {
                err = ErrorNone;
                break;
            }
        } else {
            if (role == "inbox") {
                err = ErrorNone;
                break;
            }
        }
    }
    if (err != ErrorNone) {
        alogger.log("\n\nRequired folder not found. Ensure your account has an `Inbox` folder, or if this is a Gmail account, verify that `All Mail` is enabled for IMAP in your Gmail settings.\n");
        goto done;
    }

    // smtp
    alogger.log("\n\n----------SMTP----------\n");
    errorService = "smtp";
    smtp.setConnectionLogger(&alogger);
    MailUtils::configureSessionForAccount(smtp, account);
    smtp.checkAccount(from, &err);
    if (err != ErrorNone) {
        alogger.log("\n\nSASL_PATH: " + MailUtils::getEnvUTF8("SASL_PATH"));

        if (smtp.lastSMTPResponse()) {
            alogger.log("\n\nSMTP Last Response Code: " + to_string(smtp.lastSMTPResponseCode()));
            alogger.log("\nSMTP Last Response: " + string(smtp.lastSMTPResponse()->UTF8Characters()));
        }
        if (smtp.lastLibetpanError()) {
            int e = smtp.lastLibetpanError();
            string es = LibEtPanCodeToTypeMap.count(e) ? LibEtPanCodeToTypeMap[e] : "Unknown";

            alogger.log("\n\nmailsmtp Last Error Code: " + to_string(e));
            alogger.log("\nmailsmtp Last Error Explanation: " + es);
            alogger.log("\nmailsmtp Last Error Location: " + to_string(smtp.lastLibetpanErrorLocation()));
            alogger.log("\nmailsmtp Last Auth Type: " + to_string(smtp.authType()));
        }
        goto done;
    }
    
done:
    json resp = {
        {"error", nullptr},
        {"error_service", errorService},
        {"log", alogger.accumulated},
        {"account", nullptr}
    };
    if (err == ErrorNone) {
        resp["account"] = account->toJSON();
        cout << resp.dump();
        return 0;
    } else {
        resp["error"] = ErrorCodeToTypeMap.count(err) ? ErrorCodeToTypeMap[err] : "Unknown";
        cout << resp.dump();
        return 1;
    }
}

int runSingleFunctionAndExit(std::function<void()> fn) {
    json resp = {{"error", nullptr}};
    int code = 0;
    try {
        fn();
    } catch (std::exception & ex) {
        resp["error"] = ex.what();
        code = 1;
    }
    cout << "\n" << resp.dump();
    return code;
}

int runInstallCheck() {
    AutoreleasePool pool;
    json resp = {
        {"error", nullptr},
        {"http_check", nullptr},
        {"imap_check", nullptr},
        {"smtp_check", nullptr},
        {"tidy_check", nullptr}
    };

    // Step 1: Check HTTP connectivity to identity server using curl
    string httpError = "";
    long httpCode = 0;
    try {
        string identityServer = MailUtils::getEnvUTF8("IDENTITY_SERVER");
        string pingUrl = identityServer + "/ping";
        CURL * curl_handle = CreateJSONRequest(pingUrl, "GET");

        string result;
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _onAppendToString);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&result);

        CURLcode res = curl_easy_perform(curl_handle);
        if (res != CURLE_OK) {
            httpError = string("curl error: ") + curl_easy_strerror(res);
        } else {
            curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &httpCode);
            // Any HTTP response code means curl and SSL worked
        }
        // Use shared cleanup which also frees the header list
        CleanupCurlRequest(curl_handle);
    } catch (std::exception & ex) {
        httpError = ex.what();
    }

    if (httpError != "") {
        resp["http_check"] = {{"error", httpError}};
    } else {
        resp["http_check"] = {{"status_code", httpCode}};
    }

    // Step 2: Check IMAP connectivity to Gmail to verify SSL libraries work
    string imapError = "";
    try {
        IMAPSession session;
        session.setHostname(MCSTR("imap.gmail.com"));
        session.setPort(993);
        session.setConnectionType(ConnectionType::ConnectionTypeTLS);
        // No username/password - we just want to verify SSL connection works

        ErrorCode err = ErrorNone;
        session.connect(&err);

        // Connection succeeded or failed with auth error (both mean SSL works)
        if (err != ErrorNone && err != ErrorAuthentication && err != ErrorAuthenticationRequired) {
            imapError = ErrorCodeToTypeMap.count(err) ? ErrorCodeToTypeMap[err] : ("IMAP error code: " + to_string(err));
        }
        // If we get here with ErrorAuthentication or ErrorAuthenticationRequired, SSL worked
        session.disconnect();
    } catch (std::exception & ex) {
        imapError = ex.what();
    }

    if (imapError != "") {
        resp["imap_check"] = {{"error", imapError}};
    } else {
        resp["imap_check"] = {{"success", true}};
    }

    // Step 3: Check SMTP connectivity to Gmail to verify SSL libraries work for SMTP
    string smtpError = "";
    try {
        SMTPSession smtp;
        smtp.setHostname(MCSTR("smtp.gmail.com"));
        smtp.setPort(465);
        smtp.setConnectionType(ConnectionType::ConnectionTypeTLS);
        // No username/password - we just want to verify SSL connection works

        ErrorCode err = ErrorNone;
        smtp.connect(&err);

        // Connection succeeded or failed with auth error (both mean SSL works)
        if (err != ErrorNone && err != ErrorAuthentication && err != ErrorAuthenticationRequired) {
            smtpError = ErrorCodeToTypeMap.count(err) ? ErrorCodeToTypeMap[err] : ("SMTP error code: " + to_string(err));
        }
        // If we get here with ErrorAuthentication or ErrorAuthenticationRequired, SSL worked
        smtp.disconnect();
    } catch (std::exception & ex) {
        smtpError = ex.what();
    }

    if (smtpError != "") {
        resp["smtp_check"] = {{"error", smtpError}};
    } else {
        resp["smtp_check"] = {{"success", true}};
    }

    // Step 4: Check libtidy by actually processing HTML (Linux only)
    string tidyError = "";
#if defined(__linux__)
    if (!mailspring_tidy_available()) {
        const char* err = mailspring_tidy_error();
        tidyError = err ? err : "libtidy not available";
    } else {
        // Actually test tidy by processing sample HTML, same as MCHTMLCleaner::cleanHTML
        MSTidyBuffer output = {0};
        MSTidyBuffer errbuf = {0};
        MSTidyBuffer docbuf = {0};

        MSTidyDoc tdoc = mailspring_tidyCreate();
        if (tdoc == NULL) {
            tidyError = "tidyCreate returned NULL";
        } else {
            mailspring_tidyBufInit(&output);
            mailspring_tidyBufInit(&errbuf);
            mailspring_tidyBufInit(&docbuf);

            // Test with simple HTML
            const char* testHTML = "<html><body><p>Test</p></body></html>";
            mailspring_tidyBufAppend(&docbuf, (void*)testHTML, strlen(testHTML));

            // Use dynamically resolved option IDs for libtidy version compatibility
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

            if (parseResult < 0 || cleanResult < 0 || saveResult < 0) {
                tidyError = "tidy processing failed (parse=" + to_string(parseResult) +
                           ", clean=" + to_string(cleanResult) +
                           ", save=" + to_string(saveResult) + ")";
            } else if (output.bp == NULL) {
                tidyError = "tidy produced no output";
            }

            mailspring_tidyBufFree(&docbuf);
            mailspring_tidyBufFree(&output);
            mailspring_tidyBufFree(&errbuf);
            mailspring_tidyRelease(tdoc);
        }
    }
#endif

    if (tidyError != "") {
        resp["tidy_check"] = {{"error", tidyError}};
    } else {
        resp["tidy_check"] = {{"success", true}};
    }

    // Determine overall success
    bool success = (httpError == "" && imapError == "" && smtpError == "" && tidyError == "");
    if (!success) {
        resp["error"] = "One or more checks failed";
    }

    cout << resp.dump();
    return success ? 0 : 1;
}

void runListenOnMainThread(shared_ptr<Account> account) {
    MailStore store;
    TaskProcessor processor{account, &store, nullptr};

    store.setStreamDelay(5);

    time_t lostCINAt = 0;

    processor.cleanupTasksAfterLaunch();
    
    while(true) {
        AutoreleasePool pool;
        json packet = {};
        try {
            packet = SharedDeltaStream()->waitForJSON();
        } catch (std::invalid_argument & ex) {
            json resp = {{"error", ex.what()}};
            spdlog::get("logger")->error(resp.dump());
            cout << "\n" << resp.dump() << "\n";
            continue;
        }

        // cin is interrupted when the debugger attaches, and that's ok. If cin is
        // disconnected for more than 30 seconds, it means we have been oprhaned and
        // we should exit.
        if (cin.good()) {
            lostCINAt = 0;
        } else {
            if (lostCINAt == 0) {
                lostCINAt = time(0);
            }
            if (time(0) - lostCINAt > 30) {
                // note: don't run termination / stack trace handlers,
                // just exit.
                std::exit(141);
            }
			std::this_thread::sleep_for(std::chrono::microseconds(1000));
        }

        try {
            string type = packet.count("type") ? packet["type"].get<string>() : "";

            if (type == "queue-task") {
                packet["task"]["v"] = 0;
                Task task{packet["task"]};
                processor.performLocal(&task);
        
                // interrupt the foreground sync worker to do the remote part of the task. We wait a short time
                // because we want tasks queued back to back to run ASAP and not fight for locks with remote
                // syncback. This also mitigates any potential remote loads+saves that aren't inside transactions
                // and could overwrite local changes.
                static atomic<bool> queuedForegroundWake { false };
                bool expected = false;
                if (queuedForegroundWake.compare_exchange_strong(expected, true)) {
                    std::thread([]() {
                        std::this_thread::sleep_for(chrono::milliseconds(300));
                        if (fgWorker) {
                            fgWorker->idleInterrupt();
                        }
                        queuedForegroundWake = false;
                    }).detach();
                }
            }
            
            if (type == "cancel-task") {
                // we can't always dequeue a task (if it's started already or potentially even finished).
                // but if we're deleting a draft we want to dequeue saves, etc.
                processor.cancel(packet["taskId"].get<string>());
            }
            
            if (type == "wake-workers") {
                spdlog::get("logger")->info("Waking all workers...");

                // mark that the background worker should mark all the folders as busy
                // (on it's thread!)
                bgWorkerShouldMarkAll = true;
                
                // Wake the workers
                MailUtils::wakeAllWorkers();
                
                // interrupt the foreground worker's IDLE call, because our network
                // connection may have been reset and it'll sit for a while otherwise
                // and wake-workers is called when waking from sleep
                if (fgWorker) fgWorker->idleInterrupt();
            }

            if (type == "need-bodies") {
                // interrupt the foreground sync worker to do the remote part of the task
                vector<string> ids{};
                for (auto id : packet["ids"]) {
                    ids.push_back(id.get<string>());
                }
                if (fgWorker) fgWorker->idleQueueBodiesToSync(ids);
                if (fgWorker) fgWorker->idleInterrupt();
            }

            if (type == "sync-calendar") {
                static atomic<bool> runningCalendarSync { false };
                bool expected = false;
                if (runningCalendarSync.compare_exchange_strong(expected, true)) {
                    std::thread([account]() {
                        SetThreadName("calendar");
                        auto worker = DAVWorker(account);
                        worker.run();
                        runningCalendarSync = false;
                    }).detach();
                }
            }

            if (type == "test-crash") {
                throw SyncException("test", "triggered via cin", false);
            }
            if (type == "test-segfault") {
                raise(SIGSEGV);
            }
        } catch (...) {
            exceptions::logCurrentExceptionWithStackTrace();
            abort();
        }
    }
}

int main(int argc, const char * argv[]) {
    SetThreadName("main");

    // indicate we use cout, not stdout
    std::cout.sync_with_stdio(false);
    
string exectuablePath = argv[0];

#ifndef DEBUG
    // check path to executable in an obtuse way, prevent re-use of
    // Mailspring-Sync in products / forks not called Mailspring.
    transform(exectuablePath.begin(), exectuablePath.end(), exectuablePath.begin(), ::tolower);
    string headerMessageId = string(USAGE_STRING).substr(59, 4) + string(USAGE_IDENTITY).substr(33, 6);
    if (exectuablePath.find(headerMessageId) == string::npos) {
        return 2;
    }
#endif

    // initialize the stanford exception handler
    exceptions::setProgramNameForStackTrace(exectuablePath.c_str());
    exceptions::setTopLevelExceptionHandlerEnabled(true);

    // parse launch arguments, skip program name argv[0] if present
    argc-=(argc>0); argv+=(argc>0);
    option::Stats stats(usage, argc, argv);
    vector<option::Option> options(stats.options_max);
    vector<option::Option> buffer(stats.buffer_max);
    option::Parser parse(usage, argc, argv, &options[0], &buffer[0]);

    if (parse.error())
        return 1;
    
    if (options[HELP] || argc == 0) {
        option::printUsage(std::cout, usage);
        return 0;
    }
    
    // check required environment
    string eConfigDirPath = MailUtils::getEnvUTF8("CONFIG_DIR_PATH");
    string eIdentityServer = MailUtils::getEnvUTF8("IDENTITY_SERVER");

    if ((eConfigDirPath == "") || (eIdentityServer == "")) {
        option::printUsage(std::cout, usage);
        return 1;
    }

    // initialize SQLite3 cache directory to the config dir path, ensuring we store
    // /everything/ in the place the user has specified in case it's symlinked, on
    // another volume, etc.
    sqlite3_temp_directory = sqlite3_mprintf("%s", eConfigDirPath.c_str());

    // handle --mode migrate early for speed
    string mode(options[MODE].arg);
    
    if (mode == "migrate") {
        return runSingleFunctionAndExit([](){
            MailStore store;
            store.migrate();
        });
    }

    if (mode == "install-check") {
        // setup curl for install check
        curl_global_init(CURL_GLOBAL_ALL);
        return runInstallCheck();
    }

	// get the account via param or stdin
    string accountJSON = "";
    if (options[ACCOUNT].count() > 0) {
        Option ac = options[ACCOUNT];
        accountJSON = string(options[ACCOUNT].arg);
    } else {
        cout << "\nWaiting for Account JSON:\n";
        getline(cin, accountJSON);
    }
    shared_ptr<Account> account = nullptr;
    try {
        account = make_shared<Account>(json::parse(accountJSON));
    } catch (json::exception& e) {
        json resp = { { "error", "Invalid Account JSON: " + string(e.what()) }, { "log", accountJSON } };
        cout << "\n" << resp.dump();
        return 1;
    }


	if (account->valid() != "") {
		json resp = { { "error", "Account is missing required fields:" + account->valid() } };
		cout << "\n" << resp.dump();
		return 1;
	}
    
    if (mode == "reset") {
        return runSingleFunctionAndExit([&](){
            MailStore store;
            store.resetForAccount(account->id());
        });
    }

	// get the identity via param or stdin
    string identityJSON = "";
	if (options[IDENTITY].count() > 0) {
		Option ac = options[IDENTITY];
		identityJSON = string(options[IDENTITY].arg);
	} else {
		cout << "\nWaiting for Identity JSON:\n";
        getline(cin, identityJSON);
	}
    try {
        if (identityJSON == "null") {
            Identity::SetGlobal(nullptr);
        } else {
            Identity::SetGlobal(make_shared<Identity>(json::parse(identityJSON)));
        }
    } catch (json::exception& e) {
        json resp = { { "error", "Invalid Identity JSON: " + string(e.what()) }, { "log", identityJSON } };
        cout << "\n" << resp.dump();
        return 1;
    }

	if (Identity::GetGlobal() && !Identity::GetGlobal()->valid()) {
		json resp = { { "error", "ErrorIdentityMissingFields" } };
		cout << "\n" << resp.dump();
		return 1;
	}
    
    std::vector<shared_ptr<spdlog::sinks::sink>> sinks;
    bool logToFile = mode == "sync" && !options[ORPHAN];

    try {
        if (logToFile) {
            // If we're attached to the mail client, log everything to a
            // rotating log file with the default logger format.
            // IMPORANT: On Windows, only one sync worker can have this file open at once.
            spdlog::set_formatter(std::make_shared<SPDFormatterWithThreadNames>("%P %+"));
    #if defined(_MSC_VER)
            wstring_convert<codecvt_utf8<wchar_t>, wchar_t> convert;
            wstring logPath = convert.from_bytes(eConfigDirPath) + convert.from_bytes(FS_PATH_SEP + "mailsync-" + account->id() + ".log");
    #else
            string logPath = eConfigDirPath + FS_PATH_SEP + "mailsync-" + account->id() + ".log";
    #endif
            sinks.push_back(make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath, 1048576 * 5, 3));
            sinks.push_back(make_shared<SPDFlusherSink>());
        } else {
            // If we're attached to a debugger / console, log everything to
            // stdout in an abbreviated format.
            spdlog::set_formatter(std::make_shared<SPDFormatterWithThreadNames>("%l: %v"));
    #if defined(_MSC_VER)
            sinks.push_back(make_shared<spdlog::sinks::stdout_sink_mt>());
    #else
            sinks.push_back(make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>());
    #endif
        }
    } catch (spdlog::spdlog_ex& e) {
        json resp = { { "error", "Setup Failed: " + string(e.what()) } };
        cout << "\n" << resp.dump();
        return 1;
    }

    // Always log critical errors to the stderr as well as a log file / stdout.
    // When attached to the client, these are saved and if we terminate, reported.
    auto stderr_sink = make_shared<spdlog::sinks::stderr_sink_mt>();
    stderr_sink->set_level(spdlog::level::critical);
    sinks.push_back(stderr_sink);
    
    spdlog::create("logger", std::begin(sinks), std::end(sinks));

    time_t createdAt = Identity::GetGlobal() ? Identity::GetGlobal()->createdAt() : time(0);
    MailUtils::setBaseIDVersion(createdAt);
    if (options[VERBOSE]) {
        MailUtils::enableVerboseLogging();
    }

    // setup curl
    curl_global_init(CURL_GLOBAL_ALL);

    if (mode == "test") {
        return runTestAuth(account);
    }

    if (mode == "sync") {
        spdlog::get("logger")->info("------------- Starting Sync ({}) ---------------", account->emailAddress());

        fgThread = nullptr; // started after background iteration
        bgThread = new std::thread([&]() {
            SetThreadName("background");
            bgWorker = make_shared<SyncWorker>(account);
            runBackgroundSyncWorker();
        });
        calContactsThread = new std::thread([&]() {
            SetThreadName("calContacts");
            if (account->provider() == "gmail") {
                contactsWorker = make_shared<GoogleContactsWorker>(account);
            }
            davWorker = make_shared<DAVWorker>(account);
            runCalContactsSyncWorker();
        });
        metadataThread = new std::thread([&]() {
             SetThreadName("metadata");
             metadataWorker = make_shared<MetadataWorker>(account);
             metadataWorker->run();
        });
        metadataExpirationThread = new std::thread([&]() {
            SetThreadName("metadataExpiration");
            metadataExpirationWorker = make_shared<MetadataExpirationWorker>(account->id());
            metadataExpirationWorker->run();
        });
        
        if (!options[ORPHAN]) {
            runListenOnMainThread(account);
        } else {
            bgThread->join(); // will block forever.
        }
    }
    
    return 0;
}
