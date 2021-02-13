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

using namespace mailcore;

class SyncWorker {
    IMAPSession session;

    MailStore * store;
    MailProcessor * processor;
    shared_ptr<spdlog::logger> logger;

    int unlinkPhase;
    bool idleShouldReloop;
    int iterationsSinceLaunch;
    vector<string> idleFetchBodyIDs;
    std::mutex idleMtx;
    std::condition_variable idleCv;

public:

    shared_ptr<Account> account;

    SyncWorker(shared_ptr<Account> account);
    void configure();

#pragma mark Foreground Worker

public:

    void idleInterrupt();
    void idleQueueBodiesToSync(vector<string> & ids);
    void idleCycleIteration();


#pragma mark Background Worker

public:

    bool syncNow();

    void markAllFoldersBusy();

    std::vector<std::shared_ptr<Folder>> syncFoldersAndLabels();

private:

    void ensureRootMailspringFolder(Array * remoteFolders);

    bool initialSyncFolderIncremental(Folder & folder, IMAPFolderStatus & remoteStatus);

    void syncFolderUIDRange(Folder & folder, Range range, bool heavyInitialRequest, vector<shared_ptr<Message>> * syncedMessages = nullptr);

    void syncFolderChangesViaCondstore(Folder & folder, IMAPFolderStatus & remoteStatus, bool mustSyncAll);

    void fetchRangeInFolder(String * folder, std::string folderId, Range range);

    void cleanMessageCache(Folder & folder);

    long long countBodiesDownloaded(Folder & folder);
    long long countBodiesNeeded(Folder & folder);
    time_t maxAgeForBodySync(Folder & folder);
    bool shouldCacheBodiesInFolder(Folder & folder);
    bool syncMessageBodies(Folder & folder, IMAPFolderStatus & remoteStatus);
    void syncMessageBody(Message * message);
};


#endif /* SyncWorker_hpp */
