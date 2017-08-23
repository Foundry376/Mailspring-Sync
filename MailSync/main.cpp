//
//  main.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include <iostream>
#include <string>
#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include <StanfordCPPLib/exceptions.h>
#include <curl/curl.h>
#include "json.hpp"
#include "spdlog/spdlog.h"
#include "optionparser.h"

#include "Account.hpp"
#include "Identity.hpp"
#include "MailUtils.hpp"
#include "MailStore.hpp"
#include "DeltaStream.hpp"
#include "SyncWorker.hpp"
#include "MetadataWorker.hpp"
#include "SyncException.hpp"
#include "Task.hpp"
#include "TaskProcessor.hpp"
#include "ThreadUtils.h"
#include "constants.h"


using json = nlohmann::json;
using option::Option;
using option::Descriptor;
using option::Parser;
using option::Stats;
using option::ArgStatus;

shared_ptr<SyncWorker> bgWorker = nullptr;
shared_ptr<SyncWorker> fgWorker = nullptr;
shared_ptr<MetadataWorker> metadataWorker = nullptr;

std::thread * fgThread = nullptr;
std::thread * bgThread = nullptr;
std::thread * metadataThread = nullptr;

class AccumulatorLogger : public ConnectionLogger {
public:
    string accumulated;
    
    void log(string str) {
        accumulated = accumulated + str;
    }

    void log(void * sender, ConnectionLogType logType, Data * buffer) {
        accumulated = accumulated + buffer->bytes();
    }
};

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


enum  optionIndex { UNKNOWN, HELP, IDENTITY, ACCOUNT, MODE, ORPHAN };
const option::Descriptor usage[] =
{
    {UNKNOWN, 0,"" , "",        CArg::None,      "USAGE: CONFIG_DIR_PATH=/path IDENTITY_SERVER=https://id.getmerani.com "
                                                 "ACCOUNTS_SERVER https://accounts.getmerani.com mailsync [options]\n\nOptions:" },
    {HELP,    0,"" , "help",    CArg::None,      "  --help  \tPrint usage and exit." },
    {IDENTITY,0,"a", "identity",CArg::Optional,  "  --identity, -i  \tRequired: Merani Identity JSON with credentials." },
    {ACCOUNT, 0,"a", "account", CArg::Optional,  "  --account, -a  \tRequired: Account JSON with credentials." },
    {MODE,    0,"m", "mode",    CArg::Required,  "  --mode, -m  \tRequired: sync, test, or migrate." },
    {ORPHAN,  0,"o", "orphan",  CArg::None,      "  --orphan, -o  \tOptional: allow the process to run without a parent bound to stdin." },
    {0,0,0,0,0,0}
};


void runMetadataWorker() {
    SetThreadName("metadata");
    metadataWorker->run();
}

void runForegroundSyncWorker() {
    SetThreadName("fgWorker");

    // run tasks, sync changes, idle, repeat
    fgWorker->idleCycle();
}

void runBackgroundSyncWorker() {
    SetThreadName("bgWorker");

    bool started = false;
    
    while(true) {
        try {
            // start the "foreground" idle worker after we've completed a single
            // pass through all the folders. This ensures we have the folder list
            // and the uidnext / highestmodseq etc are populated.
            if (!started) {
                bgWorker->syncFoldersAndLabels();
                started = true;
                if (!fgThread) {
                    fgThread = new std::thread(runForegroundSyncWorker);
                }
            }
            // run in a hard loop until it returns false, indicating continuation
            // is not necessary. Then sync and sleep for a bit. Interval can be long
            // because we're idling in another thread.
            bool moreToSync = true;
            while(moreToSync) {
                moreToSync = bgWorker->syncNow();
            }

        } catch (SyncException & ex) {
            if (!ex.isRetryable()) {
                throw;
            }
        }
        sleep(120);
    }
}

int runTestAuth(shared_ptr<Account> account) {
    IMAPSession session;
    SMTPSession smtp;
    AccumulatorLogger logger;
    Array * folders;
    ErrorCode err = ErrorNone;
    string errorService = "imap";

    // imap
    logger.log("----------IMAP----------\n");
    MailUtils::configureSessionForAccount(session, account);
    session.setConnectionLogger(&logger);
    session.connect(&err);
    if (err != ErrorNone) {
        goto done;
    }
    folders = session.fetchAllFolders(&err);
    if (err != ErrorNone) {
        goto done;
    }
    
    err = ErrorInvalidAccount;
    for (int i = 0; i < folders->count(); i ++) {
        string role = MailUtils::roleForFolder((IMAPFolder *)folders->objectAtIndex(i));
        if ((role == "all") || (role == "inbox")) {
            err = ErrorNone;
            break;
        }
    }
    
    // smtp
    logger.log("\n\n----------SMTP----------\n");
    errorService = "smtp";
    smtp.setConnectionLogger(&logger);
    MailUtils::configureSessionForAccount(smtp, account);
    smtp.connect(&err);
    if (err != ErrorNone) {
        goto done;
    }
    smtp.loginIfNeeded(&err);
    if (err != ErrorNone) {
        goto done;
    }
    
done:
    json resp = {
        {"error", nullptr},
        {"error_service", errorService},
        {"log", logger.accumulated},
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

int runMigrate() {
    json resp = {{"error", nullptr}};
    int code = 0;
    try {
        MailStore store;
        store.migrate();
    } catch (std::exception & ex) {
        resp["error"] = ex.what();
        code = 1;
    }
    cout << "\n" << resp.dump();
    return code;
}

void runListenOnMainThread(shared_ptr<Account> account) {
    MailStore store;
    TaskProcessor processor{account, &store, nullptr};

    store.setStreamDelay(5);
    
#ifndef DEBUG
    time_t lostCINAt = 0;
#endif

    while(true) {
        AutoreleasePool pool;
        json packet = {};
        try {
            packet = SharedDeltaStream()->waitForJSON();
        } catch (std::invalid_argument & ex) {
            json resp = {{"error", ex.what()}};
            spdlog::get("main")->error(resp.dump());
            cout << "\n" << resp.dump() << "\n";
            continue;
        }

        // cin is interrupted when the debugger attaches, and that's ok. If cin is
        // disconnected for more than 30 seconds, it means we have been oprhaned and
        // we should exit.
#ifndef DEBUG
        if (cin.good()) {
            lostCINAt = 0;
        } else {
            if (lostCINAt == 0) {
                lostCINAt = time(0);
            }
            if (time(0) - lostCINAt > 30) {
                terminate();
            }
            usleep(1000);
        }
#endif

        if (packet.count("type") && packet["type"].get<string>() == "queue-task") {
            packet["task"]["v"] = 0;
            Task task{packet["task"]};
            processor.performLocal(&task);
    
            // interrupt the foreground sync worker to do the remote part of the task
            fgWorker->idleInterrupt();
        }
        
        if (packet.count("type") && packet["type"].get<string>() == "cancel-task") {
            // we can't always dequeue a task (if it's started already or potentially even finished).
            // but if we're deleting a draft we want to dequeue saves, etc.
            processor.cancel(packet["taskId"].get<string>());
        }

        if (packet.count("type") && packet["type"].get<string>() == "need-bodies") {
            // interrupt the foreground sync worker to do the remote part of the task
            vector<string> ids{};
            for (auto id : packet["ids"]) {
                ids.push_back(id.get<string>());
            }
            fgWorker->idleQueueBodiesToSync(ids);
            fgWorker->idleInterrupt();
        }
    }
}

int main(int argc, const char * argv[]) {
    
    // indicate we use cout, not stdout
    std::cout.sync_with_stdio(false);
    
    // initialize the stanford exception handler
    exceptions::setProgramNameForStackTrace(argv[0]);
    exceptions::setTopLevelExceptionHandlerEnabled(true);

    // parse launch arguments, skip program name argv[0] if present
    argc-=(argc>0); argv+=(argc>0);
    option::Stats  stats(usage, argc, argv);
    option::Option options[stats.options_max], buffer[stats.buffer_max];
    option::Parser parse(usage, argc, argv, options, buffer);
    
    if (parse.error())
        return 1;
    
    if (options[HELP] || argc == 0) {
        option::printUsage(std::cout, usage);
        return 1;
    }
    
    // check required environment
    if ((getenv("CONFIG_DIR_PATH") == nullptr) ||
        (getenv("IDENTITY_SERVER") == nullptr) ||
        (getenv("ACCOUNTS_SERVER") == nullptr)) {
        option::printUsage(std::cout, usage);
        return 1;
    }

    // handle --mode migrate early for speed
    string mode(options[MODE].arg);
    
    if (mode == "migrate") {
        return runMigrate();
    }

    // setup logging to file or console
    std::vector<shared_ptr<spdlog::sinks::base_sink<std::mutex>>> sinks;

    if (!options[ORPHAN]) {
        // If we're attached to the mail client, log everything to a
        // rotating log file with the default logger format.
        spdlog::set_async_mode(8192, spdlog::async_overflow_policy::block_retry, nullptr, std::chrono::seconds(3), nullptr);
        spdlog::set_pattern("%+");

        string logPath = string(getenv("CONFIG_DIR_PATH")) + "/mailsync.log";
        sinks.push_back(make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath, 1048576 * 5, 3));
    } else {
        // If we're attached to a debugger / console, log everything to
        // stdout in an abbreviated format.
        spdlog::set_sync_mode();
        spdlog::set_pattern("%l: %v");
        sinks.push_back(make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>());
    }

    // Always log critical errors to the stderr as well as a log file / stdout.
    // When attached to the client, these are saved and if we terminates, reported.
    auto stderr_sink = make_shared<spdlog::sinks::stderr_sink_mt>();
    stderr_sink->set_level(spdlog::level::critical);
    sinks.push_back(stderr_sink);
    
    spdlog::create("main", std::begin(sinks), std::end(sinks));
    spdlog::create("processor", std::begin(sinks), std::end(sinks));
    spdlog::create("metadata", std::begin(sinks), std::end(sinks));
    spdlog::create("tasks", std::begin(sinks), std::end(sinks));
    spdlog::create("fg", std::begin(sinks), std::end(sinks));
    spdlog::create("bg", std::begin(sinks), std::end(sinks));

    // setup curl
    curl_global_init(CURL_GLOBAL_ALL ^ CURL_GLOBAL_SSL);

    // get the account via param or stdin
    shared_ptr<Account> account;
    if (options[ACCOUNT].count() > 0) {
        Option ac = options[ACCOUNT];
        const char * arg = options[ACCOUNT].arg;
        account = make_shared<Account>(json::parse(arg));
    } else {
        cout << "\nWaiting for Account JSON:\n";
        string inputLine;
        getline(cin, inputLine);
        account = make_shared<Account>(json::parse(inputLine));
    }
    
    if (!account->valid()) {
        json resp = {{"error", "Account is missing required fields."}};
        cout << "\n" << resp.dump();
        return 1;
    }
    
    // get the identity via param or stdin
    if (options[IDENTITY].count() > 0) {
        Option ac = options[IDENTITY];
        const char * arg = options[IDENTITY].arg;
        Identity::SetGlobal(make_shared<Identity>(json::parse(arg)));
    } else {
        cout << "\nWaiting for Identity JSON:\n";
        string inputLine;
        getline(cin, inputLine);
        Identity::SetGlobal(make_shared<Identity>(json::parse(inputLine)));
    }
    
    if (!Identity::GetGlobal()->valid()) {
        json resp = {{"error", "Identity is missing required fields."}};
        cout << "\n" << resp.dump();
        return 1;
    }

    if (mode == "test") {
        return runTestAuth(account);
    }

    if (mode == "sync") {
        if (!account->hasCloudToken()) {
            json resp = {{"error", "Account must have a cloud token for sync."}};
            cout << "\n" << resp.dump();
            return 1;
        }

        spdlog::get("main")->info("------------- Starting Sync ---------------");
        bgWorker = make_shared<SyncWorker>("bg", account);
        fgWorker = make_shared<SyncWorker>("fg", account);
        metadataWorker = make_shared<MetadataWorker>(account);

        bgThread = new std::thread(runBackgroundSyncWorker);
        metadataThread = new std::thread(runMetadataWorker);
        
        if (!options[ORPHAN]) {
            runListenOnMainThread(account);
        } else {
            bgThread->join(); // will block forever.
        }
    }

    return 0;
}
