//
//  MailStoreTransaction.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "MailStoreTransaction.hpp"

using namespace std;
using namespace std::chrono;

MailStoreTransaction::MailStoreTransaction(MailStore * store, string nameHint) :
    mStore(store), mCommited(false), mStart(system_clock::now()), mBegan(system_clock::now()), mNameHint(nameHint)
{
    mStore->beginTransaction();
    mBegan = system_clock::now();
}

MailStoreTransaction::~MailStoreTransaction() noexcept // nothrow
{
    if (false == mCommited) {
        try {
            mStore->rollbackTransaction();
        } catch (SQLite::Exception&) {
            // Never throw an exception in a destructor: error if
            // already rollbacked, but no harm is caused by this.
        }
    }
}

void MailStoreTransaction::commit()
{
    if (false == mCommited) {
        mStore->commitTransaction();
        mCommited = true;

        auto now = system_clock::now();
        long long waitingMs = duration_cast<std::chrono::milliseconds>(mBegan - mStart).count();
        long long selfMs = duration_cast<std::chrono::milliseconds>(now - mBegan).count();

        if (waitingMs > 1000) {
            spdlog::get("logger")->warn("[BUSY] Transaction={} waited {}ms to acquire write lock", mNameHint, waitingMs);
        }
        if (selfMs > 100) {
            spdlog::get("logger")->warn("[SLOW] Transaction={} held write lock for {}ms", mNameHint, selfMs);
        }
        
    } else {
        throw SQLite::Exception("Transaction already commited.");
    }
}


