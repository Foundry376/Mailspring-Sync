/** SyncWorker [MailSync]
 *
 * Author(s): Ben Gotow
 */

/* LICENSE
* Copyright (C) 2017-2021 Foundry 376.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SyncWorker_hpp
#define SyncWorker_hpp

#include <stdio.h>

#include <string>
#include <vector>
#include "MailCore/MailCore.h"

#include "mailsync/models/account.hpp"
#include "mailsync/mail_store.hpp"
#include "mailsync/mail_processor.hpp"
#include "mailsync/delta_stream.hpp"
#include "mailsync/models/folder.hpp"



class SyncWorker {
    mailcore::IMAPSession session;

    MailStore * store;
    MailProcessor * processor;
    std::shared_ptr<spdlog::logger> logger;

    int unlinkPhase;
    bool idleShouldReloop;
    int iterationsSinceLaunch;
    std::vector<std::string> idleFetchBodyIDs;
    std::mutex idleMtx;
    std::condition_variable idleCv;

public:

    std::shared_ptr<Account> account;

    SyncWorker(std::shared_ptr<Account> account);
    void configure();

#pragma mark Foreground Worker

public:

    void idleInterrupt();
    void idleQueueBodiesToSync(std::vector<std::string> & ids);
    void idleCycleIteration();


#pragma mark Background Worker

public:

    bool syncNow();

    void markAllFoldersBusy();

    std::vector<std::shared_ptr<Folder>> syncFoldersAndLabels();

private:

    void ensureRootMailspringFolder(mailcore::Array * remoteFolders);

    bool initialSyncFolderIncremental(Folder & folder, mailcore::IMAPFolderStatus & remoteStatus);

    void syncFolderUIDRange(Folder & folder, mailcore::Range range, bool heavyInitialRequest, std::vector<std::shared_ptr<Message>> * syncedMessages = nullptr);

    void syncFolderChangesViaCondstore(Folder & folder, mailcore::IMAPFolderStatus & remoteStatus, bool mustSyncAll);

    void fetchRangeInFolder(mailcore::String * folder, std::string folderId, mailcore::Range range);

    void cleanMessageCache(Folder & folder);

    long long countBodiesDownloaded(Folder & folder);
    long long countBodiesNeeded(Folder & folder);
    time_t maxAgeForBodySync(Folder & folder);
    bool shouldCacheBodiesInFolder(Folder & folder);
    bool syncMessageBodies(Folder & folder, mailcore::IMAPFolderStatus & remoteStatus);
    void syncMessageBody(Message * message);
};


#endif /* SyncWorker_hpp */
