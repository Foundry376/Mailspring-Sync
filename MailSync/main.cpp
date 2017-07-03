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
#include "json.hpp"
#include "spdlog/spdlog.h"
#include "optionparser.h"

#include "Account.hpp"
#include "MailUtils.hpp"
#include "MailStore.hpp"
#include "CommStream.hpp"
#include "SyncWorker.hpp"
#include "Task.hpp"
#include "TaskProcessor.hpp"
#include "constants.h"

using json = nlohmann::json;
using option::Option;
using option::Descriptor;
using option::Parser;
using option::Stats;
using option::ArgStatus;

CommStream * stream = nullptr;
SyncWorker * bgWorker = nullptr;
SyncWorker * fgWorker = nullptr;

std::thread * fgThread = nullptr;
std::thread * bgThread = nullptr;

class AccumulatorLogger : public ConnectionLogger {
public:
    string accumulated;

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


enum  optionIndex { UNKNOWN, HELP, ACCOUNT, MODE };
const option::Descriptor usage[] =
{
    {UNKNOWN, 0,"" , "",        CArg::None,      "USAGE: mailsync [options]\n\nOptions:" },
    {HELP,    0,"" , "help",    CArg::None,      "  --help  \tPrint usage and exit." },
    {ACCOUNT, 0,"a", "account", CArg::Optional,  "  --account, -a  \tRequired: Account JSON with credentials." },
    {MODE,    0,"m", "mode",    CArg::Required,  "  --mode, -m  \tRequired: sync, test, or migrate." },
    {0,0,0,0,0,0}
};



void runForegroundSyncWorker() {
    // run tasks, sync changes, idle, repeat
    fgWorker->idleCycle();
}

void runBackgroundSyncWorker() {
    bool firstLoop = true;
    
    while(true) {
        // run in a hard loop until it returns false, indicating continuation
        // is not necessary. Then sync and sleep for a bit. Interval can be long
        // because we're idling in another thread.
        bool moreToSync = true;
        while(moreToSync) {
            moreToSync = bgWorker->syncNow();

            // start the "foreground" idle worker after we've completed a single
            // pass through all the folders. This ensures we have the folder list
            // and the uidnext / highestmodseq etc are populated.
            if (firstLoop) {
                fgThread = new std::thread(runForegroundSyncWorker);
                firstLoop = false;
            }
        }
        sleep(120);
    }
}

int runTestAuth(shared_ptr<Account> account) {
    IMAPSession session;
    AccumulatorLogger logger;
    Array * folders;
    ErrorCode err = ErrorNone;

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

done:
    json resp = {
        {"error", nullptr},
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

void runMainThread() {
    auto logger = spdlog::stdout_color_mt("main");

    MailStore store;
    store.addObserver(stream);
    
    TaskProcessor processor{&store, logger, nullptr};
    
    while(true) {
        AutoreleasePool pool;
        json packet = stream->waitForJSON();
        
        if (packet.count("type") && packet["type"].get<string>() == "task-queued") {
            packet["task"]["v"] = 0;

            Task task{packet["task"]};
            processor.performLocal(&task);
    
            // interrupt the foreground sync worker to do the remote part of the task
            fgWorker->idleInterrupt();
        }

        if (packet.count("type") && packet["type"].get<string>() == "need-bodies") {
            // interrupt the foreground sync worker to do the remote part of the task
            logger->info("Received bodies request. Interrupting idle...");
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
    spdlog::set_pattern("%l: [%L] %v");

    argc-=(argc>0); argv+=(argc>0); // skip program name argv[0] if present
    option::Stats  stats(usage, argc, argv);
    option::Option options[stats.options_max], buffer[stats.buffer_max];
    option::Parser parse(usage, argc, argv, options, buffer);
    
    if (parse.error())
        return 1;
    
    if (options[HELP] || argc == 0) {
        option::printUsage(std::cout, usage);
        return 0;
    }
    
    string mode(options[MODE].arg);
    
    if (mode == "migrate") {
        MailStore store;
        return 0;
    }
    
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
        cout << "Account is missing required fields.\n";
        return 1;
    }

    if (mode == "test") {
        return runTestAuth(account);
    }

    if (mode == "sync") {
        stream = new CommStream((char *)"/tmp/cmail.sock");
        bgWorker = new SyncWorker("bg", account, stream);
        fgWorker = new SyncWorker("fg", account, stream);

        fgThread = new std::thread(runBackgroundSyncWorker); // SHOULD BE BACKGROUND
        runMainThread();
    }

    return 0;
}
